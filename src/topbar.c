/**
 * @file topbar.c
 * @brief hikari top bar telemetry helper — FreeBSD system telemetry with
 *        optional NVIDIA GPU support.
 *
 * Built as the standalone `hikari-topbar` binary and installed to
 * ${PREFIX}/bin. The compositor spawns it during startup and reads this
 * swaybar-protocol JSON stream over a non-blocking pipe; `src/bar.c` parses
 * each line and renders it into the scene graph as the native top bar.
 *
 * This deliberately remains a SEPARATE PROCESS. The sensors below are sampled
 * with blocking popen() calls (pactl, playerctl, nvidia-smi, pciconf) several
 * times a second; running them inside the compositor would stall the Wayland
 * event loop on every tick. Keeping them out-of-process means a slow or wedged
 * sensor degrades the bar's freshness and nothing else.
 *
 * Output is display-only; no click events are handled.
 *
 * GPU blocks are shown only when an NVIDIA GPU is detected at startup via
 * nvidia-smi.  On purely integrated-GPU machines the blocks are suppressed
 * entirely — no dead placeholders, no error text.
 *
 * Update cadence
 *   200 ms  — battery  (fast-changing, cheap sysctl)
 *     1 s   — CPU, RAM, thermals, GPU, disk, network, MPRIS, pywal palette,
 *             volume, backlight  (each of the last two forks a shell
 *             pipeline via popen, too expensive for the 200ms tick)
 *
 * LIBRARIES
 *   stdio.h       printf / popen / fgets / fclose
 *   stdlib.h      malloc / free / getenv
 *   string.h      strcpy / strncmp / memcpy / strlen
 *   unistd.h      usleep
 *   sys/sysctl.h  sysctlbyname  (FreeBSD kernel interface)
 *   sys/types.h   size_t and friends
 *   sys/param.h   CPUSTATES / CP_IDLE / MAXPATHLEN
 *   sys/mount.h   getfsstat / statfs
 *   sys/vmmeter.h vmtotal (pulled in by sys/param.h on FreeBSD)
 *   time.h        time / localtime / strftime
 *   ifaddrs.h     getifaddrs / freeifaddrs
 *   arpa/inet.h   AF_INET
 *   netinet/in.h  sockaddr_in
 *   net/if.h      IFF_UP
 */

/* [COMMENT] Action purpose: sys/types.h must precede sys/mount.h. FreeBSD's
sys/mount.h pulls in sys/ucred.h, whose struct xucred uses u_int -- a type
sys/types.h only exposes when __BSD_VISIBLE is set. FreeBSD's sys/cdefs.h clears
__BSD_VISIBLE whenever _POSIX_C_SOURCE/_XOPEN_SOURCE/_ANSI_SOURCE is defined, so
defining any of those hides u_int, IFF_UP, and (at 200809L) usleep. The Makefile
defines none of them; .clangd previously did, which is why the editor reported
errors for code that builds cleanly. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/mount.h>
#include <sys/vmmeter.h>
#include <time.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * JSON STRING ESCAPING
 *
 * Any nonconstant string emitted through the swaybar protocol (MPRIS track
 * metadata, in particular, is fully attacker/user controlled -- artist and
 * title tags can contain quotes, backslashes, or control characters) must be
 * escaped before being embedded in a full_text field, or the emitted line is
 * not valid JSON and hikari's parser (src/bar.c) desyncs.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void json_escape(const char *in, char *out, size_t out_size) {
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 1 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        const char *esc = NULL;
        char buf[8];

        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                if (c < 0x20) {
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    esc = buf;
                }
                break;
        }

        if (esc != NULL) {
            size_t elen = strlen(esc);
            if (o + elen + 1 > out_size) break;
            memcpy(out + o, esc, elen);
            o += elen;
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PYWAL COLOUR PALETTE
 * ═══════════════════════════════════════════════════════════════════════════ */

static char pywal_colors[16][10];

