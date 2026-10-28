/**
 * @file topbar.c
 * @brief hikari top bar telemetry helper — FreeBSD system telemetry with
 *        optional NVIDIA and AMD GPU support.
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
 * GPU blocks are shown only when a GPU is detected at startup: NVIDIA first
 * via nvidia-smi, then — only if nvidia-smi said nothing — AMD via the
 * platform sensor half's get_amd_gpu_info().  On machines where neither
 * answers the blocks are suppressed entirely — no dead placeholders, no error
 * text.
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
 *   sys/types.h   size_t and friends
 *   sys/param.h   BSD system parameters and limits
 *   sys/mount.h   statfs            (sys/vfs.h on Linux)
 *   time.h        time / localtime / strftime
 *   ifaddrs.h     getifaddrs / freeifaddrs
 *   arpa/inet.h   AF_INET
 *   netinet/in.h  sockaddr_in
 *   net/if.h      IFF_UP
 *   hikari/topbar_sensors.h  platform sensor contract (src/topbar_sensors_*.c)
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
#include <sys/param.h>
/* [COMMENT] Action purpose: get_home_usage() below calls statfs(2). Both
platforms provide it with the f_blocks and f_bfree members that function reads,
but from different headers -- <sys/mount.h> on FreeBSD, <sys/vfs.h> on Linux.
Branching the include leaves the function body itself identical on both, which
is why the disk sensor stays in this shared file instead of being duplicated
per platform. FreeBSD keeps the native spelling here: statvfs(3) is the
portable alternative and would delete this branch, but FreeBSD's own
statvfs(3) manual calls its output garbage that portable applications must not
depend on, and describes it as a lossy wrapper around statfs(). See
.devdocs/research/libc.md section 7c and .devdocs/research/contract.md C.5. */
#if defined(__linux__)
#include <sys/vfs.h>
#else
#include <sys/mount.h>
#endif
#include <time.h>
#include <ifaddrs.h>
#if defined(__linux__)
/* Action purpose: stat(2) for the sysfs wireless probe in get_net_status().
Linux only -- the FreeBSD path there needs no filesystem lookup. */
#include <sys/stat.h>
#endif
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>

#include <hikari/topbar_sensors.h>

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

/* Function purpose: Adopt the palette the compositor passed as argv[1] -- sixteen
 * "#rrggbb" values, comma separated.
 *
 * This is the primary source of colour. read_pywal_colors() below is kept as the
 * fallback for a helper started by hand or by an older compositor, and is not
 * consulted at all once this has succeeded: re-reading ~/.cache/wal/colors on the
 * throttled path would otherwise overwrite the configured theme a second later.
 *
 * Returns 1 when a complete palette was accepted. A partial or malformed argument
 * is rejected outright rather than half-applied, so a typo produces the pywal
 * fallback instead of eight themed blocks and eight white ones. */
static int compositor_palette_loaded = 0;

static int read_compositor_colors(const char *arg) {
    if (!arg) return 0;

    char parsed[16][10];
    int i = 0;
    int complete = 0;
    const char *cursor = arg;

    while (i < 16) {
        const char *comma = strchr(cursor, ',');
        /* Action purpose: 7 is "#rrggbb" exactly, and is what bounds the memcpy
         * below -- parsed[i] is 10 bytes, so a field of any other length is
         * rejected before it can be copied. */
        size_t len = comma ? (size_t)(comma - cursor) : strlen(cursor);

        if (len != 7) return 0;

        memcpy(parsed[i], cursor, len);
        parsed[i][len] = '\0';

        if (!is_valid_hex_color(parsed[i])) return 0;

        i++;

        if (!comma) {
            complete = 1;
            break;
        }

        cursor = comma + 1;
    }

    /* Action purpose: `complete` is what rejects a SEVENTEENTH colour, and the
     * check is not redundant with the count. The loop stops at sixteen whether
     * or not the argument ended, so counting alone would accept a trailing
     * field by silently ignoring it -- and a palette long enough to have a
     * seventeenth entry is a malformed one, not a truncatable one. */
    if (i != 16 || !complete) return 0;

    for (i = 0; i < 16; i++) {
        strncpy(pywal_colors[i], parsed[i], sizeof(pywal_colors[i]) - 1);
        pywal_colors[i][sizeof(pywal_colors[i]) - 1] = '\0';
    }

    compositor_palette_loaded = 1;

    return 1;
}

