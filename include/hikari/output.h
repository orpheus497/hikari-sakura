#if !defined(HIKARI_OUTPUT_H)
#define HIKARI_OUTPUT_H

#include <assert.h>

#include <stdbool.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>

#include <hikari/bar.h>
#include <hikari/server.h>
#include <hikari/output_config.h>



struct hikari_output {
  struct hikari_server *server;
  struct wlr_output *wlr_output;
  struct wlr_scene_output *scene_output;
  struct wlr_scene_buffer *lock_indicator_node;
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

/* [COMMENT] Function purpose: Schedule a frame for damage on an output.
Guards instead of asserting: NDEBUG erases asserts, and view->output is
legitimately NULL before hikari_view_configure. */
static inline void
hikari_output_add_damage(struct hikari_output *output, struct wlr_box *region)
{
  if (output == NULL || region == NULL) {
    return;
  }

  if (output->enabled && output->scene_output != NULL) {
    wlr_output_schedule_frame(output->wlr_output);
  }
}

/* [COMMENT] Function purpose: Schedule a frame on an output, if it is usable. */
static inline void
hikari_output_schedule_frame(struct hikari_output *output)
{
  if (output == NULL || !output->enabled) {
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
