// [COMMENT] Script function and purpose: Hikari cursor input handling and pointer axis event dispatching.

#include <hikari/cursor.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <time.h>

#include <hikari/evdev.h>

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

/* Touchscreen gesture recognition. Defined further down, beside the touchpad
gesture code it shares its classifiers and binding lookup with, but called
from the touch handlers above it. */
static bool
touch_gesture_down(int32_t id, double lx, double ly, uint32_t time_msec);

static void
touch_gesture_motion(int32_t id, double lx, double ly, uint32_t time_msec);

static void
touch_gesture_up(int32_t id, uint32_t time_msec);

static void
touch_gesture_reset(void);

static void
touch_cancel_client_sequences(struct hikari_touch_gesture_state *state);

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

/* [COMMENT] Function purpose: Whether the client owning this surface has
bound wl_touch, and so is already receiving the real touch sequence.

This is what decides whether a touch contact is also delivered as pointer
input. A client with touch support that is sent both sees one finger twice:
GTK, Qt, Firefox and Chromium all implement wl_touch and wl_pointer, and a
single tap arriving as a touch and a click can activate a control twice. A
client without touch support gets nothing at all from a touchscreen unless the
pointer is synthesised for it, so for those it still is.

The same test wlroots itself applies before it will create a touch point at
all -- touch_point_create() in types/seat/wlr_seat_touch.c returns NULL when
`wl_list_empty(&client->touches)`, "touch points are not valid without a
connected client with touch" -- so the two halves agree by construction rather
than by a heuristic of our own that could drift from it. */
static bool
surface_client_has_touch(struct wlr_surface *surface)
{
  if (surface == NULL) {
    return false;
  }

  struct wlr_seat_client *seat_client = wlr_seat_client_for_wl_client(
      hikari_server.seat, wl_resource_get_client(surface->resource));

  return seat_client != NULL && !wl_list_empty(&seat_client->touches);
}

// Function purpose: Monotonic milliseconds, for the paths that must release a
// contact without an input event to take a timestamp from.
static uint32_t
touch_now_msec(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  return (uint32_t)(now.tv_sec * 1000LL + now.tv_nsec / 1000000LL);
}