static void read_pywal_colors(void) {
    /* Action purpose: The compositor's palette wins outright. Without this
     * guard the throttled refresh below would reinstate pywal's colours a
     * second after the configured ones were applied. */
    if (compositor_palette_loaded) return;

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
    /* 1 when running on external power, 0 when on battery. Written by
     * get_bat_info(); each platform implementation documents its own source
     * (hw.acpi.acline on FreeBSD, the `online` attribute of a type == "Mains"
     * supply on Linux). Never derived from bat_state, which collapses several
     * distinct states onto one label. */
    int     bat_external;
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
 * GPU — AMD TELEMETRY  (platform sensor half, every ~1 s)
 *
 * No popen() and no second binary: on Linux this reads four sysfs attributes
 * of the card's PCI device directly, which is why it lives behind the
 * platform contract in include/hikari/topbar_sensors.h rather than beside
 * update_nvidia_info() above.  nvidia-smi stays here because it is the same
 * userland program on both platforms; amdgpu's attributes are a kernel
 * interface and are not.
 *
 * The failure contract is update_nvidia_info()'s, deliberately: the sensor
 * writes the four fields only when one card answered all four, so a partial
 * reading sets available to 0 and the caller suppresses all three blocks
 * rather than rendering some of them.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void update_amd_info(GPUInfo *gpu) {
    gpu->available = get_amd_gpu_info(&gpu->util, &gpu->vram_used,
                                      &gpu->vram_total, &gpu->temp);
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

/* Function purpose: Choose the palette entry the battery block is drawn in.
 *
 * Returns an index into the palette rather than a colour, so the bands follow
 * whatever `ui { palette }` the compositor passed -- retheme the desktop and the
 * battery retint with it, with nothing here to keep in step.
 *
 * The thresholds are the user's, and each band is inclusive at its lower bound:
 *
 *   external power            grey            color6
 *   75-100%                   purple          color4
 *   60-75%                    pink-purple     color5
 *   50-60%                    light yellow    color11
 *   35-50%                    yellow-orange   color3
 *   20-35%                    orange          color2
 *   10-20%                    light red       color9
 *    0-10%                    red             color1
 */
static int battery_color_index(int life, int external) {
    if (external) return 6;

    if (life >= 75) return 4;
    if (life >= 60) return 5;
    if (life >= 50) return 11;
    if (life >= 35) return 3;
    if (life >= 20) return 2;
    if (life >= 10) return 9;

    return 1;
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
        /* Action purpose: classify the interface as wireless or wired. Every
         * running non-loopback interface that is not wireless counts as wired,
         * so the many Ethernet drivers (igb, igc, ix, bge, ...) are covered
         * without an allowlist. Only the wireless test is platform specific,
         * and it has to be: the two systems name wireless interfaces by
         * different rules.
         *
         * On FreeBSD the name is the test. wlan(4) is the single interface
         * driver every 802.11 device is cloned onto, so a wireless interface
         * is always wlanN and the prefix is driver-derived rather than a
         * guess. Unchanged from before this split.
         *
         * On Linux the name is NOT the test, and using it was a real defect.
         * systemd's predictable interface names give wlp3s0, wlo1 and similar,
         * none of which begin with "wlan" -- so the prefix test failed and
         * every Wi-Fi-only laptop fell through to the wired branch and had its
         * top bar report ETHERNET. The kernel exposes the property directly
         * instead: a wireless interface carries a `wireless` directory, and a
         * cfg80211 one additionally a `phy80211` link, under
         * /sys/class/net/<ifname>/. Both are tested because `wireless` covers
         * the older wireless-extensions drivers as well.
         *
         * Honesty note on sourcing: the kernel's own
         * ABI/testing/sysfs-class-net page could not be retrieved from this
         * environment to quote (404 at the documented URL, while other
         * docs.kernel.org pages served normally), so this is the conventional
         * test rather than one quoted from the ABI document, and it is
         * untested here because the container exposes no wireless device.
         * It is nonetheless strictly better than the name prefix, which is
         * known-wrong on Linux rather than merely uncited. A failure of the
         * probe degrades to "wired", which is the same answer the old code
         * gave, so the worst case is unchanged behaviour. */
        int is_wifi;
#if defined(__linux__)
        char wpath[sizeof("/sys/class/net//phy80211") + IFNAMSIZ];
        struct stat wst;

        is_wifi = (snprintf(wpath, sizeof(wpath), "/sys/class/net/%s/wireless",
                       ifa->ifa_name) < (int)sizeof(wpath) &&
                      stat(wpath, &wst) == 0) ||
                  (snprintf(wpath, sizeof(wpath), "/sys/class/net/%s/phy80211",
                       ifa->ifa_name) < (int)sizeof(wpath) &&
                      stat(wpath, &wst) == 0);
#else
        is_wifi = strncmp(ifa->ifa_name, "wlan", 4) == 0;
#endif

        if (is_wifi) wifi = 1;
        else         eth  = 1;
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
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    struct stats s;
    memset(&s, 0, sizeof(s));

    /* Prime the CPU delta accumulator so the first real reading is valid */
    get_cpu_usage();

    /* Action purpose: argv[1] is the compositor's palette. Falls through to the
     * pywal cache when absent or malformed -- see read_compositor_colors(). */
    if (argc > 1) read_compositor_colors(argv[1]);
    read_pywal_colors();

    /* ── GPU grace-detection ────────────────────────────────────────────
     * Probe the dedicated GPU slot once.  If nvidia-smi cannot produce a
     * parseable response the nvidia_present flag stays 0 and every NVIDIA
     * branch below is skipped for the lifetime of the process.
     *
     * Action purpose: NVIDIA is probed FIRST and AMD only when NVIDIA did not
     * answer, so a machine with an NVIDIA card behaves exactly as it did
     * before AMD support existed -- same probe, same single nvidia-smi call
     * per second, same numbers.  On the rare machine carrying both, NVIDIA
     * therefore wins.  That is the conservative choice rather than a judgement
     * about which card matters more: the bar has one GPU slot and three
     * blocks, the NVIDIA reading is the one this program has always shown, and
     * changing which card an existing machine reports would be a regression
     * for its owner.  A machine with an AMD card and no NVIDIA driver falls
     * through to the AMD probe and gains the blocks it never had.
     *
     * Both flags are latched here and never recomputed, which is the existing
     * behaviour: a driver that appears mid-session is not picked up, and a
     * card that stops answering empties the blocks rather than switching
     * vendors.  On FreeBSD get_amd_gpu_info() always returns 0, so
     * amd_present stays 0 and nothing about that platform changes.
     * ─────────────────────────────────────────────────────────────────── */
    update_nvidia_info(&s.gpu);
    const int nvidia_present = s.gpu.available;

    if (!nvidia_present) update_amd_info(&s.gpu);
    const int amd_present = !nvidia_present && s.gpu.available;

    /* swaybar protocol header — display-only, no click events */
    printf("{\"version\":1}\n[\n");

    unsigned long ticks = 0;
    while (1) {
        /* 200 ms tick — simple, no stdin to monitor */
        usleep(200000);

        /* ── Fast path: cheap sensors polled every tick ── */
        get_bat_info(&s.bat_life, s.bat_state, &s.bat_external);

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
            if      (nvidia_present) update_nvidia_info(&s.gpu);
            else if (amd_present)    update_amd_info(&s.gpu);
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

        /* GPU blocks — emitted only on systems with a detected GPU, and only
         * while that GPU is still answering.  The vendor is settled at
         * startup; from here down the three blocks are identical either way,
         * because GPUInfo carries the same four fields and the same units
         * whichever sensor filled it. */
        if ((nvidia_present || amd_present) && s.gpu.available) {
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

        /* ── Status group: right-aligned ────────────────────────────────
         *
         * The fixed-width spacer that used to sit here ({"min_width":400}) is
         * gone. It carried no "align" field, so it -- and every block after it
         * -- fell into the LEFT run, and this group only *looked* centred
         * because 400px of padding happened to land it near the middle at
         * 1920px wide. It drifted with the output width and with whichever
         * left-hand blocks were present. The bar now has a real centre run
         * (hikari_bar_refresh, src/bar.c) which the clock uses instead.
         *
         * These are emitted in the order they should read left-to-right on
         * screen: hikari_bar_refresh() lays the right run out from its computed
         * origin and advances rightward in emission order, so this sequence is
         * the displayed sequence, not its reverse. */

        /* Network */
        printf("{\"full_text\":\"  %s \",\"color\":\"%s\","
               "\"align\":\"right\"},",
               s.net_status, pywal_colors[6]);

        /* Backlight -- omitted when no backlight tool is available */
        if (s.backlight >= 0) {
            printf("{\"full_text\":\" 󰃠 %d%% \",\"color\":\"%s\","
                   "\"align\":\"right\"},",
                   s.backlight, pywal_colors[10]);
        }

        /* Volume -- omitted when no sink is readable */
        if (s.volume >= 0) {
            printf("{\"full_text\":\" 󰖀 %d%% \",\"color\":\"%s\","
                   "\"align\":\"right\"},",
                   s.volume, pywal_colors[9]);
        }

        /* Battery -- omitted when no battery is present. Coloured by charge
         * band, or by the plugged-in colour when on external power; see
         * battery_color_index(). */
        if (s.bat_life >= 0) {
            printf("{\"full_text\":\" 󰁹 %d%% \",\"color\":\"%s\","
                   "\"align\":\"right\"},",
                   s.bat_life,
                   pywal_colors[battery_color_index(s.bat_life,
                                                    s.bat_external)]);
        }

        /* Clock and date -- centre run, emitted last so it carries the
         * array's closing block with no trailing comma. */
        printf("{\"full_text\":\"  %s \","
               "\"color\":\"%s\",\"align\":\"center\"}",
               s.full_time_str, pywal_colors[7]);

        printf("],\n");
        fflush(stdout);
        ticks++;

    }

    return 0;
}
