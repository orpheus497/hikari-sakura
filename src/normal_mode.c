#include <hikari/normal_mode.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>

#include <hikari/action.h>
#include <hikari/binding.h>
#include <hikari/evdev.h>
#include <hikari/binding_group.h>
#include <hikari/configuration.h>
#include <hikari/indicator.h>
#include <hikari/indicator_frame.h>
#include <hikari/keyboard.h>
#include <hikari/pointer_constraints.h>

#include <hikari/server.h>
#include <hikari/view.h>

#ifndef NDEBUG
#include <hikari/layout.h>
#include <hikari/sheet.h>
#include <hikari/workspace.h>
#endif

struct cursor_down_state {
  double lx;
  double ly;
  double sx;
  double sy;
};

static struct cursor_down_state cursor_down_state;


static bool
handle_input(struct hikari_binding_group *map, uint32_t code)
{
  int nbindings = map->nbindings;
  struct hikari_binding *bindings = map->bindings;

  for (int i = 0; i < nbindings; i++) {
    struct hikari_binding *binding = &bindings[i];

    if (binding->keycode == code) {
      struct hikari_event_action *event_action;

      if (binding->action->end.action != NULL) {
        hikari_server.normal_mode.pending_action = &binding->action->end;
      }

      event_action = &binding->action->begin;
      if (event_action->action != NULL) {
        event_action->action(event_action->arg);
      }
      return true;
    }
  }

  return false;
}

static bool
handle_pending_action(void)
{
  struct hikari_event_action *pending_action =
      hikari_server.normal_mode.pending_action;

  if (pending_action != NULL) {
    pending_action->action(pending_action->arg);
    hikari_server.normal_mode.pending_action = NULL;
    return true;
  }

  return false;
}

#ifndef NDEBUG
static void
dump_debug(struct hikari_server *server)
{
  struct hikari_view *view;
  printf("---------------------------------------------------------------------"
         "\n");
  printf("VIEWS\n");
  printf("---------------------------------------------------------------------"
         "\n");
  wl_list_for_each (view, &hikari_server.visible_views, visible_server_views) {
    printf("%p ", view);
  }
  printf("\n");
  printf("---------------------------------------------------------------------"
         "\n");
  printf("GROUPS\n");
  printf("---------------------------------------------------------------------"
         "\n");
  struct hikari_group *group;
  wl_list_for_each (group, &hikari_server.groups, server_groups) {
    printf("%s ", group->name);

    wl_list_for_each (view, &group->visible_views, visible_group_views) {
      printf("%p ", view);
    }

    printf("/ ");

    wl_list_for_each (view, &group->views, group_views) {
      printf("%p ", view);
    }

    printf("\n");
  }
  printf("---------------------------------------------------------------------"
         "\n");
  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    struct hikari_workspace *workspace = output->workspace;
    const char *output_name = workspace->output->wlr_output->name;
    printf("SHEETS %s (%p)\n", output_name, workspace->focus_view);
    printf(
        "---------------------------------------------------------------------"
        "\n");
    struct hikari_sheet *sheets = workspace->sheets;
    struct hikari_sheet *sheet;
    for (int i = 0; i < 10; i++) {
      sheet = &sheets[i];
      if (!wl_list_empty(&sheet->views)) {
        printf("%d ", sheet->nr);
        wl_list_for_each (view, &sheet->views, sheet_views) {
          printf("%p ", view);
        }
        printf("\n");
      }
    }
    printf(
        "---------------------------------------------------------------------"
        "\n");
    if (workspace->sheet->layout != NULL) {
      printf(
          "-------------------------------------------------------------------"
          "--\n");
      printf("LAYOUT %s\n", output_name);
      struct hikari_tile *tile;
      wl_list_for_each (tile, &workspace->sheet->layout->tiles, layout_tiles) {
        printf("%p ", tile->view);
      }
      printf("\n");
    }
  }
  printf("/////////////////////////////////////////////////////////////////////"
         "\n");
}
#endif

