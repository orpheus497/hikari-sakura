/**
 * @file topbar_sensors_linux.c
 * @brief Linux implementations of the hikari-topbar sensor contract.
 *
 * Script function and purpose: Supplies the six sensors declared in
 * include/hikari/topbar_sensors.h for Linux -- CPU usage, CPU temperature,
 * memory usage, battery, backlight, and AMD GPU telemetry -- by reading /proc
 * and /sys directly. src/topbar_sensors_freebsd.c supplies the same six from
 * sysctl(3); the Makefile links exactly one of the two by ${OS}. A caller in
 * src/topbar.c must not be able to tell which one it got: the sentinels, the
 * units and the ranges below were taken from the FreeBSD file function by
 * function, and the state label set is the closed five-literal set the header
 * pins. get_amd_gpu_info() is the one exception in spirit rather than in
 * form -- FreeBSD has no amdgpu sysfs to read, so its half answers
 * "unavailable" and the caller sees the same suppressed blocks it saw before
 * the sensor existed.
 *
 * Nothing that is portable lives here. The JSON escaping, the pywal palette,
 * the swaybar emitter, main(), the network and disk sensors and the
 * pactl / playerctl / nvidia-smi helpers all stay in src/topbar.c, exactly as
 * they do for the FreeBSD build. nvidia-smi in particular stays there even
 * though its neighbour on the bar is implemented here: it is the same userland
 * program on both platforms, which is the rule the header states, while
 * amdgpu's attributes are a Linux kernel interface.
 *
 * Every read goes through read_sysfile() below: no fork, no shell, no
 * allocation, nothing that can block on a wedged daemon. Any file may be
 * missing (a container, a VM, a desktop with no battery), present but empty,
 * or unreadable; every such case degrades to the same sentinel the FreeBSD
 * file returns when sysctlbyname(3) fails, so the bar omits the block rather
 * than rendering a dead one. get_bat_info() in particular is on the caller's
 * 200 ms tick, so its whole cost is one opendir() and a handful of short
 * reads.
 *
 * The interfaces, the units and every selection policy below are quoted and
 * justified in .devdocs/research/sysfs.md (sections 1-5) and
 * .devdocs/research/proc.md (sections 1-3). Where those notes record a point
 * as unverified it is marked as such here too, and nothing is asserted about
 * an ABI that the kernel documentation does not state.
 *
 * LIBRARIES
 *   dirent.h   opendir / readdir / closedir  (sysfs class enumeration)
 *   errno.h    errno, EINTR, ERANGE
 *   fcntl.h    open
 *   stdio.h    snprintf / sscanf
 *   stdlib.h   strtoll / strtoull
 *   string.h   strcmp / strncmp / strlen / strchr / memcpy / strcpy
 *   unistd.h   read / close
 */

/* Action purpose: No feature-test macro is defined here, deliberately, and the
   file still compiles against POSIX declarations. The hikari-topbar build line
   uses -std=gnu11, which does not define __STRICT_ANSI__, and both glibc's and
   musl's features.h enable _DEFAULT_SOURCE by themselves in that case -- so
   open(2), read(2) and the dirent interfaces are visible without one. Defining
   _POSIX_C_SOURCE instead would work here but would diverge from
   src/topbar.c and src/topbar_sensors_freebsd.c, where any such macro is
   actively harmful (it clears FreeBSD's __BSD_VISIBLE). Keeping all three
   files macro-free keeps one rule for the whole helper. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hikari/topbar_sensors.h>

/* Longest path this file builds: a /sys/class/<class>/ prefix, a device name
 * (NAME_MAX is 255 on Linux) and a short attribute leaf. */
#define SYSPATH_MAX 512
#define SYSNAME_MAX 256

#define POWER_DIR     "/sys/class/power_supply"
#define HWMON_DIR     "/sys/class/hwmon"
#define THERMAL_DIR   "/sys/class/thermal"
#define BACKLIGHT_DIR "/sys/class/backlight"
#define DRM_DIR       "/sys/class/drm"

/* ═══════════════════════════════════════════════════════════════════════════
 * FILE ACCESS — the only way this file touches the kernel
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Function purpose: Read a whole small /proc or /sys file into buf as a
 * NUL-terminated string. Returns 1 when at least one byte was read, 0 on any
 * failure -- a missing file, EACCES, a read error, or an empty file. Callers
 * treat that 0 exactly as the FreeBSD file treats sysctlbyname(3) returning
 * -1, which is what keeps the two implementations' failure contracts
 * identical. The buffer is always NUL-terminated on return, including on
 * failure, so a caller that ignores the result still sees an empty string
 * rather than stack garbage. */
static int read_sysfile(const char *path, char *buf, size_t size)
{
    if (size == 0)
        return 0;

    buf[0] = '\0';

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd == -1)
        return 0;

    size_t off = 0;
    while (off + 1 < size) {
        ssize_t n = read(fd, buf + off, size - 1 - off);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n == 0)
            break;
        if (errno == EINTR)
            continue;
        close(fd);
        buf[0] = '\0';
        return 0;
    }

    close(fd);
    buf[off] = '\0';
    return off > 0;
}

/* Function purpose: Strip the trailing newline sysfs string attributes carry,
 * plus any trailing blanks, so the value can be compared with strcmp(). */
static void chomp(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' '  || s[n - 1] == '\t'))
        s[--n] = '\0';
}

/* Function purpose: Compose "<dir>/<leaf>". Returns 0 rather than a truncated
 * path when the result would not fit, so a pathological device name can never
 * make a read land on the wrong file. */
static int join_path(char *out, size_t size, const char *dir, const char *leaf)
{
    int n = snprintf(out, size, "%s/%s", dir, leaf);
    return (n > 0 && (size_t)n < size);
}

/* Function purpose: Read a /proc or /sys file holding one integer. */
static int read_sysfile_ll(const char *path, long long *out)
{
    char  buf[64];
    char *end;

    if (!read_sysfile(path, buf, sizeof(buf)))
        return 0;

    errno = 0;
    long long v = strtoll(buf, &end, 10);
    if (end == buf || errno == ERANGE)
        return 0;

    *out = v;
    return 1;
}

/* Function purpose: Read the string attribute <dir>/<leaf>, trimmed. */
static int read_attr(const char *dir, const char *leaf, char *buf, size_t size)
{
    char path[SYSPATH_MAX];

    if (!join_path(path, sizeof(path), dir, leaf))
        return 0;
    if (!read_sysfile(path, buf, size))
        return 0;

    chomp(buf);
    return 1;
}

