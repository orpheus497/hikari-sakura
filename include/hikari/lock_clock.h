#if !defined(HIKARI_LOCK_CLOCK_H)
#define HIKARI_LOCK_CLOCK_H

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_scene_buffer;

/* [COMMENT] Class purpose: The compositor-drawn clock on the lock screen.

One scene buffer per output, redrawn on a timer that fires on the minute rather
than every second -- the default format has no seconds field, so a per-second
repaint would re-shape the text sixty times an hour to produce identical pixels.

This is deliberately not a client. A clock the compositor draws is present with
no session running, survives a client crash, and cannot be impersonated by a
window that merely looks like one. */
struct hikari_lock_clock {
  struct wl_event_source *tick;
};

void
hikari_lock_clock_init(struct hikari_lock_clock *clock);

void
hikari_lock_clock_fini(struct hikari_lock_clock *clock);

/* [COMMENT] Function purpose: Draw (or redraw) the clock on every output and
arm the next tick. Safe to call repeatedly; each call replaces the previous
buffer. */
void
hikari_lock_clock_refresh(struct hikari_lock_clock *clock);

#endif