static void
modifiers_handler(struct hikari_keyboard *keyboard)
{
  wlr_seat_set_keyboard(hikari_server.seat, keyboard->wlr_keyboard);
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (hikari_server.keyboard_state.mod_released) {

    if (hikari_server_is_cycling() && focus_view != NULL) {
      hikari_view_raise(focus_view);
      hikari_view_center_cursor(focus_view);
      hikari_server_cursor_focus();
    }

    hikari_server_unset_cycling();
  }

  if (hikari_server.keyboard_state.mod_changed) {
    /* [COMMENT] Action purpose: The Logo key going down or up drives the
    indicator overlay on and off, and the two transitions need different
    calls. hikari_indicator_damage() is only hikari_indicator_position(), so
    using it for both shows the overlay on release as much as on press and
    never takes it down. */
    if (hikari_server_is_indicating()) {
      if (focus_view != NULL) {
        hikari_group_damage(focus_view->group);
      }
      hikari_indicator_show(&hikari_server.indicator, focus_view);
    } else {
      hikari_indicator_hide(&hikari_server.indicator, focus_view);

      if (focus_view != NULL) {
        hikari_group_damage(focus_view->group);
      }
    }
#ifndef NDEBUG
    dump_debug(&hikari_server);
#endif
  }

  wlr_seat_keyboard_notify_modifiers(
      hikari_server.seat, &keyboard->wlr_keyboard->modifiers);
}

static void
cancel(void)
{}

// Function purpose: Give a node keyboard/raise focus unless it already has it.
static void
focus_node_if_needed(struct hikari_node *node)
{
  struct hikari_workspace *focused = hikari_server.workspace;

  /* Action purpose: A layer node is never equal to focus_view, so comparing
  against that alone re-focused a hovered layer surface on every motion event
  -- for a keyboard-interactive one, a full seat keyboard leave/enter per
  event. */
  struct hikari_node *focus_node = (struct hikari_node *)focused->focus_view;
#ifdef HAVE_LAYERSHELL
  struct hikari_node *focus_layer_node =
      (struct hikari_node *)focused->focus_layer;
#else
  /* Action purpose: hikari_workspace::focus_layer only exists with
  layer-shell support, and without it no layer node can be hovered, so the
  comparison below is against nothing. */
  struct hikari_node *focus_layer_node = NULL;
#endif

  if (node != focus_node && node != focus_layer_node) {
    hikari_node_focus(node);
  }
}

