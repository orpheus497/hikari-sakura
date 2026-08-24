#if !defined(HIKARI_RESIZE_MODE_H)
#define HIKARI_RESIZE_MODE_H

#include <hikari/mode.h>

struct hikari_binding;
struct hikari_view;

struct hikari_resize_mode {
  struct hikari_mode mode;

  /* [COMMENT] Class purpose: The grab anchor, as an offset from the window's
  BOTTOM-RIGHT corner in output-local coordinates -- the corner this mode
  resizes from.

  It fixes two things at once. The window no longer snaps its corner to the
  pointer when the mode is entered away from that corner; and the one-pixel
  shrink per entry disappears. The latter was arithmetic, not perception:
  hikari_view_bottom_right_cursor() warps to `geometry->x + geometry->width`,
  while the motion handler computed `cursor_x - geometry->x - border`, so
  entering resize mode and releasing without moving the pointer took `border`
  pixels off the window every time. The anchor makes entry a no-op by
  construction. */
  int anchor_x;
  int anchor_y;
};

void
hikari_resize_mode_init(struct hikari_resize_mode *resize_mode);

void
hikari_resize_mode_enter(struct hikari_view *view);

#endif
