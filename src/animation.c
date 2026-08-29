// [COMMENT] Script function and purpose: Position interpolation for windows.
// See include/hikari/animation.h for why size is deliberately out of scope.

#include <hikari/animation.h>

#include <assert.h>

#include <wlr/types/wlr_scene.h>

#include <hikari/configuration.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/view.h>

void
hikari_animation_config_init(struct hikari_animation_config *config)
{
  assert(config != NULL);

  /* [COMMENT] Action purpose: Off by default. Motion is a preference, and a
  configuration that says nothing about it must behave exactly as it did before
  this module existed. */
  config->enabled = false;
  config->duration_msec = 120;
  config->easing = HIKARI_EASING_EASE_OUT;
}

void
hikari_animation_init(struct hikari_animation *animation)
{
  assert(animation != NULL);

  animation->active = false;
  animation->placed = false;
  animation->from_x = 0;
  animation->from_y = 0;
  animation->to_x = 0;
  animation->to_y = 0;
  animation->drawn_x = 0;
  animation->drawn_y = 0;
  animation->start_msec = 0;
}

// [COMMENT] Function purpose: Milliseconds on CLOCK_MONOTONIC, truncated to the
// 32 bits the animation state stores. See the field comment in animation.h for
// why truncation is safe here.
static uint32_t
now_msec(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  return (uint32_t)((uint64_t)now.tv_sec * 1000 +
                    (uint64_t)now.tv_nsec / 1000000);
}

/* [COMMENT] Function purpose: Map linear progress to eased progress, both in
[0, 1].

The cubics are written out rather than expressed through pow(), so this
translation unit needs no <math.h> and the link needs no -lm. */
static double
ease(enum hikari_easing easing, double t)
{
  switch (easing) {
    case HIKARI_EASING_LINEAR:
      return t;

    case HIKARI_EASING_EASE_OUT: {
      double inv = 1.0 - t;
      return 1.0 - inv * inv * inv;
    }

    case HIKARI_EASING_EASE_IN_OUT:
      if (t < 0.5) {
        return 4.0 * t * t * t;
      } else {
        double inv = -2.0 * t + 2.0;
        return 1.0 - (inv * inv * inv) / 2.0;
      }
  }

  return t;
}

// [COMMENT] Function purpose: Linear progress through the animation, clamped to
// [0, 1]. A zero or negative configured duration reports 1.0, which makes the
// animation complete on its first tick rather than dividing by zero.
static double
progress(struct hikari_animation *animation, uint32_t at_msec)
{
  int duration = hikari_configuration->animation.duration_msec;

  if (duration <= 0) {
    return 1.0;
  }

  // [COMMENT] Action purpose: Unsigned difference, so a monotonic-clock wrap
  // between start and now still yields the correct elapsed value.
  uint32_t elapsed = at_msec - animation->start_msec;

  if (elapsed >= (uint32_t)duration) {
    return 1.0;
  }

  return (double)elapsed / (double)duration;
}

// [COMMENT] Function purpose: Where the view is drawn right now, in output-local
// coordinates.
static void
current_position(struct hikari_animation *animation,
    uint32_t at_msec,
    int *x,
    int *y)
{
  if (!animation->active) {
    *x = animation->to_x;
    *y = animation->to_y;
    return;
  }

  double t = ease(hikari_configuration->animation.easing,
      progress(animation, at_msec));

  *x = animation->from_x +
       (int)((double)(animation->to_x - animation->from_x) * t);
  *y = animation->from_y +
       (int)((double)(animation->to_y - animation->from_y) * t);
}

/* [COMMENT] Function purpose: Decide whether this view may animate at all right
now.

Every case here is a case where interpolation would be wrong rather than merely
unwanted. A disabled configuration and a zero duration are the user's choice. An
unplaced view has no meaningful origin to travel from. A view with no scene node
or no output has nothing to position. A hidden view would spend its animation
invisible and arrive late. Lock mode must not move anything. And an interactive
move or resize is the user dragging the window in real time -- a hundred
milliseconds of lag behind the pointer is exactly the "window does not move
properly" complaint this work exists to fix, so a drag always snaps. */
static bool
may_animate(struct hikari_view *view)
{
  struct hikari_animation_config *config = &hikari_configuration->animation;

  return config->enabled && config->duration_msec > 0 &&
         view->animation.placed && view->scene_node != NULL &&
         view->output != NULL && !hikari_view_is_hidden(view) &&
         !hikari_server_in_lock_mode() && !hikari_server_in_move_mode() &&
         !hikari_server_in_resize_mode();
}