static void
cursor_down_move(uint32_t time)
{
  struct wlr_seat *seat = hikari_server.seat;

  double x = hikari_server.cursor.wlr_cursor->x;
  double y = hikari_server.cursor.wlr_cursor->y;

  double moved_x = x - cursor_down_state.lx;
  double moved_y = y - cursor_down_state.ly;

  double sx = cursor_down_state.sx + moved_x;
  double sy = cursor_down_state.sy + moved_y;

  wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

static void
cursor_move(uint32_t time)
{
  assert(hikari_server_in_normal_mode());

  /* [COMMENT] Action purpose: A held touch contact pins focus, exactly as a
  held mouse button does by swapping this function out for cursor_down_move().

  This function is reached by far more than pointer motion:
  hikari_server_cursor_focus() calls it whenever the scene changes under the
  cursor -- a window mapping or unmapping, a re-tile, a layer surface
  appearing, a workspace switch. Each re-hit-tests and can focus and
  pointer-enter a different surface, which during a contact is wrong three
  times over: keyboard focus moves away from the window the finger is on,
  wlroots resets its button accounting on the enter so the contact's release
  is later dropped as unmatched, and the emulated motion goes on reporting
  coordinates in the old surface's frame into the new one.

  Tested here rather than by swapping in a pinned variant the way the mouse
  does, because the two can overlap: a mouse press during a contact would
  overwrite the swapped pointer and its release would restore the unpinned
  one, losing the pin while the finger is still down. One condition cannot get
  that wrong. Nothing below runs, pointer_constraint_refresh() included, which
  is what cursor_down_move() also does for a mouse drag. */
  if (hikari_server.cursor.has_primary_touch) {
    return;
  }

  double sx, sy;
  struct wlr_seat *seat = hikari_server.seat;
  struct wlr_surface *surface;
  struct hikari_workspace *workspace;

  struct hikari_node *node =
      hikari_server_node_at(hikari_server.cursor.wlr_cursor->x,
          hikari_server.cursor.wlr_cursor->y,
          &surface,
          &workspace,
          &sx,
          &sy);

  if (node != NULL) {
    focus_node_if_needed(node);

    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(seat, time, sx, sy);
  } else {
    if (hikari_server.workspace != workspace) {
      struct hikari_view *view = hikari_workspace_first_view(workspace);
      hikari_workspace_focus_view(workspace, view);
    }
    if (seat->pointer_state.focused_surface != NULL) {
      hikari_cursor_reset_image(&hikari_server.cursor);
    }
    wlr_seat_pointer_clear_focus(seat);
  }

  /* Action purpose: Seat pointer focus has just been settled -- either onto a
  surface above or cleared below -- and that is the only input the activation
  decision takes. Placed at the end so both branches are covered by one call
  rather than each growing its own.

  Nothing above this line was changed. Focus-follows-mouse is untouched: when a
  client holds the pointer the cursor does not move, so motion_handler returns
  before ever reaching this function and none of it runs. */
  hikari_pointer_constraint_refresh();
}

static inline void
start_cursor_down_handling(struct wlr_pointer_button_event *event)
{
  double lx = hikari_server.cursor.wlr_cursor->x;
  double ly = hikari_server.cursor.wlr_cursor->y;

  double sx;
  double sy;
  struct hikari_workspace *workspace;
  struct wlr_surface *surface;

  struct hikari_node *node =
      hikari_server_node_at(lx, ly, &surface, &workspace, &sx, &sy);

  if (node != NULL) {
    hikari_server.normal_mode.mode.cursor_move = cursor_down_move;
    cursor_down_state.lx = lx;
    cursor_down_state.ly = ly;
    cursor_down_state.sx = sx;
    cursor_down_state.sy = sy;
  }

  wlr_seat_pointer_notify_button(
      hikari_server.seat, event->time_msec, event->button, event->state);
}

static inline void
stop_cursor_down_handling(struct wlr_pointer_button_event *event)
{
  hikari_server.normal_mode.mode.cursor_move = cursor_move;

  wlr_seat_pointer_notify_button(
      hikari_server.seat, event->time_msec, event->button, event->state);

  hikari_server_cursor_focus();
}

static inline bool
is_cursor_down(void)
{
  return hikari_server.normal_mode.mode.cursor_move == cursor_down_move;
}

static void
button_handler(
    struct hikari_cursor *cursor, struct wlr_pointer_button_event *event)
{
  if (handle_pending_action()) {
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED && is_cursor_down()) {
      stop_cursor_down_handling(event);
    }
    return;
  }

  if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
    uint32_t modifiers = hikari_server.keyboard_state.modifiers;
    struct hikari_binding_group *map = &cursor->bindings[modifiers];

    if (!handle_input(map, event->button)) {
      start_cursor_down_handling(event);
    }
  } else {
    if (is_cursor_down()) {
      stop_cursor_down_handling(event);
    } else {
      wlr_seat_pointer_notify_button(
          hikari_server.seat, event->time_msec, event->button, event->state);
    }
  }
}

static void
key_handler(
    struct hikari_keyboard *keyboard, struct wlr_keyboard_key_event *event)
{
  if (handle_pending_action()) {
    return;
  }

  if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    uint32_t modifiers = hikari_server.keyboard_state.modifiers;
    struct hikari_binding_group *bindings = &keyboard->bindings[modifiers];