/* Function purpose: Read the integer attribute <dir>/<leaf>. */
static int read_attr_ll(const char *dir, const char *leaf, long long *out)
{
    char path[SYSPATH_MAX];

    if (!join_path(path, sizeof(path), dir, leaf))
        return 0;

    return read_sysfile_ll(path, out);
}

/* Function purpose: Match a sysfs directory entry of the form <prefix><index>
 * exactly -- "hwmon3", "thermal_zone0" -- and hand back the index. The
 * end-of-string test is what makes it exact: without it a trailing-garbage
 * name would be accepted with its leading digits. */
static int parse_indexed(const char *name, const char *prefix, unsigned long *index)
{
    size_t plen = strlen(prefix);
    if (strncmp(name, prefix, plen) != 0)
        return 0;

    const char *digits = name + plen;
    if (*digits < '0' || *digits > '9')
        return 0;

    char *end;
    errno = 0;
    unsigned long v = strtoul(digits, &end, 10);
    if (*end != '\0' || errno == ERANGE)
        return 0;

    *index = v;
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CPU — usage (delta between successive /proc/stat snapshots)
 *
 * The FreeBSD side keeps long[CPUSTATES] from kern.cp_time; this side keeps
 * the eight columns of the aggregate "cpu" line. Neither snapshot means
 * anything to the other, which is why the header gives each implementation its
 * own and src/topbar.c sees none of it. A warm-up call in main() discards the
 * first reading on both platforms.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Columns 1..8 of the aggregate line: user nice system idle iowait irq
 * softirq steal. proc_stat(5) documents guest (9) and guest_nice (10) as well;
 * see the comment in get_cpu_usage() for why they are not summed. */
#define CPU_FIELDS 8
#define CPU_IDLE   3
#define CPU_IOWAIT 4

static unsigned long long last_cpu[CPU_FIELDS];

/* Function purpose: Fill f[] from the aggregate "cpu" line of /proc/stat.
 * Returns 0 when the file cannot be read or holds no such line, which is this
 * file's equivalent of sysctlbyname("kern.cp_time") failing. */
static int read_cpu_fields(unsigned long long *f)
{
    char buf[4096];

    if (!read_sysfile("/proc/stat", buf, sizeof(buf)))
        return 0;

    for (char *line = buf; line != NULL; ) {
        char *nl = strchr(line, '\n');

        /* A short read can truncate the tail, so only a newline-terminated
         * line is a complete one. The aggregate line is the first in the file
         * on every kernel seen, far inside this buffer. */
        if (nl == NULL)
            break;
        *nl = '\0';

        /* Action purpose: match the first token for equality with "cpu", not
         * by prefix and not by byte offset. A bare strncmp(line, "cpu", 3)
         * also matches the per-CPU "cpu0" lines, which are not the aggregate;
         * and the two kernel sources quoted in .devdocs/research/proc.md 1.3
         * disagree on how many spaces follow the token, so the numbers cannot
         * be read from a fixed column either. strtoull() below skips the run
         * of blanks for us. */
        if (strncmp(line, "cpu", 3) == 0 && (line[3] == ' ' || line[3] == '\t')) {
            const char *p   = line + 3;
            int         got = 0;

            for (int i = 0; i < CPU_FIELDS; i++)
                f[i] = 0;

            /* Action purpose: stop at end of line rather than demanding eight
             * columns. proc_stat(5) marks iowait, irq, softirq and steal with
             * "(since Linux x.y.z)", so the column count is kernel-dependent;
             * an absent column stays 0 and contributes nothing to either
             * delta. */
            for (int i = 0; i < CPU_FIELDS; i++) {
                char *end;
                unsigned long long v = strtoull(p, &end, 10);
                if (end == p)
                    break;
                f[i] = v;
                p    = end;
                got  = 1;
            }

            return got;
        }

        line = nl + 1;
    }

    return 0;
}

double get_cpu_usage(void)
{
    unsigned long long cur[CPU_FIELDS];

    /* The snapshot is deliberately NOT updated on this path -- see the header
     * contract. The next successful call then measures across the whole gap,
     * which is what the FreeBSD early return before its memcpy() does. */
    if (!read_cpu_fields(cur))
        return 0.0;

    /* Action purpose: each delta is taken as an unsigned subtraction cast to
     * signed, which yields the small negative value rather than a near-2^64
     * one when a counter goes backwards. That is not hypothetical here:
     * proc_stat(5) says of iowait, verbatim, "The value in this field may
     * decrease in certain conditions." The FreeBSD side subtracts signed longs
     * and so already behaves this way; this keeps the arithmetic identical. */
    long long total = 0;
    for (int i = 0; i < CPU_FIELDS; i++)
        total += (long long)(cur[i] - last_cpu[i]);

    /* Action purpose: idle is two columns on Linux, one on FreeBSD. FreeBSD
     * has no iowait state at all -- a thread blocked on I/O leaves the CPU in
     * the idle loop, so that time lands in CP_IDLE and is not counted as busy.
     * Counting Linux's iowait as busy would make the same disk-bound workload
     * read materially higher here than on FreeBSD. Columns 9 and 10 (guest,
     * guest_nice) are left out of the total: they are widely held to be
     * already included in user and nice, which would double-count them, and
     * that relationship is NOT stated in proc_stat(5) or the kernel docs --
     * see .devdocs/research/proc.md 4.1, where it is recorded as unverified.
     * steal is counted as busy: it is time the vCPU was denied, which is
     * neither idle nor usable. */
    long long idle = (long long)(cur[CPU_IDLE]   - last_cpu[CPU_IDLE])
                   + (long long)(cur[CPU_IOWAIT] - last_cpu[CPU_IOWAIT]);

    double usage = 0.0;
    if (total > 0)
        usage = 100.0 * (1.0 - (double)idle / (double)total);

    /* Action purpose: clamp to the range the caller formats as a percentage.
     * proc_stat(5) warns that these counters can momentarily step BACKWARDS,
     * and iowait is called out by name -- the comment above quotes it. A
     * backwards step in iowait alone makes the idle delta negative while total
     * stays positive, so the expression above exceeds 100 and the bar renders
     * something like "103%". FreeBSD needs no equivalent: kern.cp_time is
     * monotonic, so this cannot fire there and the FreeBSD implementation is
     * deliberately left without it. */
    if (usage < 0.0)
        usage = 0.0;
    else if (usage > 100.0)
        usage = 100.0;

    memcpy(last_cpu, cur, sizeof(cur));
    return usage;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CPU — temperature
 *
 * Mirrors the FreeBSD tier order: the CPU's own on-die sensor first
 * (dev.cpu.0.temperature there, a known CPU hwmon driver here), the ACPI
 * thermal zone second (hw.acpi.thermal.tz0.temperature there,
 * /sys/class/thermal here), -1 third.
 * ═══════════════════════════════════════════════════════════════════════════ */

#define HWMON_CORETEMP 1
#define HWMON_K10TEMP  2

/* Function purpose: Read a millidegree-Celsius attribute as whole degrees. */
static int read_millidegrees(const char *dir, const char *leaf, int *out)
{
    long long md;

    if (!read_attr_ll(dir, leaf, &md))
        return 0;

    /* Action purpose: both sources this function is used for are documented
     * "Unit: millidegree Celsius" -- sysfs-class-hwmon for tempY_input and
     * sysfs-class-thermal for thermal_zoneX/temp -- while the contract, and
     * the caller's "%d°C", are whole degrees Celsius. The division truncates,
     * and that is the deliberate choice rather than rounding: FreeBSD's
     * (temp - 2732) / 10 truncates too, so the two platforms round the same
     * way. On coretemp nothing is lost at all -- the kernel driver doc gives
     * the hardware resolution as 1 degree C, so the three trailing digits
     * carry no information. */
    *out = (int)(md / 1000);
    return 1;
}

/* Function purpose: Pick the right temperature channel of an already
 * identified CPU hwmon device and read it. */
static int hwmon_read_cpu_channel(const char *dir, int kind, int *out)
{
    DIR *d = opendir(dir);
    if (d == NULL)
        return 0;

    unsigned lowest = 0, package = 0;
    int have_lowest = 0, have_package = 0, have_temp2 = 0;

    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        unsigned n;
        int      pos = -1;

        /* %n is assigned only once the literal "_input" has matched, and it
         * does not count toward the return value -- so this accepts
         * "temp1_input" and rejects "temp1_label", which a plain
         * "temp%u_input" conversion would not. */
        if (sscanf(e->d_name, "temp%u_input%n", &n, &pos) != 1)
            continue;
        if (pos < 0 || e->d_name[pos] != '\0')
            continue;

        if (!have_lowest || n < lowest) {
            lowest      = n;
            have_lowest = 1;
        }
        if (n == 2)
            have_temp2 = 1;

        /* Action purpose: on Intel the package sensor is identified by its
         * label, not by its number. The coretemp driver doc states that
         * tempX_label "Contains string "Core X" ... For Package temp, this
         * will be "Package id Y", where Y is the package number", and that
         * "All sysfs entries are named with their core_id" -- so the package
         * channel is wherever the core numbering leaves it and must not be
         * assumed to be temp1. The match is a prefix because Y varies, and
         * the lowest qualifying channel wins so that a dual-socket machine
         * picks package 0 deterministically rather than by readdir() order. */
        if (kind == HWMON_CORETEMP && (!have_package || n < package)) {
            char leaf[32], label[64];

            snprintf(leaf, sizeof(leaf), "temp%u_label", n);
            if (read_attr(dir, leaf, label, sizeof(label)) &&
                strncmp(label, "Package id", 10) == 0) {
                package      = n;
                have_package = 1;
            }
        }
    }
    closedir(d);

    unsigned pick;
    if (kind == HWMON_CORETEMP && have_package)
        pick = package;
    /* Action purpose: on AMD, temp2_input before temp1_input. The k10temp doc
     * is explicit that "Tctl is always available as temp1_input" while "the
     * driver exports Tdie temperature as temp2_input for those CPUs which
     * support it", and that Tctl "is a non-physical temperature on an
     * arbitrary scale" that "does _not_ represent an actual physical
     * temperature". Taking the lowest-numbered channel because it is lowest
     * would print Tctl with a degree sign on pre-Family-17h parts. On Family
     * 17h and later the doc says the driver "aims to compensate and report the
     * real temperature", so falling back to temp1_input is correct there and
     * merely least-bad on the older parts. The Tccd channels (temp3..temp10)
     * are ignored: there can be eight of them and the bar shows one number. */
    else if (kind == HWMON_K10TEMP && have_temp2)
        pick = 2;
    else if (have_lowest)
        pick = lowest;
    else
        return 0;

    char leaf[32];
    snprintf(leaf, sizeof(leaf), "temp%u_input", pick);
    return read_millidegrees(dir, leaf, out);
}

/* Function purpose: Tier 1 -- the CPU's own digital sensor, via hwmon. */
static int hwmon_cpu_temp(int *out)
{
    DIR *d = opendir(HWMON_DIR);
    if (d == NULL)
        return 0;

    char          best[SYSNAME_MAX] = "";
    unsigned long best_index        = 0;
    int           best_kind         = 0;

    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        unsigned long index;
        if (!parse_indexed(e->d_name, "hwmon", &index))
            continue;
        if (best_kind != 0 && index >= best_index)
            continue;

        char dir[SYSPATH_MAX], name[64];
        if (!join_path(dir, sizeof(dir), HWMON_DIR, e->d_name))
            continue;
        if (!read_attr(dir, "name", name, sizeof(name)))
            continue;

        /* Action purpose: an explicit list of CPU driver names, matched
         * exactly, rather than a scan for anything that looks like a plausible
         * temperature. Two documented reasons. First, the hwmon interface doc
         * says that for chips reading an external thermistor through an ADC
         * "all temp* files described above should contain values expressed in
         * millivolt instead of millidegree Celsius" -- a scan can pick up a
         * channel whose 45000 means 45 mV. Second, the thermal sysfs doc says
         * the generic thermal driver "also creates a hwmon sysfs I/F for each
         * _type_ of thermal zone device", so the ACPI zones appear under
         * /sys/class/hwmon too; an unfiltered scan could reach acpitz through
         * tier 1 and silently invert the tier order FreeBSD has.
         *
         * "name" is safe as the key: the hwmon ABI calls it "the only
         * mandatory attribute" and constrains it to "a short, lowercase
         * string, not containing whitespace, dashes, or the wildcard
         * character", so an exact strcmp needs no case folding or trimming
         * beyond the newline. "coretemp" and "k10temp" are the two driver
         * prefixes the kernel's own driver docs give. The list is a list:
         * ARM SoCs, Apple silicon and out-of-tree zenpower use other names and
         * will fall through to tier 2, which is correct behaviour.
         *
         * hwmonX numbering is allocation order, not identity, so hwmon0 is
         * never hard-coded; the lowest matching index simply makes the choice
         * deterministic across readdir() orders. */
        int kind = 0;
        if (strcmp(name, "coretemp") == 0)
            kind = HWMON_CORETEMP;
        else if (strcmp(name, "k10temp") == 0)
            kind = HWMON_K10TEMP;

        if (kind != 0) {
            best_kind  = kind;
            best_index = index;
            snprintf(best, sizeof(best), "%s", e->d_name);
        }
    }
    closedir(d);

    if (best_kind == 0)
        return 0;

    char dir[SYSPATH_MAX];
    if (!join_path(dir, sizeof(dir), HWMON_DIR, best))
        return 0;

    return hwmon_read_cpu_channel(dir, best_kind, out);
}

