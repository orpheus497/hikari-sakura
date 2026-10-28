#if !defined(HIKARI_CURSOR_H)
#define HIKARI_CURSOR_H

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include <hikari/binding_group.h>
#include <hikari/gesture_config.h>

struct hikari_output;
struct wlr_touch;

// [COMMENT] Action purpose: bound generously above any realistic
// human-driven swipe/pinch (libinput reports these at pointer-frame
// rate, so even a full-second gesture stays well under this).
#define HIKARI_GESTURE_MAX_UPDATES 128

struct hikari_gesture_update {
  uint32_t time_msec;
  double dx;
  double dy;
  double scale;
  double rotation;
};

struct hikari_gesture_state {
  bool active;
  enum hikari_gesture_type type;
  uint32_t fingers;
  uint32_t begin_time_msec;

  double total_dx;
  double total_dy;
  double last_scale;

  int nupdates;
  struct hikari_gesture_update updates[HIKARI_GESTURE_MAX_UPDATES];
};

/* [COMMENT] Action purpose: Upper bound on simultaneously tracked touch
points. Ten is every finger; libinput reports no more, and a fixed array
keeps the recogniser allocation-free on the input path. */
#define HIKARI_TOUCH_GESTURE_MAX_POINTS 10

/* [COMMENT] Action purpose: Fewest simultaneous fingers a touchscreen gesture
may claim from the client. One- and two-finger sequences are how a client is
driven -- tapping, dragging, selecting text, and its own pinch-to-zoom -- so
the compositor never takes those. A gestures{} binding with a lower finger
count still works on a touchpad, where libinput does the recognition and the
touch points never reach a client at all. */
#define HIKARI_TOUCH_GESTURE_MIN_FINGERS 3

/* [COMMENT] Action purpose: One tracked finger. Start and current positions
are layout pixels, which is what the recogniser's thresholds are expressed in
-- unlike the touchpad path, whose dx/dy arrive in libinput's own units. */
struct hikari_touch_point {
  int32_t id;
  bool active;
  double start_lx;
  double start_ly;
  double lx;
  double ly;
};

/* [COMMENT] Action purpose: Recognition state for one touchscreen sequence,
meaning everything from the first finger landing on an empty screen to the
last one lifting.

`claimed` records that the compositor took the sequence: every client holding
one of its points has been sent wl_touch.cancel, which is precisely what that
event is for, and no further touch event for the sequence is forwarded.
`fired` stops a second binding firing from the same sequence once one has. */
struct hikari_touch_gesture_state {
  struct hikari_touch_point points[HIKARI_TOUCH_GESTURE_MAX_POINTS];
  int npoints;
  uint32_t peak_fingers;
  uint32_t begin_time_msec;
  bool claimed;
  bool fired;

  /* Mean distance from the centroid at the moment peak_fingers was last
  reached, which is the baseline a pinch's scale is measured against. Taken
  then rather than at the first finger because the spread of a three-finger
  pinch is not defined until all three are down. */
  double start_spread;
  bool start_spread_valid;

  /* Largest mean displacement reached during the sequence, kept running so a
  hold can be judged after the fingers have begun to lift, when their
  positions are no longer all available. */
  double max_distance;
};

struct hikari_cursor {
  struct wlr_cursor *wlr_cursor;
  struct wlr_xcursor_manager *cursor_mgr;

  struct wl_listener motion_absolute;
  struct wl_listener motion;
  struct wl_listener frame;
  struct wl_listener axis;
  struct wl_listener button;
  struct wl_listener surface_destroy;
  struct wl_listener request_set_cursor;

  struct wl_listener touch_down;
  struct wl_listener touch_up;
  struct wl_listener touch_motion;
  struct wl_listener touch_cancel;
  struct wl_listener touch_frame;

  struct wl_listener swipe_begin;
  struct wl_listener swipe_update;
  struct wl_listener swipe_end;
  struct wl_listener pinch_begin;
  struct wl_listener pinch_update;
  struct wl_listener pinch_end;
  struct wl_listener hold_begin;
  struct wl_listener hold_end;

  struct hikari_gesture_state gesture_state;

  // [COMMENT] Action purpose: tracks which touch point (if any) is driving
  // hikari's own focus/raise/move/resize bookkeeping, so additional
  // simultaneous fingers stay pure client-forwarded multi-touch input.
  // primary_touch_device is which physical device that point came from,
  // so a different device being unplugged doesn't release it.
  bool has_primary_touch;
  int32_t primary_touch_id;
  struct wlr_touch *primary_touch_device;

  /* [COMMENT] Action purpose: Whether the primary touch point should also
  reach its client as wl_pointer input. False when that client has bound
  wl_touch: it is already receiving the real touch sequence, and sending a
  synthesised button on top of it delivers one finger twice -- a toolkit that
  implements both protocols, which GTK, Qt, Firefox and Chromium all do, sees
  a touch and a click for a single tap. True for a client that has no touch
  support, which would otherwise get nothing at all from a touchscreen.

  Only the client notification is conditional. hikari's own focus, raise,
  move and resize bookkeeping runs off the synthesised button either way. */
  bool touch_emulates_pointer;

  struct hikari_touch_gesture_state touch_gesture;

  struct hikari_binding_group bindings[HIKARI_BINDING_GROUP_MASK];
};

void
hikari_cursor_init(
    struct hikari_cursor *cursor, struct wlr_output_layout *output_layout);

void
hikari_cursor_fini(struct hikari_cursor *cursor);

void
hikari_cursor_configure_bindings(
    struct hikari_cursor *cursor, struct wl_list *bindings);

void
hikari_cursor_activate(struct hikari_cursor *cursor);

void
hikari_cursor_deactivate(struct hikari_cursor *cursor);

/* Release the primary touch if, and only if, it is currently latched to
the given device. For callers outside this file that cannot supply the
touch_id a real touch_up/touch_cancel event carries -- specifically, a
touch device being destroyed while it holds the primary touch, which
otherwise never generates the touch_up/touch_cancel this state machine
expects to clear it. The device check matters because more than one
touch device can drive this cursor: without it, unplugging an unrelated,
idle touch device would release a different device's still-active
primary touch mid-gesture. */
void
hikari_cursor_release_primary_touch(
    struct hikari_cursor *cursor, struct wlr_touch *touch);

void
hikari_cursor_set_image(struct hikari_cursor *cursor, const char *path);

void
hikari_cursor_center(struct hikari_cursor *cursor,
    struct hikari_output *output,
    struct wlr_box *geometry);

static inline void
hikari_cursor_reset_image(struct hikari_cursor *cursor)
{
  hikari_cursor_set_image(cursor, "left_ptr");
}

/* Function purpose: Move the pointer on the compositor's own initiative -- the
one funnel every such warp in the tree reaches.

Defined in src/cursor.c rather than inline here because it now has to consult
the active pointer constraint, and pulling pointer_constraints.h into every
translation unit that includes this header (most of them, via server.h) to
achieve that would be the wrong trade. */
void
hikari_cursor_warp(struct hikari_cursor *cursor, int x, int y);

#endif
