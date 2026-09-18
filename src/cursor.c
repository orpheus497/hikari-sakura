// [COMMENT] Script function and purpose: Hikari cursor input handling and pointer axis event dispatching.

#include <hikari/cursor.h>

#include <assert.h>
#include <errno.h>
#include <math.h>

#include <dev/evdev/input-event-codes.h>

#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include <hikari/action.h>
#include <hikari/binding.h>
#include <hikari/binding_config.h>
#include <hikari/configuration.h>
#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/pointer_constraints.h>
#include <hikari/server.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>

static void
motion_absolute_handler(struct wl_listener *listener, void *data);

static void
frame_handler(struct wl_listener *listener, void *data);

static void
motion_handler(struct wl_listener *listener, void *data);

static void
button_handler(struct wl_listener *listener, void *data);

static void
axis_handler(struct wl_listener *listener, void *data);

static void
request_set_cursor_handler(struct wl_listener *listener, void *data);

static void
surface_destroy_handler(struct wl_listener *listener, void *data);

static void
cursor_touch_down_handler(struct wl_listener *listener, void *data);

static void
cursor_touch_up_handler(struct wl_listener *listener, void *data);

static void
cursor_touch_motion_handler(struct wl_listener *listener, void *data);

static void
cursor_touch_cancel_handler(struct wl_listener *listener, void *data);

static void
cursor_touch_frame_handler(struct wl_listener *listener, void *data);

static void
cursor_swipe_begin_handler(struct wl_listener *listener, void *data);

static void
cursor_swipe_update_handler(struct wl_listener *listener, void *data);

static void
cursor_swipe_end_handler(struct wl_listener *listener, void *data);

static void
cursor_pinch_begin_handler(struct wl_listener *listener, void *data);

static void
cursor_pinch_update_handler(struct wl_listener *listener, void *data);

static void
cursor_pinch_end_handler(struct wl_listener *listener, void *data);

static void
cursor_hold_begin_handler(struct wl_listener *listener, void *data);

static void
cursor_hold_end_handler(struct wl_listener *listener, void *data);

static unsigned long
get_cursor_size(void)
{
  const char *cursor_size = getenv("XCURSOR_SIZE");

  if (cursor_size != NULL && strlen(cursor_size) > 0) {
    errno = 0;
    char *end;
    unsigned long size = strtoul(cursor_size, &end, 10);
    if (*end == '\0' && errno == 0) {
      return size;
    }
  }

  setenv("XCURSOR_SIZE", "16", 1);

  return 16;
}

static const char *
get_cursor_theme(void)
{
  char *cursor_theme = getenv("XCURSOR_THEME");

  if (cursor_theme != NULL && strlen(cursor_theme) > 0) {
    return cursor_theme;
  }

  setenv("XCURSOR_THEME", "Adwaita", 1);

  return "Adwaita";
}

static void
configure_bindings(struct hikari_cursor *cursor, struct wl_list *bindings)
{
  int nr[256] = { 0 };
  struct hikari_binding_config *binding_config;
  wl_list_for_each (binding_config, bindings, link) {
    nr[binding_config->key.modifiers]++;
  }

  for (int mask = 0; mask < 256; mask++) {
    cursor->bindings[mask].nbindings = nr[mask];
    if (nr[mask] != 0) {
      cursor->bindings[mask].bindings =
          hikari_calloc(nr[mask], sizeof(struct hikari_binding));
    } else {
      cursor->bindings[mask].bindings = NULL;
    }

    nr[mask] = 0;
  }

  wl_list_for_each (binding_config, bindings, link) {
    uint8_t mask = binding_config->key.modifiers;
    struct hikari_binding *binding = &cursor->bindings[mask].bindings[nr[mask]];

    binding->action = &binding_config->action;

    switch (binding_config->key.type) {
      case HIKARI_ACTION_BINDING_KEY_KEYCODE:
        binding->keycode = binding_config->key.value.keycode;
        break;

      case HIKARI_ACTION_BINDING_KEY_KEYSYM:
        assert(false);
        break;
    }

    nr[mask]++;
  }
}

// Touch and Gesture Handlers

// [COMMENT] Function purpose: Release whatever mode-level "click" the
// primary touch point started (focus/raise, or an in-progress move/resize
// drag), so a touch_up or touch_cancel can never leave the mode state
// machine stuck waiting for a release that will never arrive.
static void
release_primary_touch(struct hikari_cursor *cursor, uint32_t time_msec)
{
  cursor->has_primary_touch = false;

  struct wlr_pointer_button_event button_event = {
    .pointer = NULL,
    .time_msec = time_msec,
    .button = BTN_LEFT,
    .state = WL_POINTER_BUTTON_STATE_RELEASED,
  };

  hikari_server.mode->button_handler(cursor, &button_event);
}

