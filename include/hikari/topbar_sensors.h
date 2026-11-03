// Script function and purpose: Platform sensor contract for the hikari-topbar
// helper. src/topbar.c keeps the JSON escaping, the swaybar emitter, the
// palette handling and the main loop; everything declared below is implemented
// once per platform, by src/topbar_sensors_freebsd.c or (when it exists)
// src/topbar_sensors_linux.c, and the Makefile selects one by ${OS}.
//
// These are the signatures src/topbar.c already used internally -- the split
// removed `static`, nothing more. There is deliberately no initialiser and no
// teardown: every function below is self-priming or stateless, which is what
// the single caller needs and no more. Sensors whose data source is the same
// program on both platforms -- nvidia-smi, playerctl, pactl -- are NOT here;
// they remain shared in src/topbar.c, as do get_net_status() and
// get_home_usage(), whose bodies are already portable.

#if !defined(HIKARI_TOPBAR_SENSORS_H)
#define HIKARI_TOPBAR_SENSORS_H

/* [COMMENT] Function purpose: CPU busy percentage since the previous call. Each
implementation keeps its own private snapshot of the previous reading, so the
first call after process start returns a meaningless value -- main() discards
one priming call before the loop. Returns 0.0 both when the machine is idle and
when the underlying counter cannot be read; the caller cannot distinguish them
and prints the block either way.

Two distinct 0.0 paths, and they are not interchangeable: when the underlying
counter cannot be read the snapshot is NOT updated, so the next successful call
measures across the whole gap; when no tick has elapsed the snapshot IS updated.
An implementation that collapses them changes recovery after a transient read
failure. */
double get_cpu_usage(void);

/* [COMMENT] Function purpose: CPU temperature in degrees Celsius, or -1 when no
sensor is readable. The caller suppresses the block for any negative value, not
only for -1. */
int get_cpu_temp(void);

/* [COMMENT] Function purpose: Committed memory as a percentage of physical
memory. Returns 0.0 when unavailable; the caller prints the block either way.
The two platforms do not compute the same number from the same machine, only
the same intent. */
double get_mem_usage(void);

/* [COMMENT] Function purpose: Battery charge, charge-state label, and whether
the machine is on external power. All three out-parameters are written on every
path; none is left untouched.

  life      charge in percent, or -1 when no battery is present; the caller
            suppresses the whole block on a negative value
  state     one of "Full", "Discharging", "Charging", "AC", "Unknown", and
            nothing else; at most 32 bytes including the terminator, which is
            the size of the caller's struct stats::bat_state. There is no size
            parameter because there is exactly one caller and it has a
            fixed-size field -- an implementation whose data source can produce
            a longer label must map it onto this closed set, not widen it.
  external  1 on external power, 0 on battery; never a sentinel. When no power
            source can be detected at all this must be 0, which is the safe
            direction: it paints a draining battery as draining. */
void get_bat_info(int *life, char *state, int *external);

/* [COMMENT] Function purpose: Backlight brightness as a percentage, 0-100, or
-1 when no backlight is readable. The caller suppresses the block on a negative
value. The percentage is the contract, not the raw reading: FreeBSD's
backlight(8) reports a percentage directly, so that implementation does no
arithmetic, while a platform whose interface exposes a raw driver scale must
convert here rather than in the caller. */
int get_backlight(void);

/* [COMMENT] Function purpose: AMD GPU telemetry, taken from the amdgpu
driver's own sysfs attributes. Returns 1 when ONE card answered all four
questions, in which case all four out-parameters have been written; returns 0
and leaves every out-parameter UNTOUCHED when no amdgpu card is present or any
single reading is missing.

  util        core utilisation in percent, 0-100
  vram_used   used VRAM in MiB
  vram_total  total VRAM in MiB, always greater than 0 on success
  temp        die temperature in whole degrees Celsius

The MiB unit is the contract, not the kernel's: amdgpu reports both VRAM
figures in bytes (drivers/gpu/drm/amd/amdgpu/amdgpu_vram_mgr.c documents
mem_info_vram_total as "the total amount of VRAM in bytes"), so the
implementation divides and the caller's field keeps the same unit nvidia-smi's
--format=csv,nounits already hands it. Converting in the caller would make the
unit depend on which GPU answered.

The all-or-nothing return is deliberate and is the NVIDIA contract, not a new
one: update_nvidia_info() in src/topbar.c sets GPUInfo::available to 0 whenever
its one line does not yield all four values, and the caller then suppresses all
three GPU blocks rather than rendering a partial set. There is no per-field
sentinel here because the caller has no per-field suppression to spend one on.

NVIDIA is deliberately NOT behind this function. nvidia-smi is the same program
on both platforms, so update_nvidia_info() stays shared in src/topbar.c under
the rule at the top of this file; amdgpu's attributes are a Linux sysfs
interface and so belong to the platform half. src/topbar_sensors_freebsd.c
implements this by returning 0 without reading anything -- FreeBSD has no
/sys/class/drm, and reporting unavailable leaves that platform's bar exactly as
it was. */
int get_amd_gpu_info(float *util, long *vram_used, long *vram_total, int *temp);

#endif