/* Function purpose: Tier 2 -- the ACPI thermal zone, matching FreeBSD's
 * hw.acpi.thermal.tz0.temperature fallback. */
static int thermal_cpu_temp(int *out)
{
    DIR *d = opendir(THERMAL_DIR);
    if (d == NULL)
        return 0;

    unsigned long acpitz = 0, highest = 0;
    int           have_acpitz = 0, have_any = 0;

    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        unsigned long index;
        if (!parse_indexed(e->d_name, "thermal_zone", &index))
            continue;

        if (!have_any || index > highest)
            highest = index;
        have_any = 1;

        if (have_acpitz && index >= acpitz)
            continue;

        char dir[SYSPATH_MAX], type[64];
        if (!join_path(dir, sizeof(dir), THERMAL_DIR, e->d_name))
            continue;

        /* Action purpose: prefer type == "acpitz". The thermal class ABI
         * enumerates no set of type values -- it says the string "is given by
         * thermal zone driver as part of registration" and offers exactly one
         * example, "acpitz", which is also the value in the thermal sysfs
         * doc's worked example. Preferring the one documented value is as far
         * as the documentation goes; any longer vocabulary would be invented.
         * x86_pkg_temp is a plausible second candidate and is deliberately NOT
         * matched here, because it is unverified -- see
         * .devdocs/research/sysfs.md 6.4. */
        if (read_attr(dir, "type", type, sizeof(type)) &&
            strcmp(type, "acpitz") == 0) {
            acpitz      = index;
            have_acpitz = 1;
        }
    }
    closedir(d);

    char dir[SYSPATH_MAX], leaf[48];

    if (have_acpitz) {
        snprintf(leaf, sizeof(leaf), "thermal_zone%lu", acpitz);
        if (join_path(dir, sizeof(dir), THERMAL_DIR, leaf) &&
            read_millidegrees(dir, "temp", out))
            return 1;
    }

    if (!have_any)
        return 0;

    /* Action purpose: with no acpitz zone, take the lowest-numbered zone whose
     * temp actually reads. That is the same guess FreeBSD makes with its
     * hard-coded tz0, and ascending order keeps it stable: readdir() order is
     * filesystem-dependent, and an unstable choice between two zones would
     * make the bar flicker between two temperatures. Zones are probed in order
     * rather than all read up front so that the common case costs one open. */
    for (unsigned long i = 0; i <= highest; i++) {
        snprintf(leaf, sizeof(leaf), "thermal_zone%lu", i);
        if (join_path(dir, sizeof(dir), THERMAL_DIR, leaf) &&
            read_millidegrees(dir, "temp", out))
            return 1;
    }

    return 0;
}

