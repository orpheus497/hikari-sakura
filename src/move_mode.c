#include <hikari/move_mode.h>

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

// [COMMENT] Function purpose: Handles keyboard events during move mode.
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
  double lx = hikari_server.cursor.wlr_cursor->x;
  double ly = hikari_server.cursor.wlr_cursor->y;

  struct wlr_output *wlr_output =
      wlr_output_layout_output_at(hikari_server.output_layout, lx, ly);

  /* [COMMENT] Action purpose: The pointer is over no output at all. Screens of
  different heights sharing a row leave layout rectangles that belong to nothing
  -- a 1920x1200 panel beside a 1920x1080 one puts rows 1080-1200 at x >= 1920
  on no display -- and the cursor is free to enter them. Returning here holds the
  window at its last valid position rather than moving it somewhere unpaintable;
  the drag resumes the moment the pointer is over a screen again. */
  if (wlr_output == NULL) {
    return;
  }

  struct hikari_output *output = wlr_output->data;
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  assert(focus_view != NULL);

  struct hikari_output *view_output = focus_view->output;
  struct hikari_move_mode *move_mode = &hikari_server.move_mode;

  /* [COMMENT] Action purpose: Subtract the grab anchor, so the window follows
  the pointer's DELTA rather than teleporting its origin to the pointer's
  absolute position. See the anchor's comment in move_mode.h.

  Applied to BOTH branches. Crossing a screen boundary is still the same drag,
  and passing the raw pointer position to the migrate path put the window's
  top-left corner under the cursor for exactly one motion event before the next
  one returned it -- a corner-jump at the seam on every crossing. The migrate
  path uses these coordinates only as a position and never as a pointer
  location, so anchoring them here is correct rather than merely convenient. */
  double anchored_x = lx - move_mode->anchor_x;
  double anchored_y = ly - move_mode->anchor_y;

  if (output == view_output) {
    hikari_view_move_absolute(focus_view,
        anchored_x - view_output->geometry.x,
        anchored_y - view_output->geometry.y);
  } else {
    hikari_server_migrate_focus_view(output, anchored_x, anchored_y, false);
  }
}

// [COMMENT] Function purpose: Handles pointer button events during move mode.
static void
button_handler(
    struct hikari_cursor *cursor, struct wlr_pointer_button_event *event)
{
  if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
    hikari_server_enter_normal_mode(NULL);
  }
}

void
hikari_move_mode_init(struct hikari_move_mode *move_mode)
{
  move_mode->mode.key_handler = key_handler;
  move_mode->mode.button_handler = button_handler;
  move_mode->mode.modifiers_handler = modifiers_handler;

  move_mode->mode.cancel = cancel;
  move_mode->mode.cursor_move = cursor_move;

  move_mode->anchor_x = 0;
  move_mode->anchor_y = 0;
}

/* [COMMENT] Function purpose: Establish the grab anchor for a move.

Two cases, and the distinction is what preserves the historical behaviour while
fixing the common one. When the pointer is already over the window -- every
pointer-initiated move, and a keyboard-initiated one after a focus change, since
focusing centres the pointer on the view -- the window is grabbed exactly where
it was taken hold of and does not move until the pointer does. When the pointer
is somewhere else entirely, there is no meaningful grab point, so the corner
warp hikari has always done is kept and the anchor is zero, which reproduces the
old path exactly. */
static void
set_anchor(struct hikari_move_mode *move_mode, struct hikari_view *view)
{
  struct hikari_output *output = view->output;
  struct wlr_box *border_geometry = hikari_view_border_geometry(view);
  struct wlr_box *geometry = hikari_view_geometry(view);

  int cursor_x = hikari_server.cursor.wlr_cursor->x - output->geometry.x;
  int cursor_y = hikari_server.cursor.wlr_cursor->y - output->geometry.y;

  /* [COMMENT] Action purpose: Tested against the BORDER box, not the content
  box, so taking hold of a window by its border counts as grabbing the window.
  The anchor itself is measured from the content origin, because that is what
  hikari_view_move_absolute() positions. */
  bool over_view = cursor_x >= border_geometry->x &&
                   cursor_x < border_geometry->x + border_geometry->width &&
                   cursor_y >= border_geometry->y &&
                   cursor_y < border_geometry->y + border_geometry->height;

  if (over_view) {
    move_mode->anchor_x = cursor_x - geometry->x;
    move_mode->anchor_y = cursor_y - geometry->y;
  } else {
    move_mode->anchor_x = 0;
    move_mode->anchor_y = 0;
    hikari_view_top_left_cursor(view);
  }
}

void
hikari_move_mode_enter(struct hikari_view *view)
{
  struct hikari_indicator *indicator = &hikari_server.indicator;

  hikari_indicator_set_color(indicator, hikari_configuration->indicator_insert);
  hikari_indicator_update(indicator, view);

  hikari_view_raise(view);

  // [COMMENT] Action purpose: After the raise, so the anchor is measured
  // against geometry nothing else is about to change.
  set_anchor(&hikari_server.move_mode, view);

  hikari_server.mode = (struct hikari_mode *)&hikari_server.move_mode;
}
