#if !defined(HIKARI_MODE_H)
#define HIKARI_MODE_H

#include <stdbool.h>

#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>

struct hikari_keyboard;

struct hikari_cursor;

struct hikari_mode {
  void (*key_handler)(
      struct hikari_keyboard *keyboard, struct wlr_keyboard_key_event *event);

  void (*modifiers_handler)(struct hikari_keyboard *keyboard);

  void (*button_handler)(
      struct hikari_cursor *cursor, struct wlr_pointer_button_event *event);

  void (*cursor_move)(uint32_t time_msec);

  void (*cancel)(void);

  /* [COMMENT] Action purpose: A touch contact's compositor-side half, kept
  separate from button_handler rather than fed through it as a fabricated
  BTN_LEFT event.

  A finger is not a mouse, and button_handler is written for one: it assumes
  motion has already settled pointer focus before the button arrives (true of a
  pointer, false of a touch point, because wlr_cursor_warp() emits no motion
  signal), it assumes a focus refresh on release is always wanted, and it keeps
  a single cursor-down flag because a seat has one pointer but many fingers.
  Routing touch through it therefore needed a suppression flag consulted at
  every client-facing call inside one mode, which left the other modes
  forwarding a finger as a click and the suppressed paths subtly wrong.

  These three run the window management a contact asks for and NOTHING
  client-facing. Delivering the contact to the client -- as real wl_touch, or
  as emulated wl_pointer for a client that cannot speak it -- belongs to the
  touch handlers in cursor.c alone, which is what makes one finger reach a
  client exactly once without any mode having to know about it.

  All three are optional. hikari_server is a zeroed global (server.c), so a
  mode that leaves them unset ignores touch, which is the right answer for the
  nine modes that do nothing with a pointer button either. */

  /* Returns whether the contact was consumed by the compositor -- a mouse
  binding fired, or a pending end action ran. A consumed contact is not
  emulated as a pointer button, exactly as a bound mouse press is not
  forwarded. */
  bool (*touch_begin)(struct hikari_cursor *cursor, uint32_t time_msec);

  /* The contact moved. Separate from cursor_move because a pointer's motion
  also carries the client its enter and motion, and a finger's must not. */
  void (*touch_motion)(uint32_t time_msec);

  void (*touch_end)(struct hikari_cursor *cursor, uint32_t time_msec);
};

#endif