void
hikari_cursor_release_primary_touch(
    struct hikari_cursor *cursor, struct wlr_touch *touch)
{
  if (cursor->has_primary_touch && cursor->primary_touch_device == touch) {
    release_primary_touch(cursor, 0);
  }
}

static void
cursor_touch_down_handler(struct wl_listener *listener, void *data)
{
  struct wlr_touch_down_event *event = data;
  struct wlr_surface *surface;
  struct hikari_workspace *workspace;
  struct hikari_cursor *cursor = &hikari_server.cursor;
  double lx, ly;
  double sx, sy;

  // [COMMENT] Action purpose: wlr_touch_down_event x/y are normalized 0..1
  // device coordinates, not layout pixels; convert before hit-testing.
  wlr_cursor_absolute_to_layout_coords(
      cursor->wlr_cursor, &event->touch->base, event->x, event->y, &lx, &ly);

  hikari_server_node_at(lx, ly, &surface, &workspace, &sx, &sy);
  // Action purpose: Only notify a touch-down when the hit test actually
  // found a surface -- there is nothing meaningful to deliver a down event
  // to otherwise (e.g. a touch that starts on bare wallpaper).
  if (surface != NULL) {
    wlr_seat_touch_notify_down(
        hikari_server.seat, surface, event->time_msec, event->touch_id, sx, sy);
  }

  // [COMMENT] Action purpose: the first finger of a fresh multi-touch
  // sequence also drives hikari's own focus/raise/move/resize bookkeeping,
  // exactly like a left mouse click, reusing the mode state machine as-is.
  // The client still receives the real wl_touch events sent above.
  // Additional simultaneous fingers stay pure client-forwarded multi-touch.
  if (!cursor->has_primary_touch) {
    cursor->has_primary_touch = true;
    cursor->primary_touch_id = event->touch_id;
    cursor->primary_touch_device = event->touch;

    wlr_cursor_warp(cursor->wlr_cursor, &event->touch->base, lx, ly);

    struct wlr_pointer_button_event button_event = {
      .pointer = NULL,
      .time_msec = event->time_msec,
      .button = BTN_LEFT,
      .state = WL_POINTER_BUTTON_STATE_PRESSED,
    };

    hikari_server.mode->button_handler(cursor, &button_event);
  }
}

static void
cursor_touch_up_handler(struct wl_listener *listener, void *data)
{
  struct wlr_touch_up_event *event = data;
  struct hikari_cursor *cursor = &hikari_server.cursor;

  wlr_seat_touch_notify_up(hikari_server.seat, event->time_msec, event->touch_id);

  if (cursor->has_primary_touch
      && event->touch_id == cursor->primary_touch_id) {
    release_primary_touch(cursor, event->time_msec);
  }
}

static void
cursor_touch_motion_handler(struct wl_listener *listener, void *data)
{
  struct wlr_touch_motion_event *event = data;
  struct wlr_surface *surface;
  struct hikari_workspace *workspace;
  struct hikari_cursor *cursor = &hikari_server.cursor;
  double lx, ly;
  double sx, sy;

  // [COMMENT] Action purpose: wlr_touch_motion_event x/y are normalized 0..1
  // device coordinates, not layout pixels; convert before hit-testing.
  wlr_cursor_absolute_to_layout_coords(
      cursor->wlr_cursor, &event->touch->base, event->x, event->y, &lx, &ly);

  hikari_server_node_at(lx, ly, &surface, &workspace, &sx, &sy);
  wlr_seat_touch_notify_motion(
      hikari_server.seat, event->time_msec, event->touch_id, sx, sy);

  if (cursor->has_primary_touch
      && event->touch_id == cursor->primary_touch_id) {
    wlr_cursor_warp(cursor->wlr_cursor, &event->touch->base, lx, ly);
    hikari_server.mode->cursor_move(event->time_msec);
  }
}

static void
cursor_touch_cancel_handler(struct wl_listener *listener, void *data)
{
  struct wlr_touch_cancel_event *event = data;
  struct hikari_cursor *cursor = &hikari_server.cursor;

  // [COMMENT] Action purpose: wlr_seat_touch_notify_cancel takes the
  // wlr_seat_client owning the touch point, not the touch_id itself.
  struct wlr_touch_point *point =
      wlr_seat_touch_get_point(hikari_server.seat, event->touch_id);

  if (point != NULL) {
    wlr_seat_touch_notify_cancel(hikari_server.seat, point->client);
  }

  if (cursor->has_primary_touch
      && event->touch_id == cursor->primary_touch_id) {
    release_primary_touch(cursor, event->time_msec);
  }
}

static void
cursor_touch_frame_handler(struct wl_listener *listener, void *data)
{
  wlr_seat_touch_notify_frame(hikari_server.seat);
}

