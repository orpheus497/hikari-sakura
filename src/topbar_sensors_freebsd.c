/**
 * @file topbar_sensors_freebsd.c
 * @brief FreeBSD implementations of the hikari-topbar sensor contract.
 *
 * Script function and purpose: Supplies the five sensors declared in
 * include/hikari/topbar_sensors.h for FreeBSD -- CPU usage, CPU temperature,
 * memory usage, battery, and backlight. Each one was moved here verbatim from
 * src/topbar.c when the helper was split by platform (DECISIONS_LOG D-05,
 * .devdocs/research/contract.md); only the `static` keyword was dropped from
 * the five definitions so the shared translation unit can call them. The
 * snapshot get_cpu_usage() needs moved with it and stays private to this file.
 *
 * Everything that does not touch a FreeBSD kernel interface stayed in
 * src/topbar.c: the JSON escaping, the pywal palette, the swaybar emitter,
 * main(), the network and disk sensors, and the pactl / playerctl / nvidia-smi
 * helpers -- those three drive the same userland program on either platform,
 * so duplicating them here would only let the two copies drift.
 *
 * LIBRARIES
 *   stdio.h        popen / fscanf / pclose  (backlight(8) pipeline)
 *   string.h       strcpy / memcpy
 *   sys/types.h    size_t and friends
 *   sys/param.h    BSD system parameters and limits
 *   sys/sysctl.h   sysctlbyname  (FreeBSD kernel interface)
 *   sys/resource.h CPUSTATES / CP_IDLE
 *   sys/vmmeter.h  VM statistics declarations
 */

/* [COMMENT] Action purpose: No feature-test macro may be defined for this
target. FreeBSD's sys/cdefs.h clears __BSD_VISIBLE whenever _POSIX_C_SOURCE,
_XOPEN_SOURCE or _ANSI_SOURCE is defined, and the BSD-only declarations this
file depends on disappear with it. The Makefile defines none of them for
hikari-topbar and must not start; the same constraint, and the history behind
it, is documented at the top of src/topbar.c and at the hikari-topbar build
rule in the Makefile. The include order below is the order these headers had
in src/topbar.c, kept unchanged so the move cannot have perturbed it. */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/vmmeter.h>

#include <hikari/topbar_sensors.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * CPU — usage (delta between successive kern.cp_time snapshots)
 *
 * The previous snapshot is kept in file-scope so get_cpu_usage() can be
 * called repeatedly without passing state explicitly.  A warm-up call in
 * main() discards the first (meaningless) 100% reading.
 * ═══════════════════════════════════════════════════════════════════════════ */

static long last_cp_time[CPUSTATES];

double get_cpu_usage(void) {
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

int get_cpu_temp(void) {
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

double get_mem_usage(void) {
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

void get_bat_info(int *life, char *state, int *external) {
    size_t len = sizeof(*life);
    if (sysctlbyname("hw.acpi.battery.life", life, &len, NULL, 0) == -1)
        *life = -1;

    int s;
    int inferred = 0;
    len = sizeof(s);

    if (sysctlbyname("hw.acpi.battery.state", &s, &len, NULL, 0) == 0) {
        /* Action purpose: `inferred` is taken from the ACPI flags directly and
         * NOT from the label beside them, because the label is lossy. The final
         * `else` catches every flag combination that is neither charging nor
         * discharging and calls all of them "AC" -- which includes CRITICAL
         * (0x04) on its own. Treating that label as mains would paint a
         * critically flat battery in the plugged-in colour, so only the two
         * states that positively mean external power set the flag.
         *
         * DISCHARG (0x01) is deliberately tested BEFORE CHARGING (0x02), and
         * that order must not be swapped to make s == 3 read as "Charging".
         * FreeBSD names 0x03 ACPI_BATT_STAT_INVALID (sys/dev/acpica/acpiio.h)
         * and its driver does not correct it -- acpi_cmbat.c:294 merely logs
         * "battery reports simultaneous charging and discharging" and passes
         * the value through -- so 0x03 reaches us as a firmware bug, not as a
         * meaningful state. Deriving "on mains" from a reading the kernel calls
         * invalid is exactly the inference this function refuses to make
         * everywhere else, and it fails in the dangerous direction: a battery
         * that is really draining would be painted plugged-in. Testing
         * DISCHARG first resolves the impossible pair toward the safe reading.
         *
         * 0x07 is ACPI_BATT_STAT_NOT_PRESENT (no battery at all) and lands in
         * the same branch for the same reason. It costs nothing: with no
         * battery, hw.acpi.battery.life fails, *life stays -1, and the caller
         * omits the block entirely.
         *
         * None of this is load-bearing while hw.acpi.acline answers -- it
         * overrides `inferred` outright below. This is the fallback's fallback,
         * and it is written to be safe rather than clever. */
        if      (s == 0) { strcpy(state, "Full");        inferred = 1; }
        else if (s &  1)   strcpy(state, "Discharging");
        else if (s &  2) { strcpy(state, "Charging");    inferred = 1; }
        else               strcpy(state, "AC");
    } else {
        strcpy(state, "Unknown");
    }

    /* Action purpose: hw.acpi.acline is the AUTHORITY on external power and
     * overrides the inference above whenever it answers. It reports the AC line
     * itself -- 1 on mains, 0 on battery -- rather than being deduced from what
     * the battery is doing, and src/lock_config.c already reads the same sysctl
     * to settle the same question.
     *
     * This closes a gap the inference alone cannot: a machine plugged in with a
     * full battery whose firmware reports some flag combination other than 0
     * lands in the "AC" branch above, which deliberately does NOT set
     * `inferred` -- so it would have been coloured as though discharging. The
     * AC line knows better.
     *
     * The fallback is kept, not replaced. A machine with no ACPI power source
     * at all -- a desktop, or a VM -- has no such sysctl and the read fails;
     * there the positive-only inference is still the best answer available, and
     * it errs toward "on battery", which is the safe direction. */
    int acline;
    len = sizeof(acline);

    *external = (sysctlbyname("hw.acpi.acline", &acline, &len, NULL, 0) == 0)
                    ? (acline != 0)
                    : inferred;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BACKLIGHT — current brightness level
 * ═══════════════════════════════════════════════════════════════════════════ */

int get_backlight(void) {
    FILE *fp = popen(
        "backlight 2>/dev/null | grep brightness | awk '{print $2}'", "r");
    if (!fp) return -1;
    int level = -1;
    fscanf(fp, "%d", &level);
    pclose(fp);
    return level;
}
