#if !defined(HIKARI_LOCK_INDICATOR_H)
#define HIKARI_LOCK_INDICATOR_H

#include <stdbool.h>

#include <wayland-util.h>

#include <wlr/render/wlr_texture.h>

struct hikari_output;

struct hikari_lock_indicator {
  struct wlr_buffer *wait;
  struct wlr_buffer *type;
  struct wlr_buffer *verify;
  struct wlr_buffer *deny;

  struct wlr_buffer *current;

  /* [COMMENT] Class purpose: A relayed PAM diagnostic, rendered on demand and
  shown below the indicator circles. Separate from the four fixed buffers above
  because its content is not known until a module explains itself, so it is
  built when one does and dropped when there is nothing left to say -- which is
  the normal state, and is why NULL is the resting value. The dimensions are
  cached alongside it purely so the per-output positioning does not have to ask
  the buffer for them on every damage pass. */
  struct wlr_buffer *message;
  int message_width;
  int message_height;

  struct wl_event_source *reset_state;
};

void
hikari_lock_indicator_init(struct hikari_lock_indicator *lock_indicator);

void
hikari_lock_indicator_fini(struct hikari_lock_indicator *lock_indicator);

void
hikari_lock_indicator_set_wait(struct hikari_lock_indicator *lock_indicator);

void
hikari_lock_indicator_set_type(struct hikari_lock_indicator *lock_indicator);

void
hikari_lock_indicator_set_verify(struct hikari_lock_indicator *lock_indicator);

void
hikari_lock_indicator_set_deny(struct hikari_lock_indicator *lock_indicator);

void
hikari_lock_indicator_clear(struct hikari_lock_indicator *lock_indicator);

/* [COMMENT] Function purpose: Show one line of diagnostic text below the
indicator, replacing whatever was there. `is_error` picks the colour: a
PAM_ERROR_MSG is drawn in the conflict colour, a PAM_TEXT_INFO in the clock's.
`text` must already be NUL-terminated, control-stripped and valid UTF-8 --
see sanitize_message() in src/lock_mode.c, which is the only caller. */
void
hikari_lock_indicator_set_message(
    struct hikari_lock_indicator *lock_indicator,
    const char *text,
    bool is_error);

/* [COMMENT] Function purpose: Take the diagnostic line back down. Called when
the password buffer is cleared, so the reason the last attempt was refused does
not linger over a fresh one. */
void
hikari_lock_indicator_clear_message(
    struct hikari_lock_indicator *lock_indicator);

void
hikari_lock_indicator_damage(struct hikari_lock_indicator *lock_indicator);

#endif