// [COMMENT] Function purpose: Look up a configured gestures{} action for a
// classified (type, direction, fingers) triple.
static struct hikari_gesture_binding_config *
find_gesture_binding(enum hikari_gesture_type type,
    enum hikari_gesture_direction direction,
    uint32_t fingers)
{
  struct hikari_gesture_binding_config *binding_config;

  wl_list_for_each (binding_config,
      &hikari_configuration->gesture_binding_configs,
      link) {
    if (binding_config->type == type && binding_config->direction == direction
        && binding_config->fingers == fingers) {
      return binding_config;
    }
  }

  return NULL;
}

// Function purpose: Invoke the action bound to a matched gesture, mirroring
// how a matched key/mouse binding is dispatched.
static void
fire_gesture_binding(struct hikari_gesture_binding_config *binding_config)
{
  /* Every other live input path (keyboard, pointer button/motion, touch --
  translated to synthetic pointer events) dispatches through
  hikari_server.mode's vtable, which lock_mode overrides to no-ops while
  locked. Gesture recognition never joined that vtable -- it called
  configured actions directly -- so a configured gesture binding fired
  regardless of lock state, the one input modality that could act on an
  unauthenticated locked screen. */
  if (hikari_server_in_lock_mode()) {
    return;
  }

  struct hikari_event_action *event_action = &binding_config->action.begin;

  if (event_action->action != NULL) {
    event_action->action(event_action->arg);
  }
}

// Function purpose: Classify a completed swipe's accumulated displacement
// into one of the four cardinal directions by comparing the dominant axis.
static enum hikari_gesture_direction
classify_swipe_direction(double dx, double dy)
{
  if (fabs(dx) >= fabs(dy)) {
    return dx >= 0 ? HIKARI_GESTURE_DIRECTION_RIGHT : HIKARI_GESTURE_DIRECTION_LEFT;
  } else {
    return dy >= 0 ? HIKARI_GESTURE_DIRECTION_DOWN : HIKARI_GESTURE_DIRECTION_UP;
  }
}

// Action purpose: Minimum accumulated displacement (in the same units as
// wlr_pointer_swipe_update_event's dx/dy) before a swipe is classified and
// matched against a binding. Without this, an incidental few-pixel wobble
// from a 3+ finger tap or hold would classify as a directional swipe and
// could fire a bound action the user never intended to trigger.
#define HIKARI_GESTURE_SWIPE_MIN_DISTANCE 20.0

// Action purpose: Minimum |scale - 1.0| before a pinch is classified and
// matched against a binding, for the same reason as the swipe threshold
// above -- negligible scale drift during an otherwise-stationary gesture
// should not be treated as a deliberate pinch-in/out.
#define HIKARI_GESTURE_PINCH_MIN_SCALE_DELTA 0.1

// [COMMENT] Action purpose: wlroots reports pinch scale relative to 1.0 at
// gesture start; >= 1.0 means fingers spread apart (pinch-out), < 1.0 means
// fingers moved together (pinch-in).
static enum hikari_gesture_direction
classify_pinch_direction(double scale)
{
  return scale >= 1.0 ? HIKARI_GESTURE_DIRECTION_OUT : HIKARI_GESTURE_DIRECTION_IN;
}

// Function purpose: Start accumulating a new gesture stream (swipe, pinch,
// or hold), resetting the running totals and update buffer from any prior
// gesture.
static void
gesture_begin(struct hikari_gesture_state *state,
    enum hikari_gesture_type type,
    uint32_t fingers,
    uint32_t time_msec)
{
  state->active = true;
  state->type = type;
  state->fingers = fingers;
  state->begin_time_msec = time_msec;
  state->total_dx = 0;
  state->total_dy = 0;
  state->last_scale = 1.0;
  state->nupdates = 0;
}

// Function purpose: Append one wlroots update event to the in-progress
// gesture's replay buffer, so it can be forwarded to the client verbatim if
// the gesture turns out not to match a binding. Silently drops updates
// beyond HIKARI_GESTURE_MAX_UPDATES rather than growing without bound.
static void
gesture_buffer_update(struct hikari_gesture_state *state,
    uint32_t time_msec,
    double dx,
    double dy,
    double scale,
    double rotation)
{
  if (state->nupdates >= HIKARI_GESTURE_MAX_UPDATES) {
    return;
  }

  struct hikari_gesture_update *update = &state->updates[state->nupdates++];
  update->time_msec = time_msec;
  update->dx = dx;
  update->dy = dy;
  update->scale = scale;
  update->rotation = rotation;
}

