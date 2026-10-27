// [COMMENT] Script function and purpose: Defaults and power-source resolution
// for the lock screen's appearance settings. Parsing lives with the rest of the
// configuration in src/configuration.c; this file owns the values themselves.

#include <hikari/lock_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// [COMMENT] Action purpose: sysctlbyname(3) is the FreeBSD interface for
// reading hw.acpi.acline. The guard exists so clangd-based IDE analysis on
// Linux still resolves the rest of the file, mirroring the guard src/lock_mode.c
// applies to explicit_bzero. Without a way to ask, the AC timeout is used --
// see hikari_lock_config_blank_timeout().
#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/types.h>
#define HIKARI_HAVE_ACPI_ACLINE 1
#endif

// [COMMENT] Action purpose: Linux has no hw.acpi.acline. The equivalent is the
// power supply class under /sys, which this file reads directly rather than
// through a library: the whole query is an opendir and two small reads, and
// anything that wrapped it would be larger than the thing it wrapped.
//
// This duplicates about twenty lines of src/topbar_sensors_linux.c on purpose.
// The two live in binaries that share no object code -- hikari-topbar links
// against libc alone, which is what lets it be built and tested without
// wlroots -- so sharing would mean introducing a translation unit into that
// link for the sake of one loop. The duplication is the smaller cost, and the
// two agree on semantics by the reasoning quoted at the test below.
#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#define HIKARI_HAVE_SYSFS_POWER_SUPPLY 1
#endif

#include <hikari/memory.h>

void
hikari_lock_config_init(struct hikari_lock_config *lock_config)
{
  lock_config->blur = true;
  lock_config->blur_radius = 12;
  lock_config->blur_passes = 3;

  lock_config->clock = true;
  lock_config->clock_format = hikari_malloc(strlen("%H:%M") + 1);
  strcpy(lock_config->clock_format, "%H:%M");
  lock_config->date_format = hikari_malloc(strlen("%A, %e %B") + 1);
  strcpy(lock_config->date_format, "%A, %e %B");

  hikari_font_init(&lock_config->clock_font, "sans 72");
  hikari_font_init(&lock_config->date_font, "sans 20");

  // [COMMENT] Action purpose: White at full alpha. The clock is drawn over a
  // blurred snapshot of the user's own desktop, whose brightness is unknown, so
  // the default has to be the value that stays legible over the widest range --
  // and it is paired with a drop shadow in lock_clock.c for the same reason.
  lock_config->clock_color[0] = 1.0f;
  lock_config->clock_color[1] = 1.0f;
  lock_config->clock_color[2] = 1.0f;
  lock_config->clock_color[3] = 1.0f;

  lock_config->blank_timeout_ac = 180;
  lock_config->blank_timeout_battery = 60;
}

void
hikari_lock_config_fini(struct hikari_lock_config *lock_config)
{
  hikari_free(lock_config->clock_format);
  lock_config->clock_format = NULL;

  hikari_free(lock_config->date_format);
  lock_config->date_format = NULL;

  hikari_font_fini(&lock_config->clock_font);
  hikari_font_fini(&lock_config->date_font);
}

#ifdef HIKARI_HAVE_SYSFS_POWER_SUPPLY
// [COMMENT] Function purpose: Read one attribute of one power supply into buf
// as a NUL-terminated string. Returns true when at least one byte was read.
// Every failure -- the device vanished mid-scan, EACCES, a short read, an
// empty file -- returns false, which the caller treats exactly as FreeBSD
// treats sysctlbyname(3) returning -1. That shared failure contract is what
// keeps the two platforms' answers identical where it matters.
static bool
read_power_supply_attr(
    const char *device, const char *attr, char *buf, size_t size)
{
  char path[512];

  if (size == 0) {
    return false;
  }

  buf[0] = '\0';

  // [COMMENT] Action purpose: snprintf(3) returning >= size means the name was
  // truncated, and a truncated path names a different file than the one asked
  // for. Refusing is the only safe reading of that.
  if ((size_t)snprintf(
          path, sizeof(path), "/sys/class/power_supply/%s/%s", device, attr) >=
      sizeof(path)) {
    return false;
  }

  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd == -1) {
    return false;
  }

  ssize_t n = read(fd, buf, size - 1);
  close(fd);

  if (n <= 0) {
    return false;
  }

  buf[n] = '\0';

  // [COMMENT] Action purpose: sysfs terminates these one-line attributes with
  // a newline. strcmp against "Mains" fails unless it is removed.
  char *nl = strchr(buf, '\n');
  if (nl != NULL) {
    *nl = '\0';
  }

  return true;
}