    if (handle_input(bindings, event->keycode)) {
      return;
    }
  }

  wlr_seat_set_keyboard(hikari_server.seat, keyboard->wlr_keyboard);
  wlr_seat_keyboard_notify_key(
      hikari_server.seat, event->time_msec, event->keycode, event->state);
}

/* [COMMENT] Function purpose: Window management for a touch contact beginning,
and nothing client-facing.

Runs the mouse bindings, so a `pointer BTN_LEFT` binding fires from a
touchscreen too, and the pending end action a press-and-hold binding leaves
behind.

Focus is taken here rather than left to cursor_move. A pointer has hovered the
node before it clicks, so focus is already settled by the time the button
arrives; a finger has not hovered anything, and wlr_cursor_warp() emits no
motion signal, so nothing else would settle it until the contact ended. */
static bool
touch_begin(struct hikari_cursor *cursor, uint32_t time_msec)
{
  if (handle_pending_action()) {
    return true;
  }

  double sx;
  double sy;
  struct wlr_surface *surface;
  struct hikari_workspace *workspace;

  struct hikari_node *node =
      hikari_server_node_at(cursor->wlr_cursor->x,
          cursor->wlr_cursor->y,
          &surface,
          &workspace,
          &sx,
          &sy);

  if (node != NULL) {
    focus_node_if_needed(node);
  }

  struct hikari_binding_group *map =
      &cursor->bindings[hikari_server.keyboard_state.modifiers];

  return handle_input(map, BTN_LEFT);
}

/* [COMMENT] Function purpose: Window management for a touch contact ending.

First the pending end action: a press-and-hold mouse binding stores its end
action when the press fires and expects the release to run it, and a finger
must honour that the same way.

Then focus is resynced, because the contact pinned it. cursor_move() returns
early for the whole of a contact, so a physical mouse moved meanwhile warps
the cursor without carrying pointer focus along; unresynced, the next click --
which need not be preceded by any motion -- would go to whatever surface the
contact had entered rather than the one now under the cursor.
has_primary_touch is already false here, so the refresh does the full hit
test.

Not while a physical button is held. hikari_server_cursor_focus() dispatches
through mode->cursor_move, which is cursor_down_move() for the duration of a
pointer drag -- and that reports motion from the cursor's CURRENT position,
which the contact warped to the finger. Refreshing here would jump the drag to
wherever the finger was. stop_cursor_down_handling() refreshes focus when the
button is released, which is the right moment for it.

This cannot run during a lock: lock entry releases the contact through
end_primary_touch(), which does not call this, and detaches the touch
listeners before installing lock mode. */
static void
touch_end(struct hikari_cursor *cursor, uint32_t time_msec)
{
  handle_pending_action();

  if (!is_cursor_down()) {
    hikari_server_cursor_focus();
  }
}

void
hikari_normal_mode_init(struct hikari_normal_mode *normal_mode)
{
  normal_mode->mode.key_handler = key_handler;
  normal_mode->mode.button_handler = button_handler;
  normal_mode->mode.modifiers_handler = modifiers_handler;

  normal_mode->mode.cancel = cancel;
  normal_mode->mode.cursor_move = cursor_move;

  normal_mode->mode.touch_begin = touch_begin;
  normal_mode->mode.touch_end = touch_end;

  /* Action purpose: The only mode in which a contact is delivered to a
  pointer-only client. See struct hikari_mode. */
  normal_mode->mode.touch_emulates_pointer = true;
  /* Action purpose: No touch_motion. A contact that has already begun must not
  re-focus whatever it is dragged over -- a finger swiping inside a window is
  scrolling it, not choosing a new one -- and the client's own motion is the
  touch layer's to deliver. move_mode and resize_mode do set one, because there
  the motion IS the window management. */

  normal_mode->pending_action = NULL;
}

void
hikari_normal_mode_enter(void)
{
  struct hikari_server *server = &hikari_server;

  assert(server->workspace != NULL);

  hikari_server.normal_mode.mode.cursor_move = cursor_move;

  server->mode->cancel();
  server->mode = (struct hikari_mode *)&server->normal_mode;
}