// [COMMENT] Function purpose: Replay a buffered, unmatched gesture stream to
// the focused client exactly as wlroots reported it, since the begin/update
// events were withheld until the end event revealed whether a binding
// claimed the gesture instead.
static void
replay_swipe(struct hikari_gesture_state *state, bool cancelled, uint32_t end_time_msec)
{
  struct wlr_pointer_gestures_v1 *gestures = hikari_server.pointer_gestures;
  struct wlr_seat *seat = hikari_server.seat;

  wlr_pointer_gestures_v1_send_swipe_begin(
      gestures, seat, state->begin_time_msec, state->fingers);

  for (int i = 0; i < state->nupdates; i++) {
    struct hikari_gesture_update *update = &state->updates[i];
    wlr_pointer_gestures_v1_send_swipe_update(
        gestures, seat, update->time_msec, update->dx, update->dy);
  }

  wlr_pointer_gestures_v1_send_swipe_end(gestures, seat, end_time_msec, cancelled);
}

static void
replay_pinch(struct hikari_gesture_state *state, bool cancelled, uint32_t end_time_msec)
{
  struct wlr_pointer_gestures_v1 *gestures = hikari_server.pointer_gestures;
  struct wlr_seat *seat = hikari_server.seat;

  wlr_pointer_gestures_v1_send_pinch_begin(
      gestures, seat, state->begin_time_msec, state->fingers);

  for (int i = 0; i < state->nupdates; i++) {
    struct hikari_gesture_update *update = &state->updates[i];
    wlr_pointer_gestures_v1_send_pinch_update(gestures,
        seat,
        update->time_msec,
        update->dx,
        update->dy,
        update->scale,
        update->rotation);
  }

  wlr_pointer_gestures_v1_send_pinch_end(gestures, seat, end_time_msec, cancelled);
}

static void
replay_hold(struct hikari_gesture_state *state, bool cancelled, uint32_t end_time_msec)
{
  struct wlr_pointer_gestures_v1 *gestures = hikari_server.pointer_gestures;
  struct wlr_seat *seat = hikari_server.seat;

  wlr_pointer_gestures_v1_send_hold_begin(
      gestures, seat, state->begin_time_msec, state->fingers);
  wlr_pointer_gestures_v1_send_hold_end(gestures, seat, end_time_msec, cancelled);
}

static void
cursor_swipe_begin_handler(struct wl_listener *listener, void *data)
{
  struct wlr_pointer_swipe_begin_event *event = data;

  gesture_begin(&hikari_server.cursor.gesture_state,
      HIKARI_GESTURE_SWIPE,
      event->fingers,
      event->time_msec);
}

static void
cursor_swipe_update_handler(struct wl_listener *listener, void *data)
{
  struct wlr_pointer_swipe_update_event *event = data;
  struct hikari_gesture_state *state = &hikari_server.cursor.gesture_state;

  if (!state->active) {
    return;
  }

  state->total_dx += event->dx;
  state->total_dy += event->dy;

  gesture_buffer_update(state, event->time_msec, event->dx, event->dy, 0, 0);
}

static void
cursor_swipe_end_handler(struct wl_listener *listener, void *data)
{
  struct wlr_pointer_swipe_end_event *event = data;
  struct hikari_gesture_state *state = &hikari_server.cursor.gesture_state;

  if (!state->active) {
    return;
  }
  state->active = false;

  double distance =
      hypot(state->total_dx, state->total_dy);

  if (!event->cancelled && distance >= HIKARI_GESTURE_SWIPE_MIN_DISTANCE) {
    enum hikari_gesture_direction direction =
        classify_swipe_direction(state->total_dx, state->total_dy);

    struct hikari_gesture_binding_config *binding_config =
        find_gesture_binding(HIKARI_GESTURE_SWIPE, direction, state->fingers);

    if (binding_config != NULL) {
      fire_gesture_binding(binding_config);
      return;
    }
  }

  replay_swipe(state, event->cancelled, event->time_msec);
}

static void
cursor_pinch_begin_handler(struct wl_listener *listener, void *data)
{
  struct wlr_pointer_pinch_begin_event *event = data;

  gesture_begin(&hikari_server.cursor.gesture_state,
      HIKARI_GESTURE_PINCH,
      event->fingers,
      event->time_msec);
}

static void
cursor_pinch_update_handler(struct wl_listener *listener, void *data)
{
  struct wlr_pointer_pinch_update_event *event = data;
  struct hikari_gesture_state *state = &hikari_server.cursor.gesture_state;

  if (!state->active) {
    return;
  }

  state->last_scale = event->scale;

  gesture_buffer_update(
      state, event->time_msec, event->dx, event->dy, event->scale, event->rotation);
}

static void
cursor_pinch_end_handler(struct wl_listener *listener, void *data)
{
  struct wlr_pointer_pinch_end_event *event = data;
  struct hikari_gesture_state *state = &hikari_server.cursor.gesture_state;

  if (!state->active) {
    return;
  }
  state->active = false;

  if (!event->cancelled &&
      fabs(state->last_scale - 1.0) >= HIKARI_GESTURE_PINCH_MIN_SCALE_DELTA) {
    enum hikari_gesture_direction direction =
        classify_pinch_direction(state->last_scale);

    struct hikari_gesture_binding_config *binding_config =
        find_gesture_binding(HIKARI_GESTURE_PINCH, direction, state->fingers);

    if (binding_config != NULL) {
      fire_gesture_binding(binding_config);
      return;
    }
  }

  replay_pinch(state, event->cancelled, event->time_msec);
}

