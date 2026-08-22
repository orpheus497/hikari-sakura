#if !defined(HIKARI_LOCK_CONFIG_H)
#define HIKARI_LOCK_CONFIG_H

#include <stdbool.h>

#include <hikari/font.h>

/* [COMMENT] Class purpose: Everything the lock screen draws and how long it
stays lit, parsed from the `ui { lock { ... } }` block.

Upstream had none of this: the lock screen was a blanked output with a password
circle, and any information on it had to come from a client the user marked
`public`. The blur and the clock are compositor-drawn, so they work with no
client running and cannot be spoofed by one. */
struct hikari_lock_config {
  /* Blur of the workspace snapshot taken at lock time. `radius` is in pixels
  at output scale; `passes` is how many box-blur passes approximate a Gaussian
  (three is the standard approximation and the default). */
  bool blur;
  int blur_radius;
  int blur_passes;

  /* Clock and date. Both are strftime(3) format strings. `date_format` may be
  NULL, which draws the time alone. */
  bool clock;
  char *clock_format;
  char *date_format;
  struct hikari_font clock_font;
  struct hikari_font date_font;
  float clock_color[4];

  /* Seconds of inactivity before the outputs are powered down while locked,
  chosen by whether the machine is on mains power. 0 disables blanking. Split
  because the useful value differs by an order of magnitude: on battery the
  screen should die quickly, on mains a visible clock is the point. */
  int blank_timeout_ac;
  int blank_timeout_battery;
};

void
hikari_lock_config_init(struct hikari_lock_config *lock_config);

void
hikari_lock_config_fini(struct hikari_lock_config *lock_config);

/* [COMMENT] Function purpose: Seconds to wait before blanking, chosen from the
current power source. Reads the power state at call time rather than caching it,
so unplugging the mains while the screen is locked takes effect on the next
keystroke. Returns 0 when blanking is disabled. */
int
hikari_lock_config_blank_timeout(const struct hikari_lock_config *lock_config);

#endif
