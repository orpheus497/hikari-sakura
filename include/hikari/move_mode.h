#if !defined(HIKARI_MOVE_MODE_H)
#define HIKARI_MOVE_MODE_H

#include <hikari/mode.h>

struct hikari_binding;
struct hikari_view;

struct hikari_move_mode {
  struct hikari_mode mode;

  /* [COMMENT] Class purpose: The grab anchor -- where inside the window the
  pointer took hold, as an offset from the window's origin in output-local
  coordinates.

  Without it the mode moved the window's TOP-LEFT CORNER to the pointer on every
  motion, and compensated by warping the pointer to that corner on entry. So a
  window grabbed anywhere other than its corner jumped out from under the
  pointer the instant the mode was entered. Recording the offset once and
  subtracting it on every motion is what makes the window move WITH the pointer
  instead of to it. */
  int anchor_x;
  int anchor_y;
};

void
hikari_move_mode_init(struct hikari_move_mode *move_mode);

void
hikari_move_mode_enter(struct hikari_view *view);

#endif
