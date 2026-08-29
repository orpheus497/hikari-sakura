#if !defined(HIKARI_OUTPUT_H)
#define HIKARI_OUTPUT_H

#include <assert.h>

#include <stdbool.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hikari/bar.h>
#include <hikari/server.h>
#include <hikari/output_config.h>



struct hikari_output {
  struct hikari_server *server;
  struct wlr_output *wlr_output;
  struct wlr_scene_output *scene_output;
  struct wlr_scene_buffer *lock_indicator_node;
  /* [COMMENT] Class purpose: The lock screen's own scene nodes, both
  parented to hikari_server.layers.lock. The backdrop is the blurred
  snapshot of this output taken at lock time; the clock is drawn above it.
  Per-output because each has its own resolution and its own picture. */
  struct wlr_scene_buffer *lock_backdrop_node;
  struct wlr_scene_buffer *lock_clock_node;
  struct hikari_workspace *workspace;

  bool enabled;

  struct wl_listener frame;
  struct wl_listener request_state;
  struct wl_listener destroy;

#ifdef HAVE_LAYERSHELL
  struct wl_list layers[4];
#endif

  struct wl_list views;
#ifdef HAVE_XWAYLAND
  struct wl_list unmanaged_xwayland_views;
#endif
  struct wl_list server_outputs;

  struct wlr_box geometry;
  struct wlr_box usable_area;

  struct wlr_scene_node *background;

  /* This output's native top bar. Reserves space out
  of usable_area so tiled views never sit underneath it. */
  struct hikari_bar bar;
};

void
hikari_output_init(struct hikari_output *output, struct wlr_output *wlr_output);

void
hikari_output_fini(struct hikari_output *output);

void
hikari_output_damage_whole(struct hikari_output *output);

void
hikari_output_disable(struct hikari_output *output);

void
hikari_output_enable(struct hikari_output *output);

/* Function purpose: Re-derive an output's geometry, usable area, bar
reservation and background placement from the output layout. The single entry
point for that -- output init and the layout-change handler both call it, so a
move, a mode change and a hotplug all take the same path. */
void
hikari_output_update_geometry(struct hikari_output *output);

void
hikari_output_load_background(struct hikari_output *output,
    const char *path,
    enum hikari_background_fit background_fit);

void
hikari_output_move(struct hikari_output *output, double lx, double ly);

struct hikari_output *
hikari_output_next(struct hikari_output *output);

struct hikari_output *
hikari_output_prev(struct hikari_output *output);

#ifdef HAVE_XWAYLAND
void
hikari_output_rearrange_xwayland_views(struct hikari_output *output);
#endif

/* [COMMENT] Function purpose: Schedule a frame for damage on an output, AND on
every other output the damaged region reaches.

`region` is output-local to `output`. It used to be taken, checked for NULL and
then never read -- so damage always scheduled exactly one frame, on the one
output it was handed, and every view damage path hands it `view->output`. A
window straddling two screens therefore repainted on the screen it belonged to
and went stale on the other, for whole frames at a time, until something
unrelated happened to damage that output. That is the choppy, torn seam during a
cross-screen drag: two halves of one window drawn from different frames.

It is NOT the 60.026/60.000 Hz beat between the two panels. That is real, is
inherent to independent per-output page flips, and bounds the halves to one
frame apart; this had no such bound.

Guards instead of asserting: NDEBUG erases asserts, and view->output is
legitimately NULL before hikari_view_configure.

The origin output is scheduled exactly as before, unconditionally and without
consulting the box, so this cannot regress a caller that passes a degenerate
region -- an empty box intersects nothing, and the fan-out below would silently
drop damage that used to be delivered. The neighbours are additional. */
static inline void
hikari_output_add_damage(struct hikari_output *output, struct wlr_box *region)
{
  if (output == NULL || region == NULL) {
    return;
  }

  if (output->enabled && output->scene_output != NULL) {
    wlr_output_schedule_frame(output->wlr_output);
  }

  /* [COMMENT] Action purpose: Translate to layout space, which is the only
  space in which two outputs' boxes can be compared, and schedule every OTHER
  output the region reaches. Unconditional by ruling: the test is arithmetic on
  two boxes, and correctness here must not depend on reading a spill policy. A
  window clipped to its own screen still has a box that reaches only that
  screen, so the extra frames are naturally absent rather than suppressed. */
  struct wlr_box layout_region = { .x = region->x + output->geometry.x,
    .y = region->y + output->geometry.y,
    .width = region->width,
    .height = region->height };

  struct hikari_output *neighbour;
  wl_list_for_each (neighbour, &hikari_server.outputs, server_outputs) {
    if (neighbour == output || !neighbour->enabled ||
        neighbour->scene_output == NULL) {
      continue;
    }

    struct wlr_box intersection;
    if (wlr_box_intersection(
            &intersection, &layout_region, &neighbour->geometry)) {
      wlr_output_schedule_frame(neighbour->wlr_output);
    }
  }
}

/* [COMMENT] Function purpose: Schedule a frame on an output, if it is usable. */
static inline void
hikari_output_schedule_frame(struct hikari_output *output)
{
  if (output == NULL || !output->enabled || output->scene_output == NULL) {
    return;
  }

  wlr_output_schedule_frame(output->wlr_output);
}

// [COMMENT] Function purpose: Request a frame for an output based on surface damage, checking for enablement and transitioning states.
static inline void
hikari_output_add_effective_surface_damage(
    struct hikari_output *output, struct wlr_surface *surface, int x, int y)
{
  // [COMMENT] Action purpose: Subsurface commits reach here before the parent
  // view is configured onto an output, so output may legitimately be NULL.
  if (output == NULL || surface == NULL) {
    return;
  }

  if (!output->enabled || output->scene_output == NULL) {
    return;
  }

  wlr_output_schedule_frame(output->wlr_output);
}

#endif