static void
cursor_hold_begin_handler(struct wl_listener *listener, void *data)
{
  struct wlr_pointer_hold_begin_event *event = data;

  gesture_begin(&hikari_server.cursor.gesture_state,
      HIKARI_GESTURE_HOLD,
      event->fingers,
      event->time_msec);
}

static void
cursor_hold_end_handler(struct wl_listener *listener, void *data)
{
  struct wlr_pointer_hold_end_event *event = data;
  struct hikari_gesture_state *state = &hikari_server.cursor.gesture_state;

  if (!state->active) {
    return;
  }
  state->active = false;

  if (!event->cancelled) {
    struct hikari_gesture_binding_config *binding_config = find_gesture_binding(
        HIKARI_GESTURE_HOLD, HIKARI_GESTURE_DIRECTION_NONE, state->fingers);

    if (binding_config != NULL) {
      fire_gesture_binding(binding_config);
      return;
    }
  }

  replay_hold(state, event->cancelled, event->time_msec);
}

// [COMMENT] Function purpose: Initialize the cursor, attach it to the output layout, and pre-load xcursor themes.
void
hikari_cursor_init(
    struct hikari_cursor *cursor, struct wlr_output_layout *output_layout)
{
  struct wlr_cursor *wlr_cursor = wlr_cursor_create();

  wlr_cursor_attach_output_layout(wlr_cursor, output_layout);

  const char *cursor_theme = get_cursor_theme();
  unsigned long cursor_size = get_cursor_size();

  cursor->cursor_mgr = wlr_xcursor_manager_create(cursor_theme, cursor_size);

  // [COMMENT] Action purpose: Pre-load xcursor images at integer scales 1 and 2.
// Scale 1 is required for all SDR displays. Scale 2 covers the majority of
// HiDPI deployments. Additional scales will be loaded per-output in
// hikari_output_init when the actual wlr_output->scale value is available,
// ensuring correct cursor sharpness at all display densities.
  wlr_xcursor_manager_load(cursor->cursor_mgr, 1);
  wlr_xcursor_manager_load(cursor->cursor_mgr, 2);

  cursor->wlr_cursor = wlr_cursor;

  cursor->has_primary_touch = false;
  cursor->gesture_state.active = false;

  wl_list_init(&cursor->surface_destroy.link);
  hikari_binding_group_init(cursor->bindings);
}

void
hikari_cursor_configure_bindings(
    struct hikari_cursor *cursor, struct wl_list *bindings)
{
  hikari_binding_group_fini(cursor->bindings);
  hikari_binding_group_init(cursor->bindings);
  configure_bindings(cursor, bindings);
}

void
hikari_cursor_fini(struct hikari_cursor *cursor)
{
  hikari_binding_group_fini(cursor->bindings);
  hikari_cursor_deactivate(cursor);

  wlr_xcursor_manager_destroy(cursor->cursor_mgr);
}

void
hikari_cursor_activate(struct hikari_cursor *cursor)
{
  struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;

  cursor->motion_absolute.notify = motion_absolute_handler;
  wl_signal_add(&wlr_cursor->events.motion_absolute, &cursor->motion_absolute);

  cursor->frame.notify = frame_handler;
  wl_signal_add(&wlr_cursor->events.frame, &cursor->frame);

  cursor->motion.notify = motion_handler;
  wl_signal_add(&wlr_cursor->events.motion, &cursor->motion);

  cursor->button.notify = button_handler;
  wl_signal_add(&wlr_cursor->events.button, &cursor->button);

  cursor->axis.notify = axis_handler;
  wl_signal_add(&wlr_cursor->events.axis, &cursor->axis);

  cursor->request_set_cursor.notify = request_set_cursor_handler;
  wl_signal_add(&hikari_server.seat->events.request_set_cursor,
      &cursor->request_set_cursor);

  cursor->touch_down.notify = cursor_touch_down_handler;
  wl_signal_add(&wlr_cursor->events.touch_down, &cursor->touch_down);

  cursor->touch_up.notify = cursor_touch_up_handler;
  wl_signal_add(&wlr_cursor->events.touch_up, &cursor->touch_up);

  cursor->touch_motion.notify = cursor_touch_motion_handler;
  wl_signal_add(&wlr_cursor->events.touch_motion, &cursor->touch_motion);

  cursor->touch_cancel.notify = cursor_touch_cancel_handler;
  wl_signal_add(&wlr_cursor->events.touch_cancel, &cursor->touch_cancel);

  cursor->touch_frame.notify = cursor_touch_frame_handler;
  wl_signal_add(&wlr_cursor->events.touch_frame, &cursor->touch_frame);

  cursor->swipe_begin.notify = cursor_swipe_begin_handler;
  wl_signal_add(&wlr_cursor->events.swipe_begin, &cursor->swipe_begin);

  cursor->swipe_update.notify = cursor_swipe_update_handler;
  wl_signal_add(&wlr_cursor->events.swipe_update, &cursor->swipe_update);

  cursor->swipe_end.notify = cursor_swipe_end_handler;
  wl_signal_add(&wlr_cursor->events.swipe_end, &cursor->swipe_end);

  cursor->pinch_begin.notify = cursor_pinch_begin_handler;
  wl_signal_add(&wlr_cursor->events.pinch_begin, &cursor->pinch_begin);

  cursor->pinch_update.notify = cursor_pinch_update_handler;
  wl_signal_add(&wlr_cursor->events.pinch_update, &cursor->pinch_update);

  cursor->pinch_end.notify = cursor_pinch_end_handler;
  wl_signal_add(&wlr_cursor->events.pinch_end, &cursor->pinch_end);

  cursor->hold_begin.notify = cursor_hold_begin_handler;
  wl_signal_add(&wlr_cursor->events.hold_begin, &cursor->hold_begin);

  cursor->hold_end.notify = cursor_hold_end_handler;
  wl_signal_add(&wlr_cursor->events.hold_end, &cursor->hold_end);

  hikari_cursor_reset_image(cursor);
}

