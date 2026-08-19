#if !defined(HIKARI_LOCK_MODE_H)
#define HIKARI_LOCK_MODE_H

#include <stdbool.h>

#include <wayland-util.h>

#include <hikari/lock_indicator.h>
#include <hikari/mode.h>

struct hikari_lock_mode {
  struct hikari_mode mode;
  struct wl_event_source *disable_outputs;
  struct wl_event_source *locker_event_source;
  /* [COMMENT] Class purpose: Retry timer for reaping the unlocker child
  without blocking. The compositor must never call a blocking waitpid() from a
  Wayland event-loop callback -- the child writes its result before it finishes
  PAM cleanup and exits, so a blocking reap stalls the whole compositor. */
  struct wl_event_source *locker_reap_timer;
  struct hikari_lock_indicator *lock_indicator;

  bool outputs_disabled;
};

void
hikari_lock_mode_init(struct hikari_lock_mode *lock_mode);

void
hikari_lock_mode_fini(struct hikari_lock_mode *lock_mode);

void
hikari_lock_mode_enter(void);

static inline bool
hikari_lock_mode_are_outputs_disabled(struct hikari_lock_mode *lock_mode)
{
  return lock_mode->outputs_disabled;
}

#endif
