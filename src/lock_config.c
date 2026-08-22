// [COMMENT] Script function and purpose: Defaults and power-source resolution
// for the lock screen's appearance settings. Parsing lives with the rest of the
// configuration in src/configuration.c; this file owns the values themselves.

#include <hikari/lock_config.h>

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

int
hikari_lock_config_blank_timeout(const struct hikari_lock_config *lock_config)
{
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