static int is_valid_hex_color(const char *s) {
    if (s[0] != '#') return 0;
    for (int i = 1; i < 7; i++) {
        if (!isxdigit((unsigned char)s[i])) return 0;
    }
    return s[7] == '\0';
}

static void read_pywal_colors(void) {
    for (int i = 0; i < 16; i++) strcpy(pywal_colors[i], "#ffffff");

    const char *home = getenv("HOME");
    if (!home) return;

    char path[256];
    int n = snprintf(path, sizeof(path), "%s/.cache/wal/colors", home);
    if (n < 0 || (size_t)n >= sizeof(path)) return;

    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[16];
    int i = 0;
    while (fgets(line, sizeof(line), fp) && i < 16) {
        line[strcspn(line, "\n")] = '\0';
        if (is_valid_hex_color(line)) {
            strncpy(pywal_colors[i], line, sizeof(pywal_colors[i]) - 1);
            pywal_colors[i][sizeof(pywal_colors[i]) - 1] = '\0';
        }
        i++;
    }
    fclose(fp);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GPU INFO STRUCTURE
 *
 * Encapsulates all telemetry for a single GPU unit.  Using a dedicated
 * struct (promoted from gpu_monitor.c) keeps GPU state self-contained and
 * makes the grace-detection logic trivial: check gpu.available.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float util;         /* Core utilisation, percent                 */
    long  vram_used;    /* Used VRAM, MiB                            */
    long  vram_total;   /* Total VRAM, MiB                           */
    int   temp;         /* Die temperature, °C                       */
    int   available;    /* 1 = driver alive and responding, 0 = not  */
} GPUInfo;

/* ═══════════════════════════════════════════════════════════════════════════
 * COMBINED SYSTEM STATS
 * ═══════════════════════════════════════════════════════════════════════════ */