int get_cpu_temp(void)
{
    int celsius;

    if (hwmon_cpu_temp(&celsius))
        return celsius;
    if (thermal_cpu_temp(&celsius))
        return celsius;

    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MEMORY — usage
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Function purpose: Look up one "Key:  value" line in a /proc/meminfo image.
 * Keys are matched by name, never by line number: proc_meminfo(5) says "Some
 * fields are displayed only if the kernel was configured with various
 * options", so the set present and therefore the line numbering vary. */
static int meminfo_field(const char *buf, const char *key, unsigned long long *out)
{
    size_t klen = strlen(key);

    for (const char *line = buf; line != NULL; ) {
        const char *nl = strchr(line, '\n');
        if (nl == NULL)
            break;

        if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
            const char *v = line + klen + 1;
            char       *end;
            unsigned long long value = strtoull(v, &end, 10);
            if (end == v)
                return 0;
            *out = value;
            return 1;
        }

        line = nl + 1;
    }

    return 0;
}

double get_mem_usage(void)
{
    char buf[8192];

    if (!read_sysfile("/proc/meminfo", buf, sizeof(buf)))
        return 0.0;

    unsigned long long total, avail;

    if (!meminfo_field(buf, "MemTotal", &total) || total == 0)
        return 0.0;

    /* Action purpose: MemTotal - MemAvailable, not MemTotal - MemFree. This
     * reproduces the INTENT of FreeBSD's (active + wire) / physmem -- a figure
     * that tracks real memory pressure and does not climb to ~100% merely
     * because the kernel has filled RAM with reclaimable cache -- and NOT its
     * value. The two classify memory on different axes: FreeBSD's active is an
     * LRU-queue position, while the kernel doc describes MemAvailable as "An
     * estimate of how much memory is available for starting new applications,
     * without swapping", calculated from the file LRU lists and per-zone
     * watermarks. The numbers will differ, sometimes by tens of points on the
     * same workload, and on a ZFS FreeBSD box dramatically so. That is
     * expected; .devdocs/research/proc.md 3.3 enumerates the four divergences.
     *
     * No unit conversion and no page-size lookup: both operands come from the
     * same file carrying the same suffix, so whatever "kB" means there -- and
     * proc_meminfo(5) does not define it -- it cancels in the ratio. */
    if (!meminfo_field(buf, "MemAvailable", &avail)) {
        /* Action purpose: MemAvailable is documented "(since Linux 3.14)", so
         * on an older kernel the key is simply absent. The stand-in is built
         * from the inputs the kernel doc names for the estimate itself. It is
         * strictly worse -- it credits all of Cached as available, including
         * the Shmem part that cannot be reclaimed without swap, and it applies
         * none of the watermark corrections -- so it is reached only when the
         * key is missing, never preferred. */
        unsigned long long free_kb = 0, buffers = 0, cached = 0, sreclaim = 0;

        if (!meminfo_field(buf, "MemFree", &free_kb))
            return 0.0;

        meminfo_field(buf, "Buffers", &buffers);
        meminfo_field(buf, "Cached", &cached);
        meminfo_field(buf, "SReclaimable", &sreclaim);

        avail = free_kb + buffers + cached + sreclaim;
    }

    /* Action purpose: an unsigned subtraction, so a sum that exceeds MemTotal
     * -- conceivable in the fallback above, where four independently reported
     * fields are added -- must not be allowed to wrap into an enormous
     * percentage. 0.0 is the value this function already returns when it
     * cannot answer, and the caller prints the block either way. */
    if (avail >= total)
        return 0.0;

    return 100.0 * (double)(total - avail) / (double)total;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BATTERY — life percent, charge state, and external power
 *
 * FreeBSD reads three ACPI sysctls and the kernel has already consolidated
 * every connected battery into them. Linux does not consolidate: each supply
 * is its own directory under /sys/class/power_supply, so the aggregation
 * policy is this file's to make, and it is spelled out below.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Accumulator over the batteries that passed the candidate test. */
struct bat_acc {
    long long sum;                 /* capacities, percent                  */
    int       count;
    int       rank;                /* status rank of the chosen entry, -1 none */
    char      name[SYSNAME_MAX];   /* its device name, for the tie-break   */
    char      status[32];          /* its raw status string                */
};

/* Function purpose: Rank a raw sysfs status string so that one battery's state
 * can stand for the pack. Charging beats Discharging beats everything else,
 * which mirrors how FreeBSD's kernel-side consolidation reports a pack with
 * any unit charging as charging. */
static int bat_status_rank(const char *status)
{
    if (strcmp(status, "Charging") == 0)
        return 2;
    if (strcmp(status, "Discharging") == 0)
        return 1;
    return 0;
}

/* Function purpose: Fold one qualifying battery into an accumulator. */
static void bat_accumulate(struct bat_acc *a, const char *name,
                           long long capacity, const char *status)
{
    a->sum += capacity;
    a->count++;

    /* Action purpose: ties break on the lexicographically smallest device
     * name, not on arrival. readdir() order is filesystem-dependent, and a bar
     * that picked a different battery's state from one tick to the next would
     * change colour at random on a two-battery machine. */
    int rank = bat_status_rank(status);
    if (rank > a->rank || (rank == a->rank && strcmp(name, a->name) < 0)) {
        a->rank = rank;
        snprintf(a->name, sizeof(a->name), "%s", name);
        snprintf(a->status, sizeof(a->status), "%s", status);
    }
}

/* Function purpose: Recognise the ACPI battery naming convention. */
static int is_acpi_battery_name(const char *name)
{
    if (strncmp(name, "BAT", 3) == 0 && name[3] >= '0' && name[3] <= '9')
        return 1;
    if (strncmp(name, "CMB", 3) == 0)
        return 1;
    return 0;
}

void get_bat_info(int *life, char *state, int *external)
{
    /* All three outputs are written on every path, including the one where
     * the directory does not exist at all. */
    *life     = -1;
    *external = 0;
    strcpy(state, "Unknown");

    struct bat_acc acpi = { 0, 0, -1, "", "" };
    int saw_mains = 0, mains_online = 0, inferred = 0;

    DIR *d = opendir(POWER_DIR);
    if (d != NULL) {
        const struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.')
                continue;

            char dir[SYSPATH_MAX], type[32];
            if (!join_path(dir, sizeof(dir), POWER_DIR, e->d_name))
                continue;
            if (!read_attr(dir, "type", type, sizeof(type)))
                continue;

            if (strcmp(type, "Mains") == 0) {
                long long online;
                if (read_attr_ll(dir, "online", &online)) {
                    saw_mains = 1;
                    /* Action purpose: "online" is tri-valued, not boolean. The
                     * power supply ABI documents 0 Offline, 1 "Online Fixed"
                     * and 2 "Online Programmable", so a == 1 test reports a
                     * USB-PD PPS supply as unplugged. This is also the
                     * spelling the FreeBSD side already uses for
                     * hw.acpi.acline (acline != 0), so the portable test and
                     * the native one agree and neither platform needs a
                     * branch.
                     *
                     * Any online Mains wins: a laptop can expose both a barrel
                     * jack and a USB-C Mains, and being plugged into either is
                     * being plugged in. USB-typed supplies are deliberately
                     * not treated as mains -- "USB" is its own documented type
                     * value, and an online USB supply may be a phone charging
                     * FROM the laptop. */
                    if (online != 0)
                        mains_online = 1;
                }
                continue;
            }

            if (strcmp(type, "Battery") != 0)
                continue;

            /* Action purpose: a MISSING "present" file means present. The ABI
             * says so in as many words -- "If the property does not exist, the
             * battery is considered to be present" -- and getting it backwards
             * silently hides the battery on every machine whose driver omits
             * the file. Only an existing file reading 0 means absent, which is
             * this platform's spelling of FreeBSD's
             * ACPI_BATT_STAT_NOT_PRESENT and produces the same visible result:
             * no candidate, life -1, block omitted. */
            long long present;
            if (read_attr_ll(dir, "present", &present) && present == 0)
                continue;

            /* Action purpose: "capacity" is the drop-in for
             * hw.acpi.battery.life -- the ABI gives it as "Valid values: 0 -
             * 100 (percent)", the same unit and range, so no arithmetic and no
             * second file. charge_now/charge_full are deliberately not used
             * instead: the power supply class doc states that "Inferring not
             * available properties using some heuristics or mathematical model
             * is not a subject of work for a battery driver", and those
             * attributes are not in the reachable ABI for a generic supply at
             * all. A reading outside 0..100 is a driver bug and disqualifies
             * the entry rather than being clamped and shown. */
            long long capacity;
            if (!read_attr_ll(dir, "capacity", &capacity))
                continue;
            if (capacity < 0 || capacity > 100)
                continue;

            char status[32];
            if (!read_attr(dir, "status", status, sizeof(status)))
                status[0] = '\0';

            /* Action purpose: Linux registers non-system batteries in the
             * same class -- wireless mice, keyboards and game controllers all
             * show up with type "Battery" -- and averaging a mouse into the
             * laptop reading would be wrong. The kernel has a "scope" property
             * (System vs Device) meant for exactly this, but it is NOT in
             * either reachable ABI file (see .devdocs/research/sysfs.md 6.1),
             * and this file does not key on an interface it cannot cite. So
             * only entries whose directory name follows the ACPI convention
             * are counted. This is a naming heuristic, not an ABI guarantee,
             * and is recorded as one.
             *
             * There is deliberately NO fallback to "every type == Battery
             * entry" when no ACPI-named supply exists. Such a fallback reads
             * plausible and is a sentinel-parity bug: on a desktop with no
             * system battery but a wireless mouse attached, it reports the
             * mouse's charge as the machine's, where FreeBSD's
             * hw.acpi.battery.life simply fails and leaves the -1 that
             * suppresses the block. Matching that -1 is the whole contract
             * this file is written against. The cost is a battery block
             * missing on hardware that names its supply something other than
             * BAT<n>/CMB*; the alternative is a wrong number on ordinary
             * desktops, which is worse and far more common. */
            if (is_acpi_battery_name(e->d_name))
                bat_accumulate(&acpi, e->d_name, capacity, status);
        }
        closedir(d);
    }

    const struct bat_acc *pick = &acpi;

    if (pick->count > 0) {
        /* Action purpose: the unweighted mean, truncated. capacity is a
         * percentage, so percentages of different-sized cells are not
         * additive and a sum would be meaningless; a max would hide a flat
         * second cell. The mean is also wrong when the cells differ in size,
         * but it is wrong by a bounded amount and it is the only aggregation
         * that stays inside 0..100 using the one attribute the ABI documents.
         * A charge-weighted mean would be better and is not available:
         * charge_full / energy_full are not in the reachable ABI. The single
         * battery case -- overwhelmingly the common one -- takes this path
         * with no averaging at all. */
        *life = (int)(pick->sum / pick->count);

        /* Action purpose: the five sysfs status strings are mapped onto the
         * five labels the header pins, and `inferred` is set only by the two
         * that POSITIVELY mean external power. This is the same safety
         * argument the FreeBSD file makes about its ACPI flags: a state that
         * does not prove mains must not paint a draining battery as plugged
         * in. "Not charging" is the plugged-in-but-charge-inhibited state and
         * is a distinct ABI value from "Discharging"; it maps onto FreeBSD's
         * "AC" branch, which likewise does not set `inferred`. Anything
         * unreadable, or any string outside the documented set, becomes
         * "Unknown" -- the label set stays closed at five, and the longest of
         * them is 12 bytes against the caller's 32-byte field. */
        if (strcmp(pick->status, "Full") == 0) {
            strcpy(state, "Full");
            inferred = 1;
        } else if (strcmp(pick->status, "Charging") == 0) {
            strcpy(state, "Charging");
            inferred = 1;
        } else if (strcmp(pick->status, "Discharging") == 0) {
            strcpy(state, "Discharging");
        } else if (strcmp(pick->status, "Not charging") == 0) {
            strcpy(state, "AC");
        }
    }

    /* Action purpose: a Mains supply whose "online" could be read is the
     * AUTHORITY on external power and overrides the inference, exactly as
     * hw.acpi.acline overrides it on FreeBSD -- it reports the input rail
     * itself rather than being deduced from what the battery is doing. The
     * fallback is kept, not replaced: a desktop or VM with no Mains entry, or
     * one whose online attribute cannot be read, is the same situation as a
     * FreeBSD machine with no hw.acpi.acline, and there the positive-only
     * inference is still the best answer available. It errs toward "on
     * battery", which the header makes a requirement and which is the safe
     * direction. */
    *external = saw_mains ? mains_online : inferred;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BACKLIGHT — current brightness as a percentage
 *
 * The contract is the percentage. FreeBSD's backlight(8) already reports one,
 * so that implementation does no arithmetic; sysfs reports a raw driver-scale
 * value whose ceiling is max_brightness in the same directory, so the
 * conversion happens here rather than in the caller.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Function purpose: Rank a backlight interface type, highest preferred. */
static int backlight_rank(const char *type)
{
    /* Action purpose: the ABI states this policy itself, verbatim: "In the
     * general case, when multiple backlight interfaces are available for a
     * single device, firmware control should be preferred to platform control
     * should be preferred to raw control", because "Using a firmware interface
     * reduces the probability of confusion with the hardware and the OS
     * independently updating the backlight state" -- which is precisely the
     * failure a status bar would show, two interfaces disagreeing and the
     * percentage jumping. Three values exist and no more; a device whose type
     * cannot be read ranks below all three rather than being discarded, so a
     * machine with one such backlight still gets a reading. */
    if (strcmp(type, "firmware") == 0)
        return 3;
    if (strcmp(type, "platform") == 0)
        return 2;
    if (strcmp(type, "raw") == 0)
        return 1;
    return 0;
}

int get_backlight(void)
{
    DIR *d = opendir(BACKLIGHT_DIR);
    if (d == NULL)
        return -1;

    char best[SYSNAME_MAX] = "";
    int  best_rank         = -1;

    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;

        char dir[SYSPATH_MAX], type[32];
        if (!join_path(dir, sizeof(dir), BACKLIGHT_DIR, e->d_name))
            continue;

        int rank = 0;
        if (read_attr(dir, "type", type, sizeof(type)))
            rank = backlight_rank(type);

        /* Within a tier the lowest name by strcmp wins. readdir() order is not
         * stable, and an unstable choice between two backlights would make the
         * bar flicker between two percentages. */
        if (rank > best_rank || (rank == best_rank && strcmp(e->d_name, best) < 0)) {
            best_rank = rank;
            snprintf(best, sizeof(best), "%s", e->d_name);
        }
    }
    closedir(d);

    if (best_rank < 0)
        return -1;

    char dir[SYSPATH_MAX];
    if (!join_path(dir, sizeof(dir), BACKLIGHT_DIR, best))
        return -1;

    /* Action purpose: "brightness", not "actual_brightness", although the
     * latter sounds more truthful. The stable ABI says outright "Userspace
     * should generally use the values in 'brightness' to make decisions", and
     * gives the reason: actual_brightness queries the hardware, where "some
     * hardware may treat blanking differently or have custom power saving
     * features". A bar reading actual_brightness would show 0% whenever the
     * panel blanks on idle. brightness is the user's setting, and the setting
     * is what this block reports. */
    long long current, maximum;
    if (!read_attr_ll(dir, "brightness", &current))
        return -1;
    if (!read_attr_ll(dir, "max_brightness", &maximum))
        return -1;

    /* Action purpose: max_brightness is device-specific and the ABI puts no
     * bound on it -- real hardware ranges from about 15 to 100000 -- so the
     * percentage does not exist until it is computed here. A non-positive
     * maximum would be a division by zero or a negative scale, and an
     * unreadable one is indistinguishable from an absent backlight: both are
     * the same unavailable sentinel the FreeBSD side returns when its pipeline
     * matches nothing. Integer arithmetic in this order avoids a float
     * dependency and the precision loss of dividing first; the multiply cannot
     * plausibly overflow a long long for any real max_brightness. */
    if (maximum <= 0 || current < 0)
        return -1;

    return (int)((current * 100) / maximum);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GPU — AMD telemetry from the amdgpu driver's sysfs attributes
 *
 * The one sensor here with no FreeBSD counterpart worth the name. It exists
 * because src/topbar.c's only GPU source is nvidia-smi, so an AMD machine
 * shows no GPU blocks at all; everything below is read with the same open and
 * read this file already uses for /proc and /sys, so it costs no fork, no
 * shell and no new library -- which is precisely why it belongs here and not
 * beside the nvidia-smi helper in the shared file.
 *
 * Four attributes, all on the card's PCI device, all quoted from the kernel:
 *
 *   gpu_busy_percent      "The amdgpu driver provides a sysfs API for reading
 *                         how busy the GPU is as a percentage. The file
 *                         gpu_busy_percent is used for this."
 *                         (drivers/gpu/drm/amd/pm/amdgpu_pm.c, DOC:
 *                         gpu_busy_percent)
 *   mem_info_vram_total   "returns the total amount of VRAM in bytes"
 *   mem_info_vram_used    "returns the total amount of currently used VRAM in
 *                         bytes"
 *                         (drivers/gpu/drm/amd/amdgpu/amdgpu_vram_mgr.c)
 *   hwmon tempN_input     "temp[1-3]_input: the on die GPU temperature in
 *                         millidegrees Celsius"
 *                         (drivers/gpu/drm/amd/pm/amdgpu_pm.c, DOC: hwmon)
 *
 * BYTES, not MiB: the two VRAM attributes are documented in bytes and the
 * contract is MiB, so the division happens in amd_read_card() below.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Function purpose: Test whether the driver bound to the device directory
 * `dir` is `driver`, by reading the DRIVER= line out of its uevent file.
 *
 * Action purpose: uevent and not the card number, and uevent rather than the
 * basename of the `driver` symlink beside it. Both are written by the driver
 * core from the same string -- drivers/base/core.c adds
 * add_uevent_var(env, "DRIVER=%s", drv->name) under the comment "Add
 * "DRIVER=%s" variable if the device is bound to a driver", and
 * drivers/base/dd.c builds the symlink with sysfs_create_link(&dev->kobj,
 * &dev->driver->p->kobj, "driver") -- so they agree, and both are absent when
 * nothing is bound. The uevent file is chosen because it is an ordinary file:
 * it goes through read_sysfile() like every other read in this translation
 * unit, where the symlink would need readlink(2) and its own truncation
 * handling for no extra information. The card index is NOT usable for this:
 * drm_sysfs_minor_alloc() names minors "card%d" from an allocation counter
 * with no relation to the driver, so card0 is whichever GPU probed first. */
static int uevent_driver_is(const char *dir, const char *driver)
{
    char path[SYSPATH_MAX];
    char buf[4096];

    if (!join_path(path, sizeof(path), dir, "uevent"))
        return 0;
    if (!read_sysfile(path, buf, sizeof(buf)))
        return 0;

    size_t dlen = strlen(driver);

    for (char *line = buf; line != NULL; ) {
        char *nl = strchr(line, '\n');

        /* uevent_show() emits every variable with a trailing newline, so an
         * unterminated tail is a truncated read and not a line. */
        if (nl == NULL)
            break;
        *nl = '\0';

        /* The whole value must match, not a prefix: "amdgpu" and a
         * hypothetical "amdgpu_foo" are different drivers. */
        if (strncmp(line, "DRIVER=", 7) == 0 &&
            strncmp(line + 7, driver, dlen) == 0 && line[7 + dlen] == '\0')
            return 1;

        line = nl + 1;
    }

    return 0;
}

/* Function purpose: Pick the right temperature channel of one amdgpu hwmon
 * node and read it as whole degrees Celsius. */
static int amd_hwmon_channel(const char *dir, int *out)
{
    DIR *d = opendir(dir);
    if (d == NULL)
        return 0;

    unsigned lowest = 0, edge = 0;
    int have_lowest = 0, have_edge = 0;

    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        unsigned n;
        int      pos = -1;

        /* The same exact-match idiom hwmon_read_cpu_channel() uses: %n is
         * assigned only once "_input" has matched and does not count toward
         * the return value, so "temp1_input" is accepted and "temp1_label"
         * is not. */
        if (sscanf(e->d_name, "temp%u_input%n", &n, &pos) != 1)
            continue;
        if (pos < 0 || e->d_name[pos] != '\0')
            continue;

        if (!have_lowest || n < lowest) {
            lowest      = n;
            have_lowest = 1;
        }

        /* Action purpose: prefer the channel LABELLED "edge", by label and
         * not by number. amdgpu's three channels are fixed in the driver --
         * temp_label[] = {PP_TEMP_EDGE, "edge"}, {PP_TEMP_JUNCTION,
         * "junction"}, {PP_TEMP_MEM, "mem"} -- and "edge" is the die
         * temperature that corresponds to what nvidia-smi's
         * temperature.gpu reports, so the two vendors put the same quantity
         * in the same block. The number cannot be assumed: the driver's
         * hwmon_attributes_visible() hides temp1_input entirely on multi-AID
         * parts, under the comment "hotspot temperature for gc 9,4,3", which
         * leaves temp2 (junction) as the lowest channel on that hardware.
         * Falling back to the lowest-numbered channel is therefore a
         * fallback and not the rule, and the lowest is taken rather than the
         * first so that readdir() order cannot make the bar alternate
         * between two sensors. */
        if (!have_edge || n < edge) {
            char leaf[32], label[64];

            snprintf(leaf, sizeof(leaf), "temp%u_label", n);
            if (read_attr(dir, leaf, label, sizeof(label)) &&
                strcmp(label, "edge") == 0) {
                edge      = n;
                have_edge = 1;
            }
        }
    }
    closedir(d);

    unsigned pick;
    if (have_edge)
        pick = edge;
    else if (have_lowest)
        pick = lowest;
    else
        return 0;

    char leaf[32];
    snprintf(leaf, sizeof(leaf), "temp%u_input", pick);

    /* Already millidegrees here, exactly as for the CPU sensors: the amdgpu
     * hwmon doc and the hwmon class ABI give tempY_input the same unit. */
    return read_millidegrees(dir, leaf, out);
}

