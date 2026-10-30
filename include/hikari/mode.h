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

  /* [COMMENT] Action purpose: A touch contact's compositor-side half, separate
  from button_handler because button_handler is written for a pointer: it
  assumes motion settled focus before the button arrived (wlr_cursor_warp()
  emits no motion signal, so for a contact nothing has), that a focus refresh
  on release is always wanted, and that one cursor-down flag suffices for what
  is one pointer but many fingers.

  These three run window management and NOTHING client-facing. Delivering a
  contact to its client -- real wl_touch, or emulated wl_pointer for a client
  that cannot speak it -- belongs to cursor.c alone, so one finger reaches a
  client exactly once without any mode having to know about it.

  All three are optional. hikari_server is a zeroed global (server.c), so a
  mode that leaves them unset ignores touch. */

  /* Returns whether the contact was consumed by the compositor -- a mouse
  binding fired, or a pending end action ran. A consumed contact is not
  emulated as a pointer button, exactly as a bound mouse press is not
  forwarded. */
  bool (*touch_begin)(struct hikari_cursor *cursor, uint32_t time_msec);

  /* The contact moved. Separate from cursor_move because a pointer's motion
  also carries the client its enter and motion, and a finger's must not. */
  void (*touch_motion)(uint32_t time_msec);

  void (*touch_end)(struct hikari_cursor *cursor, uint32_t time_msec);

  /* [COMMENT] Action purpose: Whether a contact in this mode may be delivered
  to a pointer-only client as emulated wl_pointer input.

  False by default, and deliberately so. Most modes exist precisely to swallow
  input until they are answered -- mark_assign, group_assign, sheet_assign,
  layout_select and mark_select all carry an empty button_handler, and that
  emptiness IS the mechanism; move and resize are driving the window
  themselves and the client has no use for a pointer; input_grab has given one
  view the input and an enter elsewhere would break the grab; dnd must not
  have a finger inject a button into a pointer drag.

  Normal mode is the only one that answers yes. The default of false means a
  mode added later declines until it says otherwise, which is the safe
  direction. */
  bool touch_emulates_pointer;
};

#endif
