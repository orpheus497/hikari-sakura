/* [COMMENT] Script function and purpose: Compositor-side motion for windows --
the interpolation state each view carries, the `ui { animation { ... } }`
configuration, and the per-frame tick that advances them.

SCOPE, stated up front because the boundary is not a matter of taste. This
animates POSITION only. A window's size is not the compositor's to interpolate:
a resize is a protocol round trip -- view->resize() returns a serial, the view
goes dirty, and the client redraws at the new size whenever it gets to it -- so
there is no sequence of intermediate sizes to draw, only the old buffer and then
the new one. Anything calling itself a resize animation is really scaling a
stale buffer, which is visibly soft on text. Position has no such problem: the
view's scene tree is positioned by the compositor alone, so moving it is exact
at every step and costs one wlr_scene_node_set_position() per frame.

The other half of correctness is that hikari hit-tests through its OWN geometry
(node_at() -> surface_at() -> hikari_view_geometry()) rather than through the
scene graph. Moving the scene node without telling the hit test would therefore
put the pointer somewhere the window is not, for the length of every animation.
hikari_animation_offset() exists for exactly that, and node_at() applies it. */

#if !defined(HIKARI_ANIMATION_H)
#define HIKARI_ANIMATION_H

#include <stdbool.h>
#include <stdint.h>

struct hikari_output;
struct hikari_view;

enum hikari_easing {
  HIKARI_EASING_LINEAR,
  HIKARI_EASING_EASE_OUT,
  HIKARI_EASING_EASE_IN_OUT
};

struct hikari_animation_config {
  bool enabled;
  int duration_msec;
  enum hikari_easing easing;
};

/* Function purpose: Establish the defaults -- off, so nothing changes for a
configuration that does not mention animation. */
void
hikari_animation_config_init(struct hikari_animation_config *config);

struct hikari_animation {
  bool active;

  /* [COMMENT] Class purpose: False until this view's scene node has been
  positioned once. Without it a window would animate in from wherever an
  unpositioned node happens to sit -- the origin of the output layout -- on
  every single map, which reads as windows flying in from the corner of the
  screen rather than as a move. The first placement is always instant. */
  bool placed;

  int from_x;
  int from_y;
  int to_x;
  int to_y;

  /* [COMMENT] Class purpose: Milliseconds on CLOCK_MONOTONIC, truncated to 32
  bits. Elapsed time is computed as an unsigned difference, which is correct
  across the ~49.7-day wrap; nothing here ever compares two absolute values. */
  uint32_t start_msec;
};

void
hikari_animation_init(struct hikari_animation *animation);

/* [COMMENT] Function purpose: Retarget `view` to the output-local position
(x, y).

Returns true when the animation has taken ownership of the scene node's
position, in which case the caller must NOT place the node itself -- the tick
will. Returns false when the move is to be applied instantly, which covers a
disabled configuration, the first placement, a hidden or node-less view, lock
mode, and an interactive move or resize. The caller places the node in that
case, exactly as it did before this module existed. */
bool
hikari_animation_move(struct hikari_view *view, int x, int y);

/* [COMMENT] Function purpose: How far the view is currently drawn from where
its geometry says it is, in output-local pixels. Zero unless an animation is
running. Subtract it from a pointer position to convert "where the user is
pointing on screen" into the coordinate space hikari's hit test works in. */
void
hikari_animation_offset(struct hikari_view *view, int *dx, int *dy);

/* [COMMENT] Function purpose: Advance every animating view on `output` and
place its scene node. Returns true if at least one is still running, i.e. the
output needs another frame. */
bool
hikari_animation_tick(struct hikari_output *output, uint32_t now_msec);

/* [COMMENT] Function purpose: Abandon an in-flight animation, leaving the view
at its target. Called when a view stops being animatable at all -- unmap, and
entering an interactive drag -- so a stale animation cannot keep writing to a
scene node the rest of the compositor believes is settled. */
void
hikari_animation_cancel(struct hikari_view *view);

#endif