/* Function purpose: Read the die temperature from the hwmon node amdgpu
 * registers beneath the card's PCI device.
 *
 * Action purpose: the node is looked up UNDER the device rather than by
 * scanning /sys/class/hwmon for name == "amdgpu", which would be ambiguous on
 * a two-AMD-card machine and would need a second walk to tie a node back to
 * its card. amdgpu registers it with hwmon_device_register_with_groups(
 * adev->dev, DRIVER_NAME, ...), i.e. parented to the PCI device, and the
 * driver core files a class device with a non-class parent inside a glue
 * directory named after the class -- kobject_add(&dir->kobj, parent_kobj,
 * "%s", sp->class->name) in drivers/base/core.c, under the comment
 * "Class-devices with a non class-device as parent, live in a "glue"
 * directory to prevent namespace collisions". So the node is always at
 * <device>/hwmon/hwmon<i>, and anything found there belongs to this card by
 * construction; no name check is needed and none is made. The lowest index
 * that yields a reading wins, for the determinism reason above. */
static int amd_hwmon_temp(const char *devdir, int *out)
{
    char hwdir[SYSPATH_MAX];

    if (!join_path(hwdir, sizeof(hwdir), devdir, "hwmon"))
        return 0;

    DIR *d = opendir(hwdir);
    if (d == NULL)
        return 0;

    unsigned long best_index = 0;
    int           best_temp  = 0;
    int           have_best  = 0;

    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        unsigned long index;
        if (!parse_indexed(e->d_name, "hwmon", &index))
            continue;
        if (have_best && index >= best_index)
            continue;

        char dir[SYSPATH_MAX];
        if (!join_path(dir, sizeof(dir), hwdir, e->d_name))
            continue;

        int celsius;
        if (!amd_hwmon_channel(dir, &celsius))
            continue;

        best_index = index;
        best_temp  = celsius;
        have_best  = 1;
    }
    closedir(d);

    if (!have_best)
        return 0;

    *out = best_temp;
    return 1;
}