bool
hikari_animation_move(struct hikari_view *view, int x, int y)
{
  assert(view != NULL);

  struct hikari_animation *animation = &view->animation;

  if (!may_animate(view)) {
    /* [COMMENT] Action purpose: Record the destination even when snapping, so
    the next animation has a correct origin and hikari_animation_offset()
    reports zero. `placed` is set here and only here -- the caller is about to
    perform the instant placement this return value asks for. */
    animation->active = false;
    animation->placed = true;
    animation->from_x = x;
    animation->from_y = y;
    animation->to_x = x;
    animation->to_y = y;

    /* [COMMENT] Action purpose: The caller places the node at (x, y) the moment
    this returns false, so that is where it will be drawn. Recorded here because
    `drawn_*` is only ever a record of an actual placement -- and because this is
    the path that first sets `placed`, it is what makes `drawn_*` valid for every
    later read. */
    animation->drawn_x = x;
    animation->drawn_y = y;

    return false;
  }

  if (!animation->active && animation->to_x == x && animation->to_y == y) {
    return false;
  }

  uint32_t at_msec = now_msec();

  /* [COMMENT] Action purpose: Retarget from where the view is DRAWN, not from
  where it was headed. A second move arriving mid-flight -- two windows closing
  in quick succession, say -- would otherwise restart from the previous target
  and visibly jump backwards before setting off again.

  `drawn_*` and not a fresh current_position(): the node is wherever the last
  tick left it, and the interpolated position for *now* is up to a frame further
  along than that. Departing from the latter makes the window jump forward by
  that much at the instant of retargeting. */
  animation->from_x = animation->drawn_x;
  animation->from_y = animation->drawn_y;
  animation->to_x = x;
  animation->to_y = y;
  animation->start_msec = at_msec;
  animation->active = true;

  /* [COMMENT] Action purpose: hikari renders on damage; without this an
  animation started while the screen is otherwise idle would never receive the
  frame that advances it.

  Requested for the whole TRAVEL -- the union of the departure box and the
  arrival box -- rather than for the view's current position, and through
  hikari_output_add_damage() rather than hikari_output_schedule_frame(), so that
  a window whose journey reaches a neighbouring screen wakes that screen too.
  Scheduling this output alone left the far half of a spanning window stale for
  the whole flight. Same defect as the one fixed in hikari_output_add_damage()
  itself; this is the caller that could not use it before, because it had no box
  to hand it. */
  struct wlr_box *geometry = hikari_view_geometry(view);

  int min_x = animation->from_x < x ? animation->from_x : x;
  int min_y = animation->from_y < y ? animation->from_y : y;
  int max_x = (animation->from_x > x ? animation->from_x : x) + geometry->width;
  int max_y =
      (animation->from_y > y ? animation->from_y : y) + geometry->height;

  struct wlr_box travel = { .x = min_x,
    .y = min_y,
    .width = max_x - min_x,
    .height = max_y - min_y };

  hikari_output_add_damage(view->output, &travel);

  return true;
}

void
hikari_animation_offset(struct hikari_view *view, int *dx, int *dy)
{
  assert(view != NULL);

  struct hikari_animation *animation = &view->animation;

  if (!animation->active) {
    *dx = 0;
    *dy = 0;
    return;
  }

  /* [COMMENT] Action purpose: Read the recorded placement rather than
  recomputing one for the current instant. The scene node is where the last tick
  put it and will not move until the next frame, so that -- and only that -- is
  the position a pointer is actually over. See the `drawn_*` comment in
  animation.h for why the difference is large rather than academic. */
  *dx = animation->drawn_x - animation->to_x;
  *dy = animation->drawn_y - animation->to_y;
}

bool
hikari_animation_tick(struct hikari_output *output, uint32_t at_msec)
{
  assert(output != NULL);

  bool running = false;

  struct hikari_view *view;
  wl_list_for_each (view, &output->views, output_views) {
    struct hikari_animation *animation = &view->animation;

    if (!animation->active) {
      continue;
    }

    /* [COMMENT] Action purpose: A view that stopped being animatable while its
    animation was in flight -- hidden, unmapped from the scene, or the user
    started dragging it -- is finished immediately at its target rather than
    left half way. */
    if (view->scene_node == NULL || !may_animate(view)) {
      hikari_animation_cancel(view);
      continue;
    }

    int current_x;
    int current_y;
    current_position(animation, at_msec, &current_x, &current_y);

    if (progress(animation, at_msec) >= 1.0) {
      animation->active = false;
      current_x = animation->to_x;
      current_y = animation->to_y;
    } else {
      running = true;
    }

    // [COMMENT] Action purpose: Same convention as
    // hikari_view_refresh_geometry() -- the node sits at the view's
    // output-local geometry plus the output's own layout origin.
    wlr_scene_node_set_position(view->scene_node,
        current_x + output->geometry.x,
        current_y + output->geometry.y);

    // [COMMENT] Action purpose: Record the placement immediately after making
    // it, so hit testing this frame reads exactly what this frame drew.
    animation->drawn_x = current_x;
    animation->drawn_y = current_y;

    /* [COMMENT] Action purpose: Wake any OTHER screen this frame's position
    reaches. frame_handler() reschedules only the output it is running on, and
    hikari_animation_tick() walks only that output's views -- so a window
    travelling across a seam advanced on its own screen every frame and left its
    far half frozen on the neighbour. Requesting damage for the box just drawn
    fans the frame out; the neighbour's own tick walks its own views, does not
    find this one and does not reschedule, so this cannot become a loop. */
    struct wlr_box *geometry = hikari_view_geometry(view);

    struct wlr_box drawn = { .x = current_x,
      .y = current_y,
      .width = geometry->width,
      .height = geometry->height };

    hikari_output_add_damage(output, &drawn);
  }

  return running;
}

void
hikari_animation_cancel(struct hikari_view *view)
{
  assert(view != NULL);

  struct hikari_animation *animation = &view->animation;

  if (!animation->active) {
    return;
  }

  animation->active = false;

  if (view->scene_node != NULL && view->output != NULL) {
    wlr_scene_node_set_position(view->scene_node,
        animation->to_x + view->output->geometry.x,
        animation->to_y + view->output->geometry.y);

    // [COMMENT] Action purpose: Cancelling finishes the move at its target, so
    // that is now where the node is drawn.
    animation->drawn_x = animation->to_x;
    animation->drawn_y = animation->to_y;
  }
}
