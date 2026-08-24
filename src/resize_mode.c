#include <hikari/resize_mode.h>

#include <stdbool.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>

#include <hikari/binding.h>
#include <hikari/configuration.h>
#include <hikari/keyboard.h>

#include <hikari/server.h>
#include <hikari/view.h>

static void
cancel(void)
{
  struct hikari_view *view = hikari_server.workspace->focus_view;

  if (view != NULL) {
    struct hikari_indicator *indicator = &hikari_server.indicator;

    hikari_indicator_set_color(
        indicator, hikari_configuration->indicator_selected);
    hikari_indicator_update(indicator, view);
    hikari_indicator_damage(indicator, view);
    hikari_group_damage(view->group);

    hikari_view_center_cursor(view);
  }
}

// [COMMENT] Function purpose: Handle keyboard events during resize mode.
static void
key_handler(
    struct hikari_keyboard *keyboard, struct wlr_keyboard_key_event *event)
{
  if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
    hikari_server_enter_normal_mode(NULL);
  }
}

static void
modifiers_handler(struct hikari_keyboard *keyboard)
{}

static void
cursor_move(uint32_t time_msec)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  assert(focus_view != NULL);

  struct hikari_resize_mode *resize_mode = &hikari_server.resize_mode;
  struct hikari_output *output = focus_view->output;
  struct wlr_box *geometry = hikari_view_geometry(focus_view);

  int cursor_x = hikari_server.cursor.wlr_cursor->x - output->geometry.x;
  int cursor_y = hikari_server.cursor.wlr_cursor->y - output->geometry.y;

  /* [COMMENT] Action purpose: The anchor is the pointer's offset from the
  bottom-right corner at grab time, so subtracting it yields the corner the
  pointer is dragging, and the new size is that corner minus the origin.

  This replaces `cursor_x - geometry->x - border`. That expression assumed the
  pointer sat exactly on the corner and then subtracted a border width that the
  warp had not added, so every entry into resize mode shrank the window by
  `border` pixels even if the pointer never moved. The anchor makes the entry
  size a fixed point by construction, whatever the pointer is doing. */
  int new_width = (cursor_x - resize_mode->anchor_x) - geometry->x;
  int new_height = (cursor_y - resize_mode->anchor_y) - geometry->y;

  if (new_width > 0 && new_height > 0) {
    hikari_view_resize_absolute(focus_view, new_width, new_height);
  }
}

// [COMMENT] Function purpose: Handle pointer button events during resize mode.
static void
button_handler(
    struct hikari_cursor *cursor, struct wlr_pointer_button_event *event)
{
  if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
    hikari_server_enter_normal_mode(NULL);
  }
}

// [COMMENT] Function purpose: Initialize resize mode state and handlers.
void
hikari_resize_mode_init(struct hikari_resize_mode *resize_mode)
{
  resize_mode->mode.key_handler = key_handler;
  resize_mode->mode.button_handler = button_handler;
  resize_mode->mode.modifiers_handler = modifiers_handler;

  resize_mode->mode.cancel = cancel;
  resize_mode->mode.cursor_move = cursor_move;

  resize_mode->anchor_x = 0;
  resize_mode->anchor_y = 0;
}

/* [COMMENT] Function purpose: Establish the grab anchor for a resize, measured
from the bottom-right corner.

Same two cases as the move mode's set_anchor(), and for the same reason: a
pointer already over the window grabs where it is, a pointer elsewhere gets the
corner warp hikari has always performed. The anchor is zero in the second case,
which -- unlike the old expression -- is now genuinely a no-op on entry. */
static void
set_anchor(struct hikari_resize_mode *resize_mode, struct hikari_view *view)
{
  struct hikari_output *output = view->output;
  struct wlr_box *border_geometry = hikari_view_border_geometry(view);
  struct wlr_box *geometry = hikari_view_geometry(view);

  int cursor_x = hikari_server.cursor.wlr_cursor->x - output->geometry.x;
  int cursor_y = hikari_server.cursor.wlr_cursor->y - output->geometry.y;

  bool over_view = cursor_x >= border_geometry->x &&
                   cursor_x < border_geometry->x + border_geometry->width &&
                   cursor_y >= border_geometry->y &&
                   cursor_y < border_geometry->y + border_geometry->height;

  if (over_view) {
    resize_mode->anchor_x = cursor_x - (geometry->x + geometry->width);
    resize_mode->anchor_y = cursor_y - (geometry->y + geometry->height);
  } else {
    resize_mode->anchor_x = 0;
    resize_mode->anchor_y = 0;
    hikari_view_bottom_right_cursor(view);
  }
}

void
hikari_resize_mode_enter(struct hikari_view *view)
{
  struct hikari_indicator *indicator = &hikari_server.indicator;

  hikari_indicator_set_color(indicator, hikari_configuration->indicator_insert);
  hikari_indicator_update(indicator, view);

  hikari_view_raise(view);

  // [COMMENT] Action purpose: After the raise, so the anchor is measured
  // against geometry nothing else is about to change.
  set_anchor(&hikari_server.resize_mode, view);

  hikari_server.mode = (struct hikari_mode *)&hikari_server.resize_mode;
}