/* Function purpose: Take a complete reading from one amdgpu card, given its
 * PCI device directory. Writes nothing and returns 0 unless all four values
 * are present and sane, which is the header's all-or-nothing contract. */
static int amd_read_card(const char *devdir, float *util, long *vram_used,
                         long *vram_total, int *temp)
{
    long long busy, used, total;
    int       celsius;

    /* Action purpose: gpu_busy_percent may be missing on a perfectly working
     * card and that is not an error to report. amdgpu_pm_sysfs_init() returns
     * before creating any of its attributes when "adev->pm.dpm_enabled == 0",
     * and every entry in its table is additionally gated on an attribute mask
     * that is zeroed under multi-VF SRIOV -- so on such a card the file
     * simply does not exist. The read failing is also how a card whose
     * firmware cannot answer presents itself: amdgpu_get_gpu_busy_percent()
     * returns the sensor's error code, read(2) fails, and read_sysfile()
     * reports the same 0 as an absent file. Both end here, with the GPU
     * blocks suppressed rather than a fabricated zero shown. */
    if (!read_attr_ll(devdir, "gpu_busy_percent", &busy))
        return 0;

    /* Action purpose: the kernel-doc says "a percentage" and pins no range,
     * so the range is enforced here rather than assumed. A value outside
     * 0..100 disqualifies the card instead of being clamped, which is the
     * same judgement get_bat_info() makes about an out-of-range capacity:
     * a driver reporting nonsense should cost the block, not be dressed up
     * as a plausible number. */
    if (busy < 0 || busy > 100)
        return 0;

    if (!read_attr_ll(devdir, "mem_info_vram_used", &used))
        return 0;
    if (!read_attr_ll(devdir, "mem_info_vram_total", &total))
        return 0;

    /* A zero total would render " 0/0 MiB" and would divide by nothing
     * meaningful if the block ever became a percentage; a negative byte count
     * is a driver bug. Either disqualifies the card. */
    if (used < 0 || total <= 0)
        return 0;

    if (!amd_hwmon_temp(devdir, &celsius))
        return 0;

    *util = (float)busy;

    /* Action purpose: bytes to MiB, truncating. Both attributes are
     * documented in bytes and the caller's field is MiB, so the shift happens
     * exactly once, here. Truncation matches read_millidegrees() above and
     * matches nvidia-smi, whose memory.used/memory.total are already whole
     * MiB. long long throughout: a 192 GiB card overflows a 32-bit byte
     * count, while the MiB result fits a long on either word size. */
    *vram_used  = (long)(used  / (1024LL * 1024));
    *vram_total = (long)(total / (1024LL * 1024));
    *temp       = celsius;

    return 1;
}