void
hikari_cursor_deactivate(struct hikari_cursor *cursor)
{
  wl_list_remove(&cursor->motion_absolute.link);
  wl_list_remove(&cursor->frame.link);
  wl_list_remove(&cursor->motion.link);
  wl_list_remove(&cursor->button.link);
  wl_list_remove(&cursor->axis.link);
  wl_list_remove(&cursor->request_set_cursor.link);
  wl_list_remove(&cursor->touch_down.link);
  wl_list_remove(&cursor->touch_up.link);
  wl_list_remove(&cursor->touch_motion.link);
  wl_list_remove(&cursor->touch_cancel.link);
  wl_list_remove(&cursor->touch_frame.link);
  wl_list_remove(&cursor->swipe_begin.link);
  wl_list_remove(&cursor->swipe_update.link);
  wl_list_remove(&cursor->swipe_end.link);
  wl_list_remove(&cursor->pinch_begin.link);
  wl_list_remove(&cursor->pinch_update.link);
  wl_list_remove(&cursor->pinch_end.link);
  wl_list_remove(&cursor->hold_begin.link);
  wl_list_remove(&cursor->hold_end.link);

  // Action purpose: A held primary touch point is driving mode-level state
  // (focus/raise, or an in-progress move/resize drag) via a synthetic
  // BTN_LEFT press. Deactivation happens mid-session (e.g. entering lock
  // mode, which overwrites hikari_server.mode right after this call) --
  // release it through the still-active mode's button_handler now, or that
  // mode is left believing a button is still held with no touch event ever
  // able to release it.
  if (cursor->has_primary_touch) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint32_t time_msec =
        (uint32_t)(now.tv_sec * 1000LL + now.tv_nsec / 1000000LL);
    release_primary_touch(cursor, time_msec);
  }

  // Action purpose: Clear any in-progress gesture. Deactivation happens
  // mid-session (e.g. entering lock mode); without this, a gesture left
  // "active" here would resume against stale state (or never reset) once
  // the cursor is reactivated.
  cursor->gesture_state.active = false;

  hikari_cursor_set_image(cursor, NULL);
}

// [COMMENT] Function purpose: Set cursor image from xcursor theme or clear it.
void
hikari_cursor_set_image(struct hikari_cursor *cursor, const char *path)
{
  // [COMMENT] Action purpose: Remove existing surface destroy listener before resetting the cursor.
  wl_list_remove(&cursor->surface_destroy.link);
  // [COMMENT] Action purpose: Reinitialize the listener list link to prevent use-after-free on disconnect.
  wl_list_init(&cursor->surface_destroy.link);
  // [COMMENT] Action purpose: Check if valid cursor path provided.
  if (path != NULL) {
   // [COMMENT] Action purpose: Set the hardware/software cursor to the requested xcursor image.
   wlr_cursor_set_xcursor(cursor->wlr_cursor, cursor->cursor_mgr, path);
  } else {
   // [COMMENT] Action purpose: Clear the cursor image by unsetting the wlr_surface.
   wlr_cursor_set_surface(cursor->wlr_cursor, NULL, 0, 0);
  }
}

