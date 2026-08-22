#if !defined(HIKARI_CURSOR_H)
#define HIKARI_CURSOR_H

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include <hikari/binding_group.h>
#include <hikari/gesture_config.h>

struct hikari_output;

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
  bool has_primary_touch;
  int32_t primary_touch_id;

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

static inline void
hikari_cursor_warp(struct hikari_cursor *cursor, int x, int y)
{
  wlr_cursor_warp(cursor->wlr_cursor, NULL, x, y);
}

#endif