// [COMMENT] Function purpose: Report whether this machine is running on
// battery, by the same rule the FreeBSD branch applies to hw.acpi.acline.
// Sets *on_battery only when an answer exists; returns false when the machine
// offered none, which is the "no power source" case a desktop or a VM
// presents and which must fall through to the AC timeout.
static bool
sysfs_on_battery(bool *on_battery)
{
  DIR *dir = opendir("/sys/class/power_supply");

  if (dir == NULL) {
    return false;
  }

  bool saw_mains = false;
  bool mains_online = false;
  const struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {
    char type[32];

    if (entry->d_name[0] == '.') {
      continue;
    }

    if (!read_power_supply_attr(entry->d_name, "type", type, sizeof(type)) ||
        strcmp(type, "Mains") != 0) {
      continue;
    }

    char online[32];
    if (!read_power_supply_attr(
            entry->d_name, "online", online, sizeof(online))) {
      continue;
    }

    saw_mains = true;

    // [COMMENT] Action purpose: "online" is tri-valued, not boolean. The power
    // supply class ABI documents 0 Offline, 1 "Online Fixed" and 2 "Online
    // Programmable", so an == 1 test reports a USB-PD PPS supply as unplugged
    // and would blank such a laptop's screen on the battery timeout while it
    // charges. Testing against 0 is also the spelling the FreeBSD branch below
    // uses for hw.acpi.acline, so the two platforms agree without a third rule.
    //
    // Any online Mains wins: a laptop can expose both a barrel jack and a
    // USB-C Mains, and being plugged into either is being plugged in. Supplies
    // typed "USB" are deliberately not counted -- that is its own documented
    // type value, and an online USB supply may be a phone charging FROM this
    // machine.
    if (strtol(online, NULL, 10) != 0) {
      mains_online = true;
    }
  }

  closedir(dir);

  if (!saw_mains) {
    return false;
  }

  *on_battery = !mains_online;
  return true;
}
#endif

int
hikari_lock_config_blank_timeout(const struct hikari_lock_config *lock_config)
{
#ifdef HIKARI_HAVE_SYSFS_POWER_SUPPLY
  bool on_battery;

  // [COMMENT] Action purpose: Only an affirmative "on battery" shortens the
  // timeout. A machine that reported no mains supply at all has not said it is
  // on battery, so it falls through to the AC timeout below -- the same answer
  // the FreeBSD branch gives when the sysctl does not exist.
  if (sysfs_on_battery(&on_battery) && on_battery) {
    return lock_config->blank_timeout_battery;
  }
#endif

#ifdef HIKARI_HAVE_ACPI_ACLINE
  int acline;
  size_t len = sizeof(acline);

  /* [COMMENT] Action purpose: hw.acpi.acline is 1 on mains and 0 on battery.
  A machine with no ACPI power source at all -- a desktop, or a VM -- has no
  such sysctl, and the read fails; falling through to the AC timeout is the
  right answer there, since a machine that cannot be on battery should not be
  given the battery's aggressive blanking. */
  if (sysctlbyname("hw.acpi.acline", &acline, &len, NULL, 0) == 0 &&
      acline == 0) {
    return lock_config->blank_timeout_battery;
  }
#endif

  return lock_config->blank_timeout_ac;
}