/* [COMMENT] Function purpose: The single funnel for every warp the compositor
performs on its own initiative. Verified: hikari_cursor_center(),
hikari_view_center_cursor(), hikari_view_top_left_cursor(),
hikari_view_bottom_right_cursor() and hikari_workspace_center_cursor() all reach
wlr_cursor_warp() through here and nothing bypasses it, which is what lets one
guard cover the whole tree. */
void
hikari_cursor_warp(struct hikari_cursor *cursor, int x, int y)
{
  /* [COMMENT] Action purpose: A warp that sends the pointer somewhere else ends
  a client's hold on it -- D-028 Ruling 8, the user's ruling that window
  management behaves identically whether or not a client is holding the mouse.

  Deactivating BEFORE the move is what keeps the two in step. Moving the cursor
  while the client still believed itself locked would leave it reporting a
  captured pointer that no longer matches reality, which is the desynchronised
  state this whole feature exists to eliminate. No cursor hint is honoured: the
  caller already has a destination in mind and the hint would fight it.

  Cosmetic recentring after a geometry change does NOT reach here -- those three
  call sites in src/view.c are gated on the constraint instead, because nobody
  asked for the pointer to go anywhere. D-028 Ruling 9. */
  hikari_pointer_constraint_break_for_warp();

  wlr_cursor_warp(cursor->wlr_cursor, NULL, x, y);
}

void
hikari_cursor_center(struct hikari_cursor *cursor,
    struct hikari_output *output,
    struct wlr_box *geometry)
{
  int x = output->geometry.x + geometry->x + geometry->width / 2;
  int y = output->geometry.y + geometry->y + geometry->height / 2;

  hikari_cursor_warp(cursor, x, y);
}

// [COMMENT] Function purpose: Handle absolute pointer motion events and warp the cursor
// to the reported absolute position on the output.
static void
motion_absolute_handler(struct wl_listener *listener, void *data)
{
  struct hikari_cursor *cursor =
      wl_container_of(listener, cursor, motion_absolute);

  assert(!hikari_server_in_lock_mode());

  struct wlr_pointer_motion_absolute_event *event = data;

  /* [COMMENT] Action purpose: A locked pointer must not be teleported by an
  absolute device -- a tablet, a touchscreen, or a nested backend's pointer. Easy
  to overlook because the symptom only appears on hardware the developer may not
  have, and it would defeat the lock completely on hardware that does.

  No relative motion is emitted from here: this event carries an absolute
  position and no delta, and inventing one from the cursor's last position would
  hand the client a jump rather than a movement. */
  if (hikari_pointer_constraint_is_locked()) {
    return;
  }

  /* [COMMENT] Action purpose: A CONFINED pointer has to be clamped here too, or
  an absolute device walks straight out of the region that a relative one is held
  inside. The lock guard above is not enough on its own: confinement is the
  looser constraint and it is the one an absolute device can defeat silently.

  The event carries a position rather than a delta, so the delta is recovered by
  converting to layout coordinates first -- the same conversion the touch
  handlers do -- and subtracting where the cursor is now. That is a real
  displacement rather than an invented one, which is what the clamp needs. */
  double lx, ly;
  wlr_cursor_absolute_to_layout_coords(
      cursor->wlr_cursor, &event->pointer->base, event->x, event->y, &lx, &ly);

  double confined_x, confined_y;

  if (hikari_pointer_constraint_confine(lx - cursor->wlr_cursor->x,
          ly - cursor->wlr_cursor->y,
          &confined_x,
          &confined_y)) {
    wlr_cursor_warp_closest(
        cursor->wlr_cursor, &event->pointer->base, confined_x, confined_y);
  } else {
    wlr_cursor_warp_absolute(
        cursor->wlr_cursor, &event->pointer->base, event->x, event->y);
  }

  hikari_server.mode->cursor_move(event->time_msec);
}

static void
frame_handler(struct wl_listener *listener, void *data)
{
  assert(!hikari_server_in_lock_mode());

  wlr_seat_pointer_notify_frame(hikari_server.seat);
}