/* [COMMENT] Function purpose: Begin delivering a touch contact to a client
that cannot speak wl_touch, as pointer input.

Every client-facing pointer event a finger ever produces is emitted by these
three functions and nowhere else. That is the whole point of the arrangement:
the modes run window management and emit nothing, so a client is reached by
exactly one path and a finger cannot be delivered twice.

The order is the protocol's, not a convenience. wl_pointer.button carries no
coordinates -- a client applies it wherever it last saw the pointer -- so the
enter and the motion have to precede it or the tap lands at the previous tap's
position, or in whichever window the pointer happened to be left in. The enter
cannot be relied on to carry the position either: wlr_seat_pointer_enter()
returns early when the surface is unchanged, so a second tap in the same
window would emit nothing at all without the explicit motion.

Nothing here can be driven off the cursor's own motion signal, because
wlr_cursor_warp() does not emit one. */
static void
touch_pointer_begin(struct hikari_cursor *cursor,
    struct wlr_surface *surface,
    double lx,
    double ly,
    double sx,
    double sy,
    uint32_t time_msec)
{
  struct wlr_seat *seat = hikari_server.seat;

  cursor->touch_pointer_lx = lx;
  cursor->touch_pointer_ly = ly;
  cursor->touch_pointer_sx = sx;
  cursor->touch_pointer_sy = sy;

  wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
  wlr_seat_pointer_notify_motion(seat, time_msec, sx, sy);
  wlr_seat_pointer_notify_button(
      seat, time_msec, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
  wlr_seat_pointer_notify_frame(seat);
}

/* [COMMENT] Function purpose: Carry an emulated contact's movement to its
client, in the surface frame the contact started in.

Measured from the contact's own anchor rather than re-hit-testing, so a finger
dragged past the edge of the window keeps reporting into that window -- which
is what a pointer drag does, and what a client being dragged expects. This is
the same arithmetic normal mode's cursor_down_move() does for a mouse, kept
separate from it deliberately: there is one pointer but many fingers, and
sharing one anchor is what let a touch release tear down a mouse drag. */
static void
touch_pointer_motion(struct hikari_cursor *cursor, double lx, double ly,
    uint32_t time_msec)
{
  struct wlr_seat *seat = hikari_server.seat;

  double sx = cursor->touch_pointer_sx + (lx - cursor->touch_pointer_lx);
  double sy = cursor->touch_pointer_sy + (ly - cursor->touch_pointer_ly);

  wlr_seat_pointer_notify_motion(seat, time_msec, sx, sy);
  wlr_seat_pointer_notify_frame(seat);
}

/* [COMMENT] Function purpose: Release an emulated contact.

Unconditional once the contact was emulated, so the press always gets its
release. Where the seat's pointer focus has already been cleared -- lock entry
does exactly that, deliberately -- wlr_seat_pointer_send_button() returns at
once on a NULL focused_client, so this neither reaches a client nor puts focus
back, which is what the old release path got wrong. */
static void
touch_pointer_end(struct hikari_cursor *cursor, uint32_t time_msec)
{
  struct wlr_seat *seat = hikari_server.seat;

  wlr_seat_pointer_notify_button(
      seat, time_msec, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
  wlr_seat_pointer_notify_frame(seat);
}

/* [COMMENT] Function purpose: End the primary contact: release the emulated
button if there was one, and tell the mode the contact is over.

No focus refresh. The mode's touch_end does window management only, and the
client half above either pairs its own press or is a no-op because focus is
already gone. */
static void
end_primary_touch(struct hikari_cursor *cursor, uint32_t time_msec)
{
  cursor->has_primary_touch = false;

  if (cursor->touch_emulates_pointer) {
    touch_pointer_end(cursor, time_msec);
    cursor->touch_emulates_pointer = false;
  }
}

static void
release_primary_touch(struct hikari_cursor *cursor, uint32_t time_msec)
{
  end_primary_touch(cursor, time_msec);

  if (hikari_server.mode->touch_end != NULL) {
    hikari_server.mode->touch_end(cursor, time_msec);
  }
}

/* [COMMENT] Function purpose: Clean up after a touch device that has gone
away mid-touch.

wlroots emits neither up nor cancel for a device being destroyed --
wlr_touch_finish() fires only events.destroy -- so every point that device was
driving would otherwise stay down forever. For the recogniser that is fatal
rather than untidy: npoints never falls back to zero, touch_gesture_reset() is
reached only when the last finger lifts, and a sequence that had already been
claimed keeps `claimed` set, which suppresses every touch event from every
device for the rest of the session.

The recogniser tracks no device per point, so the whole sequence goes. A
second touchscreen's fingers are dropped with it, which is why their clients
are cancelled first: a point forgotten without a cancel leaves its client
holding a touch that can never end. Spoiling a concurrent gesture is a far
smaller cost than leaving touch dead. */
void
hikari_cursor_release_primary_touch(
    struct hikari_cursor *cursor, struct wlr_touch *touch)
{
  uint32_t time_msec = touch_now_msec();

  if (cursor->has_primary_touch && cursor->primary_touch_device == touch) {
    release_primary_touch(cursor, time_msec);
  }

  touch_cancel_client_sequences(&cursor->touch_gesture);
  touch_gesture_reset();
}

static void
cursor_touch_down_handler(struct wl_listener *listener, void *data)
{
  /* [COMMENT] Action purpose: A touch cannot be seen while the screen is
  locked, and nothing here has to test for it. hikari_lock_mode_enter() calls
  hikari_cursor_deactivate() -- which removes this listener -- BEFORE it
  installs lock mode, and only lock mode's cancel() reattaches, so these
  handlers are detached for the whole of a lock. The same reasoning and the
  same assertion guard the pointer handlers further down this file. */
  assert(!hikari_server_in_lock_mode());

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

  bool tracked = touch_gesture_down(event->touch_id, lx, ly, event->time_msec);

  /* [COMMENT] Action purpose: Forward the down only when the recogniser took
  the point, the hit test found something to deliver it to, and the sequence
  still belongs to the client rather than to a claimed compositor gesture.

  `tracked` is load-bearing, not caution: the suppression a claim applies is by
  sequence, so a point the recogniser could not record is one that
  touch_cancel_client_sequences() cannot find later. Sending it anyway would
  leave its client holding a touch that the claim then swallows the end of. */
  if (tracked && surface != NULL && !cursor->touch_gesture.claimed) {
    wlr_seat_touch_notify_down(
        hikari_server.seat, surface, event->time_msec, event->touch_id, sx, sy);
  }

  /* [COMMENT] Action purpose: The first contact of a fresh sequence also
  drives hikari's own window management. Additional simultaneous fingers stay
  pure client-forwarded multi-touch.

  The mode is asked first and gets to consume the contact -- that is a mouse
  binding on BTN_LEFT firing, which a touchscreen must still be able to do.
  A consumed contact is not emulated, exactly as a bound mouse press is not
  forwarded to the client either. */
  if (!cursor->has_primary_touch && !cursor->touch_gesture.claimed) {
    cursor->has_primary_touch = true;
    cursor->primary_touch_id = event->touch_id;
    cursor->primary_touch_device = event->touch;

    wlr_cursor_warp(cursor->wlr_cursor, &event->touch->base, lx, ly);

    bool consumed = false;

    if (hikari_server.mode->touch_begin != NULL) {
      consumed = hikari_server.mode->touch_begin(cursor, event->time_msec);
    }

    cursor->touch_emulates_pointer = hikari_server.mode->touch_emulates_pointer
        && !consumed && surface != NULL && !surface_client_has_touch(surface);

    if (cursor->touch_emulates_pointer) {
      touch_pointer_begin(
          cursor, surface, lx, ly, sx, sy, event->time_msec);
    }
  }
}

static void
cursor_touch_up_handler(struct wl_listener *listener, void *data)
{
  /* [COMMENT] Action purpose: A touch cannot be seen while the screen is
  locked, and nothing here has to test for it. hikari_lock_mode_enter() calls
  hikari_cursor_deactivate() -- which removes this listener -- BEFORE it
  installs lock mode, and only lock mode's cancel() reattaches, so these
  handlers are detached for the whole of a lock. The same reasoning and the
  same assertion guard the pointer handlers further down this file. */
  assert(!hikari_server_in_lock_mode());

  struct wlr_touch_up_event *event = data;
  struct hikari_cursor *cursor = &hikari_server.cursor;

  if (!cursor->touch_gesture.claimed) {
    wlr_seat_touch_notify_up(
        hikari_server.seat, event->time_msec, event->touch_id);
  }

  /* After the forwarding above, because this is where a hold is recognised and
  where the sequence is reset once the last finger lifts. */
  touch_gesture_up(event->touch_id, event->time_msec);

  if (cursor->has_primary_touch
      && event->touch_id == cursor->primary_touch_id) {
    release_primary_touch(cursor, event->time_msec);
  }
}

static void
cursor_touch_motion_handler(struct wl_listener *listener, void *data)
{
  /* [COMMENT] Action purpose: A touch cannot be seen while the screen is
  locked, and nothing here has to test for it. hikari_lock_mode_enter() calls
  hikari_cursor_deactivate() -- which removes this listener -- BEFORE it
  installs lock mode, and only lock mode's cancel() reattaches, so these
  handlers are detached for the whole of a lock. The same reasoning and the
  same assertion guard the pointer handlers further down this file. */
  assert(!hikari_server_in_lock_mode());

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

  /* Before the forwarding below, because this is what may claim the
  sequence -- and once it has, nothing more of it reaches the client. */
  touch_gesture_motion(event->touch_id, lx, ly, event->time_msec);

  if (!cursor->touch_gesture.claimed) {
    wlr_seat_touch_notify_motion(
        hikari_server.seat, event->time_msec, event->touch_id, sx, sy);
  }

  if (cursor->has_primary_touch
      && event->touch_id == cursor->primary_touch_id) {
    wlr_cursor_warp(cursor->wlr_cursor, &event->touch->base, lx, ly);

    /* [COMMENT] Action purpose: Only the modes for which the motion IS the
    window management set this -- move and resize, where the drag moves the
    window. Normal mode deliberately does not: a finger swiping inside a
    window is scrolling it, not choosing a different one, so re-focusing
    whatever it crosses would be wrong. */
    if (hikari_server.mode->touch_motion != NULL) {
      hikari_server.mode->touch_motion(event->time_msec);
    }

    if (cursor->touch_emulates_pointer) {
      touch_pointer_motion(cursor, lx, ly, event->time_msec);
    }
  }
}

static void
cursor_touch_cancel_handler(struct wl_listener *listener, void *data)
{
  struct wlr_touch_cancel_event *event = data;
  struct hikari_cursor *cursor = &hikari_server.cursor;

  /* [COMMENT] Action purpose: The hardware abandoned the sequence, so no
  gesture was made, and a partial sequence must not go on to be recognised as
  something. Dropping the tracking wholesale is right -- but every client
  holding one of the points being dropped has to be told first.

  The cancel event names one point. Its own client is covered by the call
  below, which walks the tracked points; doing that before the reset is what
  the helper's contract requires, because once an id is gone from points[]
  nothing can match that finger again, and the suppression a later claim
  applies is by sequence, so the finger's eventual lift would be swallowed and
  its client left holding a touch that never ends. */
  touch_cancel_client_sequences(&cursor->touch_gesture);
  touch_gesture_reset();

  if (cursor->has_primary_touch
      && event->touch_id == cursor->primary_touch_id) {
    release_primary_touch(cursor, event->time_msec);
  }
}

static void
cursor_touch_frame_handler(struct wl_listener *listener, void *data)
{
  /* [COMMENT] Action purpose: Always, even for a sequence that has just been
  claimed. wlr_seat_touch_send_frame() walks the seat's clients and sends only
  to those whose needs_touch_frame is set, clearing it as it goes, so this is
  already a no-op for a client with nothing pending.

  Guarding it on `claimed` instead was wrong in the one case that matters: a
  hold is recognised from inside the up handler, so the up has already been
  forwarded and its client's needs_touch_frame already set by the time the
  claim lands. Skipping the frame left that client holding an uncommitted up --
  a finger that never lifts, since GTK, Qt and Chromium all batch on
  wl_touch.frame -- until some unrelated client's sequence flushed it. */
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
  /* Every other live input path (keyboard, pointer button/motion, and the
  touch point that drives hikari's own bookkeeping) dispatches through
  hikari_server.mode's vtable, which lock_mode overrides to no-ops while
  locked. Gesture recognition never joined that vtable -- it called
  configured actions directly -- so a configured gesture binding fired
  regardless of lock state, the one input modality that could act on an
  unauthenticated locked screen. This guard is what the touchscreen
  recogniser above relies on too: it dispatches through here, so a gesture
  made on a locked screen is refused by the same check. */
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

/* [COMMENT] Script function and purpose: Touchscreen gesture recognition.

libinput does not do this for us. Its gesture events -- the swipe, pinch and
hold the handlers below forward -- come from touchpads only: libinput.h states
under @defgroup event_gesture that "Gesture events are generated when a gesture
is recognized on a touchpad", and LIBINPUT_DEVICE_CAP_GESTURE is a separate
capability from LIBINPUT_DEVICE_CAP_TOUCH. A touchscreen therefore produces
touch points and nothing else, and every gestures{} binding was dead on one.
What follows recognises the same three gestures from those points, so one
configuration block covers both kinds of hardware.

The thresholds below are in layout pixels and milliseconds, not the libinput
units the touchpad path works in, and they are chosen rather than measured --
there is no touchscreen here to calibrate against. They are deliberately
generous: a binding that fails to fire is a nuisance, one that fires during an
ordinary drag is a wrong window moved. */

/* Mean per-finger travel, in layout pixels, before a swipe is recognised. */
#define HIKARI_TOUCH_SWIPE_MIN_DISTANCE 60.0

/* |scale - 1| before a pinch is recognised, scale being the fingers' mean
distance from their centroid now over the same at the start. */
#define HIKARI_TOUCH_PINCH_MIN_SCALE_DELTA 0.2

/* How long the fingers must stay down, and how far they may drift, for the
sequence to count as a hold. Like the touchpad hold the handlers below
forward, it is dispatched when the fingers lift rather than as the dwell
elapses, which keeps the two paths consistent and needs no timer. */
#define HIKARI_TOUCH_HOLD_MIN_DURATION_MSEC 400
#define HIKARI_TOUCH_HOLD_MAX_DISTANCE 30.0

static struct hikari_touch_point *
touch_point_find(struct hikari_touch_gesture_state *state, int32_t id)
{
  for (int i = 0; i < HIKARI_TOUCH_GESTURE_MAX_POINTS; i++) {
    if (state->points[i].active && state->points[i].id == id) {
      return &state->points[i];
    }
  }

  return NULL;
}

static void
touch_gesture_reset(void)
{
  struct hikari_touch_gesture_state *state =
      &hikari_server.cursor.touch_gesture;

  for (int i = 0; i < HIKARI_TOUCH_GESTURE_MAX_POINTS; i++) {
    state->points[i].active = false;
  }

  state->npoints = 0;
  state->peak_fingers = 0;
  state->begin_time_msec = 0;
  state->claimed = false;
  state->fired = false;
  state->start_spread = 0;
  state->start_spread_valid = false;
  state->max_distance = 0;
}

// Function purpose: Mean position of the active points, at either their
// start positions or their current ones.
static void
touch_centroid(struct hikari_touch_gesture_state *state,
    bool start,
    double *cx,
    double *cy)
{
  double sx = 0;
  double sy = 0;
  int n = 0;

  for (int i = 0; i < HIKARI_TOUCH_GESTURE_MAX_POINTS; i++) {
    struct hikari_touch_point *point = &state->points[i];

    if (!point->active) {
      continue;
    }

    sx += start ? point->start_lx : point->lx;
    sy += start ? point->start_ly : point->ly;
    n++;
  }

  *cx = n > 0 ? sx / n : 0;
  *cy = n > 0 ? sy / n : 0;
}

// Function purpose: Mean distance of the active points from their centroid --
// the scalar a pinch's scale is the ratio of.
static double
touch_spread(struct hikari_touch_gesture_state *state, bool start)
{
  double cx;
  double cy;
  touch_centroid(state, start, &cx, &cy);

  double total = 0;
  int n = 0;

  for (int i = 0; i < HIKARI_TOUCH_GESTURE_MAX_POINTS; i++) {
    struct hikari_touch_point *point = &state->points[i];

    if (!point->active) {
      continue;
    }

    double px = start ? point->start_lx : point->lx;
    double py = start ? point->start_ly : point->ly;

    total += hypot(px - cx, py - cy);
    n++;
  }

  return n > 0 ? total / n : 0;
}

// Function purpose: Mean displacement of the active points from their start
// positions, which is what gives a multi-finger swipe its direction.
static void
touch_displacement(
    struct hikari_touch_gesture_state *state, double *dx, double *dy)
{
  double tx = 0;
  double ty = 0;
  int n = 0;

  for (int i = 0; i < HIKARI_TOUCH_GESTURE_MAX_POINTS; i++) {
    struct hikari_touch_point *point = &state->points[i];

    if (!point->active) {
      continue;
    }

    tx += point->lx - point->start_lx;
    ty += point->ly - point->start_ly;
    n++;
  }

  *dx = n > 0 ? tx / n : 0;
  *dy = n > 0 ? ty / n : 0;
}

/* [COMMENT] Function purpose: Tell every client holding one of the tracked
points that its touch sequence is over, each distinct client once -- several of
the sequence's points may belong to the same one.

Separate from touch_gesture_claim() because forgetting the points has to do
this too. Whoever drops a tracked point must cancel its client first: once the
id is gone from points[], nothing here can match that finger again, and the
suppression `claimed` applies is by SEQUENCE rather than by point, so the
finger's eventual lift is swallowed and its client is left holding a touch
that never ends. */
static void
touch_cancel_client_sequences(struct hikari_touch_gesture_state *state)
{
  struct wlr_seat *seat = hikari_server.seat;
  struct wlr_seat_client *seen[HIKARI_TOUCH_GESTURE_MAX_POINTS];
  int nseen = 0;

  for (int i = 0; i < HIKARI_TOUCH_GESTURE_MAX_POINTS; i++) {
    if (!state->points[i].active) {
      continue;
    }

    struct wlr_touch_point *point =
        wlr_seat_touch_get_point(seat, state->points[i].id);

    if (point == NULL || point->client == NULL) {
      continue;
    }

    bool already = false;
    for (int j = 0; j < nseen; j++) {
      if (seen[j] == point->client) {
        already = true;
        break;
      }
    }

    if (already) {
      continue;
    }

    seen[nseen++] = point->client;
    wlr_seat_touch_notify_cancel(seat, point->client);
  }
}

/* [COMMENT] Function purpose: Take the sequence away from the clients holding
it and hand it to the compositor.

The cancel above is the protocol's own word for this -- it tells a client the
compositor has taken over the sequence and that it should undo whatever the
touch had begun -- so a client that was mid-drag when the third finger landed
is left consistent rather than stranded with a point that stops reporting.

The synthesised button the first finger pressed is released here too. Without
that the mode state machine would sit waiting for a release that the
suppressed touch_up can no longer deliver. */
static void
touch_gesture_claim(
    struct hikari_touch_gesture_state *state, uint32_t time_msec)
{
  touch_cancel_client_sequences(state);

  state->claimed = true;

  if (hikari_server.cursor.has_primary_touch) {
    release_primary_touch(&hikari_server.cursor, time_msec);
  }
}

/* Function purpose: Test the in-progress sequence against the configured
gesture bindings, claiming and firing the first that matches. Pinch is tested
before swipe because spreading fingers also displace, so a pinch that went
unchecked would read as a swipe in whichever direction the hands happened to
drift. */
static void
touch_gesture_evaluate(
    struct hikari_touch_gesture_state *state, uint32_t time_msec)
{
  if (state->claimed || state->fired) {
    return;
  }

  /* [COMMENT] Action purpose: Recognise nothing while locked. fire_gesture_
  binding() already refuses to act on a locked screen, but claiming happens
  first, so without this a gesture made on the lock screen would take the
  sequence away from the lock client -- cancelling its touch input -- and
  then decline to do anything with it. Checked here rather than there so the
  points are simply left alone and go on reaching the client. */
  if (hikari_server_in_lock_mode()) {
    return;
  }

  /* Only while every finger of the sequence is still down: once one lifts the
  remaining points no longer describe the gesture that was made. */
  if (state->peak_fingers < HIKARI_TOUCH_GESTURE_MIN_FINGERS
      || state->npoints != (int)state->peak_fingers) {
    return;
  }

  struct hikari_gesture_binding_config *binding_config;

  if (state->start_spread_valid && state->start_spread > 1.0) {
    double scale = touch_spread(state, false) / state->start_spread;

    if (fabs(scale - 1.0) >= HIKARI_TOUCH_PINCH_MIN_SCALE_DELTA) {
      binding_config = find_gesture_binding(HIKARI_GESTURE_PINCH,
          classify_pinch_direction(scale),
          state->peak_fingers);

      if (binding_config != NULL) {
        touch_gesture_claim(state, time_msec);
        state->fired = true;
        fire_gesture_binding(binding_config);
        return;
      }
    }
  }

  double dx;
  double dy;
  touch_displacement(state, &dx, &dy);

  double distance = hypot(dx, dy);
  if (distance > state->max_distance) {
    state->max_distance = distance;
  }

  if (distance >= HIKARI_TOUCH_SWIPE_MIN_DISTANCE) {
    binding_config = find_gesture_binding(HIKARI_GESTURE_SWIPE,
        classify_swipe_direction(dx, dy),
        state->peak_fingers);

    if (binding_config != NULL) {
      touch_gesture_claim(state, time_msec);
      state->fired = true;
      fire_gesture_binding(binding_config);
    }
  }
}

/* Function purpose: Decide, as the first finger lifts, whether the sequence
was a hold. Everything that moves far enough to be a swipe or a pinch has
already been dispatched as one by the time this runs. */
static void
touch_gesture_try_hold(
    struct hikari_touch_gesture_state *state, uint32_t time_msec)
{
  if (hikari_server_in_lock_mode()) {
    return;
  }

  if (state->claimed || state->fired
      || state->peak_fingers < HIKARI_TOUCH_GESTURE_MIN_FINGERS
      || state->max_distance > HIKARI_TOUCH_HOLD_MAX_DISTANCE
      || time_msec - state->begin_time_msec
          < HIKARI_TOUCH_HOLD_MIN_DURATION_MSEC) {
    return;
  }

  struct hikari_gesture_binding_config *binding_config = find_gesture_binding(
      HIKARI_GESTURE_HOLD, HIKARI_GESTURE_DIRECTION_NONE, state->peak_fingers);

  if (binding_config != NULL) {
    touch_gesture_claim(state, time_msec);
    state->fired = true;
    fire_gesture_binding(binding_config);
  }
}

static bool
touch_gesture_down(int32_t id, double lx, double ly, uint32_t time_msec)
{
  struct hikari_touch_gesture_state *state =
      &hikari_server.cursor.touch_gesture;

  struct hikari_touch_point *point = NULL;
  for (int i = 0; i < HIKARI_TOUCH_GESTURE_MAX_POINTS; i++) {
    if (!state->points[i].active) {
      point = &state->points[i];
      break;
    }
  }

  /* More fingers than there are slots. The sequence is already past any
  supported finger count, so tracking stops rather than growing -- and the
  caller must not forward what is not tracked, because a claim cancels by
  walking points[] and would never reach this contact's client. */
  if (point == NULL) {
    return false;
  }

  point->id = id;
  point->active = true;
  point->lx = lx;
  point->ly = ly;
  point->start_lx = lx;
  point->start_ly = ly;

  state->npoints++;

  /* [COMMENT] Action purpose: A finger joining the sequence re-bases it.
  Measuring a three-finger swipe from where the first finger landed would
  charge it with however far that one travelled while the others were still
  on their way down, which is a swipe's worth of movement on its own. Both
  the pinch baseline and the hold clock restart here for the same reason. */
  if (state->npoints > (int)state->peak_fingers) {
    state->peak_fingers = state->npoints;

    for (int i = 0; i < HIKARI_TOUCH_GESTURE_MAX_POINTS; i++) {
      if (state->points[i].active) {
        state->points[i].start_lx = state->points[i].lx;
        state->points[i].start_ly = state->points[i].ly;
      }
    }

    state->begin_time_msec = time_msec;
    state->max_distance = 0;
    state->start_spread = touch_spread(state, true);
    state->start_spread_valid = state->npoints > 1;
  }

  return true;
}

static void
touch_gesture_motion(int32_t id, double lx, double ly, uint32_t time_msec)
{
  struct hikari_touch_gesture_state *state =
      &hikari_server.cursor.touch_gesture;

  struct hikari_touch_point *point = touch_point_find(state, id);

  if (point == NULL) {
    return;
  }

  point->lx = lx;
  point->ly = ly;

  touch_gesture_evaluate(state, time_msec);
}

static void
touch_gesture_up(int32_t id, uint32_t time_msec)
{
  struct hikari_touch_gesture_state *state =
      &hikari_server.cursor.touch_gesture;

  struct hikari_touch_point *point = touch_point_find(state, id);

  if (point != NULL) {
    point->active = false;
    state->npoints--;
  }

  /* The first lift ends the gesture proper: whatever the remaining fingers do
  is the tail of a sequence that has already been made. */
  if (state->npoints == (int)state->peak_fingers - 1) {
    touch_gesture_try_hold(state, time_msec);
  }

  if (state->npoints <= 0) {
    touch_gesture_reset();
  }
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
  cursor->touch_emulates_pointer = false;
  cursor->gesture_state.active = false;

  /* Zeroed explicitly rather than left to the caller's allocation: the
  recogniser reads npoints and peak_fingers before anything has written
  them if the first event to arrive is a motion or an up. */
  for (int i = 0; i < HIKARI_TOUCH_GESTURE_MAX_POINTS; i++) {
    cursor->touch_gesture.points[i].active = false;
  }
  cursor->touch_gesture.npoints = 0;
  cursor->touch_gesture.peak_fingers = 0;
  cursor->touch_gesture.begin_time_msec = 0;
  cursor->touch_gesture.claimed = false;
  cursor->touch_gesture.fired = false;
  cursor->touch_gesture.start_spread = 0;
  cursor->touch_gesture.start_spread_valid = false;
  cursor->touch_gesture.max_distance = 0;

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
  /* [COMMENT] Action purpose: Re-init each link after removing it, so this
  function is idempotent.

  wl_list_remove() leaves the link in an invalid state -- libwayland sets both
  pointers to NULL -- and this runs twice with nothing in between on an
  ordinary path: lock_mode.c deactivates the cursor on lock entry, only its
  unlock path reactivates, and hikari_cursor_fini() deactivates again, so
  ending the session while the screen is locked reached the second
  wl_list_remove() and dereferenced NULL. wl_list_init() leaves an empty,
  removable link, which hikari_cursor_set_image() already relies on for the
  same reason. */
  wl_list_remove(&cursor->motion_absolute.link);
  wl_list_init(&cursor->motion_absolute.link);
  wl_list_remove(&cursor->frame.link);
  wl_list_init(&cursor->frame.link);
  wl_list_remove(&cursor->motion.link);
  wl_list_init(&cursor->motion.link);
  wl_list_remove(&cursor->button.link);
  wl_list_init(&cursor->button.link);
  wl_list_remove(&cursor->axis.link);
  wl_list_init(&cursor->axis.link);
  wl_list_remove(&cursor->request_set_cursor.link);
  wl_list_init(&cursor->request_set_cursor.link);
  wl_list_remove(&cursor->touch_down.link);
  wl_list_init(&cursor->touch_down.link);
  wl_list_remove(&cursor->touch_up.link);
  wl_list_init(&cursor->touch_up.link);
  wl_list_remove(&cursor->touch_motion.link);
  wl_list_init(&cursor->touch_motion.link);
  wl_list_remove(&cursor->touch_cancel.link);
  wl_list_init(&cursor->touch_cancel.link);
  wl_list_remove(&cursor->touch_frame.link);
  wl_list_init(&cursor->touch_frame.link);
  wl_list_remove(&cursor->swipe_begin.link);
  wl_list_init(&cursor->swipe_begin.link);
  wl_list_remove(&cursor->swipe_update.link);
  wl_list_init(&cursor->swipe_update.link);
  wl_list_remove(&cursor->swipe_end.link);
  wl_list_init(&cursor->swipe_end.link);
  wl_list_remove(&cursor->pinch_begin.link);
  wl_list_init(&cursor->pinch_begin.link);
  wl_list_remove(&cursor->pinch_update.link);
  wl_list_init(&cursor->pinch_update.link);
  wl_list_remove(&cursor->pinch_end.link);
  wl_list_init(&cursor->pinch_end.link);
  wl_list_remove(&cursor->hold_begin.link);
  wl_list_init(&cursor->hold_begin.link);
  wl_list_remove(&cursor->hold_end.link);
  wl_list_init(&cursor->hold_end.link);

  // Action purpose: A held primary touch point is driving mode-level state
  // (focus/raise, or an in-progress move/resize drag) via a synthetic
  // BTN_LEFT press. Deactivation happens mid-session (e.g. entering lock
  // mode, which overwrites hikari_server.mode right after this call) --
  // release it through the still-active mode's button_handler now, or that
  // mode is left believing a button is still held with no touch event ever
  // able to release it.
  if (cursor->has_primary_touch) {
    /* [COMMENT] Action purpose: end_primary_touch(), not
    release_primary_touch(): the contact is dropped WITHOUT telling the mode.

    Both callers of this function are abandoning the mode, not continuing in
    it -- lock entry installs lock mode two statements later, and
    hikari_cursor_fini() is tearing the session down -- so there is nothing
    left to inform. Telling it is worse than pointless: move and resize answer
    touch_end by re-entering normal mode, which ends in
    hikari_server_cursor_focus() and would hand a client back both keyboard
    and pointer focus a moment after lock entry deliberately cleared them.

    The emulated button release still goes out, so a client that was sent a
    press is never left holding it; with pointer focus already cleared by lock
    entry it reaches nobody, which is the intended outcome. */
    end_primary_touch(cursor, touch_now_msec());
  }

  // Action purpose: Clear any in-progress gesture. Deactivation happens
  // mid-session (e.g. entering lock mode); without this, a gesture left
  // "active" here would resume against stale state (or never reset) once
  // the cursor is reactivated.
  cursor->gesture_state.active = false;

  /* [COMMENT] Action purpose: And the touchscreen recogniser, for the same
  reason and more urgently. Its state is not one flag: fingers still down are
  held in points[] with npoints counting them, and touch_gesture_reset() is
  otherwise reached only when the last finger lifts or a cancel arrives --
  neither of which can happen once the listeners above have been removed. A
  deactivation mid-touch would therefore carry those points into the next
  activation, where a new finger joins a sequence that no longer exists.

  `claimed` is the part that does real harm. It survives as true, and the
  down, motion, up and frame handlers all refuse to forward anything while it
  is set, so touch input would be silently dead after the reactivation with
  nothing left able to clear it.

  Cancelled before being forgotten: a finger still down here is about to lose
  its entry in points[], and once the id is gone nothing can match that finger
  again. Because the suppression `claimed` applies is by sequence rather than
  by point, its eventual lift would then be swallowed by a later claimed
  gesture and its client left holding a touch that never ends. */
  touch_cancel_client_sequences(&cursor->touch_gesture);
  touch_gesture_reset();

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