int get_amd_gpu_info(float *util, long *vram_used, long *vram_total, int *temp)
{
    DIR *d = opendir(DRM_DIR);
    if (d == NULL)
        return 0;

    unsigned long best_index = 0;
    int           have_best  = 0;
    float         b_util     = 0.0f;
    long          b_used     = 0;
    long          b_total    = 0;
    int           b_temp     = 0;

    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        unsigned long index;

        /* Action purpose: an EXACT "card<n>" match, which is what keeps the
         * other inhabitants of this directory out. drm_sysfs_connector_add()
         * registers every connector in the same class with
         * dev_set_name(kdev, "card%d-%s", dev->primary->index,
         * connector->name), so /sys/class/drm also holds card0-DP-1 and
         * friends, and render nodes are named "renderD%d". parse_indexed()
         * demands end-of-string after the digits, so only the card minors
         * themselves survive this test. */
        if (!parse_indexed(e->d_name, "card", &index))
            continue;

        /* Action purpose: the selection rule, and it is the whole of it --
         * the LOWEST-numbered card that is an amdgpu AND answers completely.
         *
         * A hybrid machine really does expose several cards: each DRM device
         * gets its own card minor, so an APU plus a discrete GPU is card0 and
         * card1. Nothing in the DRM ABI says which of them is the discrete
         * one. boot_vga, VRAM size and the card number itself are all
         * correlates, not statements, and keying on one would be inventing an
         * interface -- so this does not try to. Lowest-numbered is chosen
         * because it is stable: readdir() order is filesystem-dependent and
         * a bar that switched cards between ticks would flicker between two
         * utilisation figures.
         *
         * "and answers completely" is doing real work, not padding the rule.
         * The common two-AMD-card machine is an APU plus a dGPU, and the APU
         * is usually the lower minor while being the one whose
         * gpu_busy_percent is absent; skipping a card that cannot fill all
         * four fields lands on the card that can, and still picks the lowest
         * whenever both answer. It costs nothing when there is one card.
         * This mirrors thermal_cpu_temp()'s "lowest zone whose temp actually
         * reads" rather than introducing a new policy. */
        if (have_best && index >= best_index)
            continue;

        char carddir[SYSPATH_MAX], devdir[SYSPATH_MAX];
        if (!join_path(carddir, sizeof(carddir), DRM_DIR, e->d_name))
            continue;
        if (!join_path(devdir, sizeof(devdir), carddir, "device"))
            continue;

        if (!uevent_driver_is(devdir, "amdgpu"))
            continue;

        float u;
        long  vu, vt;
        int   t;
        if (!amd_read_card(devdir, &u, &vu, &vt, &t))
            continue;

        best_index = index;
        have_best  = 1;
        b_util     = u;
        b_used     = vu;
        b_total    = vt;
        b_temp     = t;
    }
    closedir(d);

    /* Nothing is written unless a card answered in full -- the header
     * promises the caller's GPUInfo is untouched on a 0 return, exactly as
     * update_nvidia_info() leaves it untouched when nvidia-smi says nothing.
     * */
    if (!have_best)
        return 0;

    *util       = b_util;
    *vram_used  = b_used;
    *vram_total = b_total;
    *temp       = b_temp;

    return 1;
}