struct stats {
    int     cpu_temp;
    double  cpu_usage;
    double  mem_usage;
    int     bat_life;
    char    bat_state[32];
    char    net_status[32];
    double  home_usage;
    int     volume;
    int     backlight;
    char    mpris[128];
    char    full_time_str[128];
    GPUInfo gpu;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * GPU — NVIDIA TELEMETRY  (nvidia-smi, every ~1 s)
 *
 * --query-gpu provides specific metrics; --format=csv,noheader,nounits
 * yields raw numbers that sscanf() can consume directly.
 *
 * On any failure (binary absent, driver error, parse mismatch) available is
 * set to 0 — the caller will suppress GPU bar blocks accordingly.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void update_nvidia_info(GPUInfo *gpu) {
    FILE *fp = popen(
        "nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total,"
        "temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");

    if (!fp) { gpu->available = 0; return; }

    char buf[128];
    if (fgets(buf, sizeof(buf), fp)) {
        int used, total;
        if (sscanf(buf, "%f, %d, %d, %d",
                   &gpu->util, &used, &total, &gpu->temp) == 4) {
            gpu->vram_used  = used;
            gpu->vram_total = total;
            gpu->available  = 1;
        } else {
            gpu->available  = 0;   /* driver present but state unreadable */
        }
    } else {
        gpu->available = 0;
    }
    pclose(fp);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MPRIS — current track via playerctl
 * ═══════════════════════════════════════════════════════════════════════════ */

static void get_mpris_info(char *buf, size_t size) {
    FILE *fp = popen(
        "playerctl metadata --format '{{artist}} - {{title}}' 2>/dev/null", "r");
    if (!fp) { buf[0] = '\0'; return; }
    if (fgets(buf, (int)size, fp)) {
        size_t l = strlen(buf);
        if (l > 0 && buf[l - 1] == '\n') buf[l - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
    pclose(fp);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CPU — usage (delta between successive kern.cp_time snapshots)
 *
 * The previous snapshot is kept in file-scope so get_cpu_usage() can be
 * called repeatedly without passing state explicitly.  A warm-up call in
 * main() discards the first (meaningless) 100% reading.
 * ═══════════════════════════════════════════════════════════════════════════ */

static long last_cp_time[CPUSTATES];

static double get_cpu_usage(void) {
    long   cp_time[CPUSTATES];
    size_t len = sizeof(cp_time);

    /* cp_time is an array; pass it directly — no & needed */
    if (sysctlbyname("kern.cp_time", cp_time, &len, NULL, 0) == -1)
        return 0.0;

    long sum = 0;
    for (int i = 0; i < CPUSTATES; i++)
        sum += cp_time[i] - last_cp_time[i];

    double usage = 0.0;
    if (sum > 0)
        usage = 100.0 * (1.0 -
            (double)(cp_time[CP_IDLE] - last_cp_time[CP_IDLE]) / sum);

    memcpy(last_cp_time, cp_time, sizeof(cp_time));
    return usage;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CPU — temperature
 *
 * Tries the per-core sysctl first (coretemp driver), then falls back to the
 * ACPI thermal zone.  Raw values > 1000 are in FreeBSD's tenths-of-Kelvin
 * format and are converted to °C.
 * ═══════════════════════════════════════════════════════════════════════════ */

static int get_cpu_temp(void) {
    int    temp;
    size_t len = sizeof(temp);

    if (sysctlbyname("dev.cpu.0.temperature", &temp, &len, NULL, 0) == -1) {
        /* sysctlbyname writes the returned size back through len, so the
         * failed first call can leave it inconsistent with sizeof(temp). */
        len = sizeof(temp);
        if (sysctlbyname("hw.acpi.thermal.tz0.temperature",
                         &temp, &len, NULL, 0) == -1)
            return -1;
    }

    return (temp > 1000) ? (temp - 2732) / 10 : temp;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MEMORY — usage
 *
 * active + wired pages represent genuinely committed memory.  Dividing by
 * total physical memory gives a reasonable real-usage percentage that is not
 * inflated by the page cache (as 'used' in top would be).
 * ═══════════════════════════════════════════════════════════════════════════ */

static double get_mem_usage(void) {
    long long    total;
    unsigned int active, wire;
    int          pagesize;
    size_t       len;

    len = sizeof(total);
    if (sysctlbyname("hw.physmem", &total, &len, NULL, 0) == -1) return 0.0;
    len = sizeof(pagesize);
    if (sysctlbyname("hw.pagesize", &pagesize, &len, NULL, 0) == -1) return 0.0;
    len = sizeof(active);
    if (sysctlbyname("vm.stats.vm.v_active_count", &active, &len, NULL, 0) == -1) return 0.0;
    len = sizeof(wire);
    if (sysctlbyname("vm.stats.vm.v_wire_count", &wire, &len, NULL, 0) == -1) return 0.0;

    if (total <= 0 || pagesize <= 0) return 0.0;

    return 100.0 * (long long)(active + wire) * pagesize / total;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BATTERY — life percent and charge state
 * ═══════════════════════════════════════════════════════════════════════════ */

static void get_bat_info(int *life, char *state) {
    size_t len = sizeof(*life);
    if (sysctlbyname("hw.acpi.battery.life", life, &len, NULL, 0) == -1)
        *life = -1;

    int s; len = sizeof(s);
    if (sysctlbyname("hw.acpi.battery.state", &s, &len, NULL, 0) == 0) {
        if      (s == 0) strcpy(state, "Full");
        else if (s &  1) strcpy(state, "Discharging");
        else if (s &  2) strcpy(state, "Charging");
        else             strcpy(state, "AC");
    } else {
        strcpy(state, "Unknown");
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NETWORK — connection type
 *
 * Walks the interface list.  Prefers WIFI; falls back to wired Ethernet
 * (em / re drivers cover the most common FreeBSD NICs).
 * ═══════════════════════════════════════════════════════════════════════════ */

static void get_net_status(char *buf) {
    struct ifaddrs *ifaddr, *ifa;
    int wifi = 0, eth = 0;

    if (getifaddrs(&ifaddr) == -1) { strcpy(buf, "DISCONNECTED"); return; }

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        /* Accept both address families so an IPv6-only host is not reported
         * as disconnected. */
        if (ifa->ifa_addr->sa_family != AF_INET &&
            ifa->ifa_addr->sa_family != AF_INET6) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if ((ifa->ifa_flags & (IFF_UP | IFF_RUNNING)) !=
            (IFF_UP | IFF_RUNNING)) continue;
        /* Only the wireless prefix is driver-derived on FreeBSD; every other
         * running non-loopback interface counts as wired, so the many
         * Ethernet drivers (igb, igc, ix, bge, ...) are covered without an
         * allowlist. */
        if (strncmp(ifa->ifa_name, "wlan", 4) == 0) wifi = 1;
        else                                        eth  = 1;
    }
    freeifaddrs(ifaddr);

    if      (wifi) strcpy(buf, "WIFI");
    else if (eth)  strcpy(buf, "ETHERNET");
    else           strcpy(buf, "DISCONNECTED");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DISK — home partition usage
 *
 * Iterates mounted filesystems and matches against $HOME.  Using getenv()
 * here is correct — HOME is set by the login shell before swaybar starts.
 * ═══════════════════════════════════════════════════════════════════════════ */

static double get_home_usage(void) {
    const char *home = getenv("HOME");
    if (!home) home = "/home";

    struct statfs fs;
    if (statfs(home, &fs) == -1) return 0.0;
    if (fs.f_blocks == 0) return 0.0;

    return 100.0 * (double)(fs.f_blocks - fs.f_bfree) / (double)fs.f_blocks;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AUDIO — default sink volume via PulseAudio / PipeWire-pulse
 * ═══════════════════════════════════════════════════════════════════════════ */

static int get_volume(void) {
    FILE *fp = popen(
        "pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null "
        "| grep -oE '[0-9]+%' | head -1 | tr -d '%'", "r");
    if (!fp) return -1;
    int vol = -1;
    fscanf(fp, "%d", &vol);
    pclose(fp);
    return vol;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BACKLIGHT — current brightness level
 * ═══════════════════════════════════════════════════════════════════════════ */

static int get_backlight(void) {
    FILE *fp = popen(
        "backlight 2>/dev/null | grep brightness | awk '{print $2}'", "r");
    if (!fp) return -1;
    int level = -1;
    fscanf(fp, "%d", &level);
    pclose(fp);
    return level;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    struct stats s;
    memset(&s, 0, sizeof(s));
    memset(last_cp_time, 0, sizeof(last_cp_time));

    /* Prime the CPU delta accumulator so the first real reading is valid */
    get_cpu_usage();
    read_pywal_colors();

    /* ── NVIDIA grace-detection ─────────────────────────────────────────
     * Probe the dedicated GPU slot once.  If nvidia-smi cannot produce a
     * parseable response the nvidia_present flag stays 0 and every GPU
     * rendering branch below is skipped for the lifetime of the process.
     * No GPU blocks will appear on machines without a dedicated NVIDIA GPU.
     * ─────────────────────────────────────────────────────────────────── */
    update_nvidia_info(&s.gpu);
    const int nvidia_present = s.gpu.available;

    /* swaybar protocol header — display-only, no click events */
    printf("{\"version\":1}\n[\n");

    unsigned long ticks = 0;
    while (1) {
        /* 200 ms tick — simple, no stdin to monitor */
        usleep(200000);

        /* ── Fast path: cheap sensors polled every tick ── */
        get_bat_info(&s.bat_life, s.bat_state);

        /* ── Slow path: heavier calls throttled to ~1 s ── */
        if (ticks % 5 == 0) {
            /* get_volume and get_backlight each fork a shell pipeline;
             * sampling them every 200ms tick costs roughly 50 process
             * creations per second, so they belong on the throttled path. */
            s.volume     = get_volume();
            s.backlight  = get_backlight();
            read_pywal_colors();
            s.cpu_usage  = get_cpu_usage();
            s.cpu_temp   = get_cpu_temp();
            s.mem_usage  = get_mem_usage();
            s.home_usage = get_home_usage();
            get_net_status(s.net_status);
            get_mpris_info(s.mpris, sizeof(s.mpris));
            if (nvidia_present) update_nvidia_info(&s.gpu);
        }

        time_t     now = time(NULL);
        struct tm *t   = localtime(&now);
        strftime(s.full_time_str, sizeof(s.full_time_str),
                 "%H:%M | %a, %b %e", t);

        /* ── Render bar ─────────────────────────────────────────────── */
        printf("[");

        /* CPU usage */
        printf("{\"full_text\":\"  %.0f%% \",\"color\":\"%s\"},",
               s.cpu_usage, pywal_colors[2]);

        /* RAM usage */
        printf("{\"full_text\":\"  %.0f%% \",\"color\":\"%s\"},",
               s.mem_usage, pywal_colors[3]);

        /* CPU temperature -- omitted when no sensor is readable */
        if (s.cpu_temp >= 0) {
            printf("{\"full_text\":\"  %d°C \",\"color\":\"%s\"},",
                   s.cpu_temp, pywal_colors[4]);
        }

        /* GPU blocks — emitted only on systems with a detected NVIDIA GPU */
        if (nvidia_present && s.gpu.available) {
            printf("{\"full_text\":\" 󰢮 %.0f%% \",\"color\":\"%s\"},",
                   s.gpu.util, pywal_colors[1]);
            printf("{\"full_text\":\" %ld/%ld MiB \",\"color\":\"%s\"},",
                   s.gpu.vram_used, s.gpu.vram_total, pywal_colors[11]);
            printf("{\"full_text\":\" 󰏈 %d°C \",\"color\":\"%s\"},",
                   s.gpu.temp, pywal_colors[12]);
        }

        /* Home partition usage */
        printf("{\"full_text\":\" 󰋊 %.0f%% \",\"color\":\"%s\"},",
               s.home_usage, pywal_colors[5]);

        /* Media / MPRIS */
        if (s.mpris[0]) {
            char mpris_escaped[256];
            json_escape(s.mpris, mpris_escaped, sizeof(mpris_escaped));
            printf("{\"full_text\":\"  %s \","
                   "\"color\":\"%s\",\"align\":\"left\"},",
                   mpris_escaped, pywal_colors[14]);
        } else
            printf("{\"full_text\":\"  Idle \","
                   "\"color\":\"%s\",\"align\":\"left\"},",
                   pywal_colors[8]);

        /* Spacer (pushes right-side items toward the edge) */
        printf("{\"full_text\":\"\",\"separator\":false,\"min_width\":400},");


        /* Network */
        printf("{\"full_text\":\"  %s \",\"color\":\"%s\"},",
               s.net_status, pywal_colors[6]);

        /* Volume -- omitted when no sink is readable */
        if (s.volume >= 0) {
            printf("{\"full_text\":\" 󰖀 %d%% \",\"color\":\"%s\"},",
                   s.volume, pywal_colors[9]);
        }

        /* Backlight -- omitted when no backlight tool is available */
        if (s.backlight >= 0) {
            printf("{\"full_text\":\" 󰃠 %d%% \",\"color\":\"%s\"},",
                   s.backlight, pywal_colors[10]);
        }

        /* Battery -- omitted when no battery is present */
        if (s.bat_life >= 0) {
            printf("{\"full_text\":\" 󰁹 %d%% \",\"color\":\"%s\"},",
                   s.bat_life, pywal_colors[8]);
        }

        /* Clock (no trailing comma — last block in the array) */
        printf("{\"full_text\":\"  %s \","
               "\"color\":\"%s\",\"align\":\"right\"}",
               s.full_time_str, pywal_colors[7]);

        printf("],\n");
        fflush(stdout);
        ticks++;

    }

    return 0;
}