// [COMMENT] Function purpose: Handle relative pointer motion events and move the cursor
// by the reported delta values.
static void
motion_handler(struct wl_listener *listener, void *data)
{
  struct hikari_cursor *cursor = wl_container_of(listener, cursor, motion);

  assert(!hikari_server_in_lock_mode());

  struct wlr_pointer_motion_event *event = data;

  /* [COMMENT] Action purpose: Hand the client the raw movement BEFORE the cursor
  is touched, and hand it over unconditionally rather than only while something
  is constrained. That is what the protocol asks for, and it is what makes
  mouse-look work in a window that has locked nothing.

  Three details, each a defect if missed. The timestamp is in MICROSECONDS, where
  everything else in this file is milliseconds. The unaccelerated pair is a
  genuinely different measurement from the accelerated one and libinput reports
  both -- passing delta_x/delta_y twice would silently give every client
  pointer acceleration it explicitly asked to avoid. And the manager is NULL when
  its global could not be created, which this runs against on every motion event.

  wlroots delivers this only to the seat's pointer-FOCUSED client and drops it
  silently otherwise, which is why the locked path below must return before
  anything can move focus. */
  if (hikari_server.relative_pointer != NULL) {
    wlr_relative_pointer_manager_v1_send_relative_motion(
        hikari_server.relative_pointer,
        hikari_server.seat,
        (uint64_t)event->time_msec * 1000,
        event->delta_x,
        event->delta_y,
        event->unaccel_dx,
        event->unaccel_dy);
  }

  /* [COMMENT] Action purpose: A locked pointer does not move, and this early
  return is the whole of that behaviour.

  It is also what preserves focus-follows-mouse untouched. Returning here means
  wlr_cursor_move() never runs, so the cursor never moves, so mode->cursor_move()
  has nothing to re-derive -- and cursor_move() in src/normal_mode.c is the only
  thing in the tree that refocuses on hover. No hit test, no refocus, no
  wlr_seat_pointer_clear_focus(). The behaviour falls out of the cursor standing
  still rather than from a special case, which is why src/normal_mode.c needs no
  test for this state and is unchanged outside a held lock.

  Keeping pointer focus is not incidental either: wlroots drops relative motion
  for a seat with no focused client, so a path that let focus slip would leave
  the client holding a lock that reports zero movement for ever. */
  if (hikari_pointer_constraint_is_locked()) {
    return;
  }

  /* [COMMENT] Action purpose: A confined pointer moves freely but cannot leave
  the client's region -- the looser of the two constraints, and what a windowed
  strategy game or a remote-desktop client asks for.

  Warped rather than moved, because the clamp yields an absolute destination
  rather than a delta, and warped through wlr_cursor directly rather than
  hikari_cursor_warp() because this is the constraint being HONOURED. Going
  through the compositor's own warp funnel would take the constraint down on
  every single motion event, which is the exact opposite of the intent.

  mode->cursor_move() still runs below either way: unlike a locked pointer, a
  confined one is visible, moves, and must keep updating focus and the client's
  own pointer position. */
  double confined_x, confined_y;

  if (hikari_pointer_constraint_confine(
          event->delta_x, event->delta_y, &confined_x, &confined_y)) {
    wlr_cursor_warp_closest(
        cursor->wlr_cursor, &event->pointer->base, confined_x, confined_y);
  } else {
    wlr_cursor_move(cursor->wlr_cursor,
        &event->pointer->base,
        event->delta_x,
        event->delta_y);
  }

  hikari_server.mode->cursor_move(event->time_msec);
}

// [COMMENT] Function purpose: Handle pointer button press/release events and dispatch
// to the current mode's button handler.
static void
button_handler(struct wl_listener *listener, void *data)
{
  assert(!hikari_server_in_lock_mode());

  struct hikari_cursor *cursor = wl_container_of(listener, cursor, button);
  struct wlr_pointer_button_event *event = data;

  hikari_server.mode->button_handler(cursor, event);
}

// [COMMENT] Function purpose: Handle pointer axis (scroll wheel) events and notify seat with relative direction.
static void
axis_handler(struct wl_listener *listener, void *data)
{
  assert(!hikari_server_in_lock_mode());

  struct wlr_pointer_axis_event *event = data;

  wlr_seat_pointer_notify_axis(hikari_server.seat,
      event->time_msec,
      event->orientation,
      event->delta,
      event->delta_discrete,
      event->source,
      event->relative_direction);
}

static void
request_set_cursor_handler(struct wl_listener *listener, void *data)
{
  if (!hikari_server_in_normal_mode()) {
    return;
  }

  struct hikari_cursor *cursor =
      wl_container_of(listener, cursor, request_set_cursor);

  struct hikari_server *server = &hikari_server;
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  struct wlr_seat *seat = server->seat;

  struct wl_client *focused_client = NULL;
  struct wlr_surface *focused_surface = seat->pointer_state.focused_surface;
  if (focused_surface != NULL) {
    focused_client = wl_resource_get_client(focused_surface->resource);
  }

  if (focused_client == NULL || event->seat_client->client != focused_client) {
    return;
  }

  struct wlr_surface *surface = event->surface;

  wl_list_remove(&cursor->surface_destroy.link);
  if (surface != NULL) {
    cursor->surface_destroy.notify = surface_destroy_handler;
    wl_signal_add(&surface->events.destroy, &cursor->surface_destroy);
  } else {
    wl_list_init(&cursor->surface_destroy.link);
  }

  wlr_cursor_set_surface(
      cursor->wlr_cursor, surface, event->hotspot_x, event->hotspot_y);
}

static void
surface_destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_cursor *cursor =
      wl_container_of(listener, cursor, surface_destroy);

  hikari_cursor_reset_image(cursor);
}
