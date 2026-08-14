// [COMMENT] Script function and purpose: Hikari output management, modesetting, and scene tree setup for display outputs.

#include <hikari/output.h>

#include <stdio.h>

#include <wayland-server-core.h>
#include <cairo/cairo.h>

#include <drm_fourcc.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include <hikari/memory.h>

#include <hikari/server.h>
#ifdef HAVE_XWAYLAND
#include <hikari/view.h>
#endif

static inline void
render_image_to_surface(cairo_surface_t *output,
    cairo_surface_t *image,
    enum hikari_background_fit fit)
{
  cairo_t *cairo = cairo_create(output);
  if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
    cairo_destroy(cairo);
    return;
  }

  double output_width = cairo_image_surface_get_width(output);
  double output_height = cairo_image_surface_get_height(output);
  double width = cairo_image_surface_get_width(image);
  double height = cairo_image_surface_get_height(image);

  cairo_rectangle(cairo, 0, 0, output_width, output_height);
  cairo_fill(cairo);

  if (fit == HIKARI_BACKGROUND_STRETCH) {
    cairo_scale(cairo, output_width / width, output_height / height);
    cairo_set_source_surface(cairo, image, 0, 0);
  } else if (fit == HIKARI_BACKGROUND_CENTER) {
    cairo_set_source_surface(cairo,
        image,
        output_width / 2 - width / 2,
        output_height / 2 - height / 2);
  } else if (fit == HIKARI_BACKGROUND_TILE) {
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(image);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_set_source(cairo, pattern);
    cairo_pattern_destroy(pattern);
  }

  cairo_paint(cairo);
  cairo_destroy(cairo);
}

void
hikari_output_load_background(struct hikari_output *output,
    const char *path,
    enum hikari_background_fit background_fit)
{
  if (output->background != NULL) {
    wlr_scene_node_destroy(&output->background->node);
    output->background = NULL;
  }

  assert(output->background == NULL);

  if (path == NULL) {
    goto done;
  }

  cairo_surface_t *image = cairo_image_surface_create_from_png(path);
  if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
    goto done;
  }

  int output_width = output->geometry.width;
  int output_height = output->geometry.height;

  cairo_surface_t *output_surface = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, output_width, output_height);
  if (cairo_surface_status(output_surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(image);
    goto done;
  }

  render_image_to_surface(output_surface, image, background_fit);

  unsigned char *data = cairo_image_surface_get_data(output_surface);
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, output_width);

  // [COMMENT] Action purpose: Declare wlr_drm_format using only the public API contract:
  // zero-initialise the struct, then set .format. The .len=0 and .modifiers=NULL
  // fields indicate no explicit modifier list, allowing the allocator to choose
  // the best available modifier. Accessing .capacity is forbidden -- it is a
  // private internal field used by wlroots for dynamic array bookkeeping.
  struct wlr_drm_format format = {0};
  format.format = DRM_FORMAT_ARGB8888;
  struct wlr_buffer *buffer = wlr_allocator_create_buffer(hikari_server.allocator, output_width, output_height, &format);
  
  // [COMMENT] Action purpose: Check if buffer allocation succeeded.
  if (buffer != NULL) {
    void *mapped_data;
    uint32_t mapped_format;
    size_t mapped_stride;
    // [COMMENT] Action purpose: Guard against failed buffer data mapping.
    if (wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &mapped_data, &mapped_format, &mapped_stride)) {
      // [COMMENT] Action purpose: Copy rendered cairo image data into mapped buffer by row.
      for (int y = 0; y < output_height; y++) {
        memcpy((char*)mapped_data + y * mapped_stride, data + y * stride, output_width * 4);
      }
      wlr_buffer_end_data_ptr_access(buffer);

      output->background = wlr_scene_buffer_create(&hikari_server.scene->tree, buffer);
      wlr_scene_node_set_position(&output->background->node, output->geometry.x, output->geometry.y);
      wlr_scene_node_lower_to_bottom(&output->background->node);
    }
    wlr_buffer_drop(buffer);
  }

  cairo_surface_destroy(image);
  cairo_surface_destroy(output_surface);

done:
  if (output->enabled) {
    hikari_output_damage_whole(output);
  }
}

void
hikari_output_damage_whole(struct hikari_output *output)
{
  assert(output != NULL);

  if (output->scene_output != NULL) {
    wlr_output_schedule_frame(output->wlr_output);
  }
}

// [COMMENT] Function purpose: Disable specified output and remove its listeners.
void
hikari_output_disable(struct hikari_output *output)
{
  assert(output != NULL);

  if (!output->enabled) {
    return;
  }

  struct wlr_output *wlr_output = output->wlr_output;

  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, false);

  // [COMMENT] Action purpose: Only remove listeners and clear enabled if commit succeeds.
  if (!wlr_output_commit_state(wlr_output, &state)) {
    wlr_output_state_finish(&state);
    return;
  }
  wlr_output_state_finish(&state);

  wl_list_remove(&output->frame.link);
  wl_list_remove(&output->request_state.link);
  wl_list_init(&output->frame.link);
  wl_list_init(&output->request_state.link);

  output->enabled = false;
}

// [COMMENT] Function purpose: Enable specified output, committing state to wlr_output and subscribing frame/state signals.
void
hikari_output_enable(struct hikari_output *output)
{
  assert(output != NULL);

  if (output->enabled) {
    return;
  }

  struct wlr_output *wlr_output = output->wlr_output;

  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  if (!wlr_output_commit_state(wlr_output, &state)) {
    wlr_output_state_finish(&state);
    return;
  }
  wlr_output_state_finish(&state);

  // [COMMENT] Action purpose: Subscribe to frame and request_state events for the output, guarding against double-registration.
  // [COMMENT] Action purpose: Only add the frame listener if not already registered (empty link means unregistered).
  if (wl_list_empty(&output->frame.link)) {
    wl_signal_add(&wlr_output->events.frame, &output->frame);
  }
  // [COMMENT] Action purpose: Only add the request_state listener if not already registered (empty link means unregistered).
  if (wl_list_empty(&output->request_state.link)) {
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
  }

  output->enabled = true;
}

// [COMMENT] Function purpose: Update internal output geometry tracking from layout box.
static void
output_geometry(struct hikari_output *output)
{
  struct wlr_box output_box;
  wlr_output_layout_get_box(
      hikari_server.output_layout, output->wlr_output, &output_box);

  output->geometry.x = output_box.x;
  output->geometry.y = output_box.y;
  output->geometry.width = output_box.width;
  output->geometry.height = output_box.height;

  output->usable_area = (struct wlr_box){
   .x = 0, .y = 0, .width = output_box.width, .height = output_box.height
  };

  // [COMMENT] Action purpose: Reposition background scene node to match updated output geometry.
  if (output->background != NULL) {
    wlr_scene_node_set_position(&output->background->node,
        output->geometry.x, output->geometry.y);
  }
}



#ifdef HAVE_LAYERSHELL
static void
close_layers(struct wl_list *layers)
{
  struct hikari_layer *layer, *layer_temp;
  wl_list_for_each_safe (layer, layer_temp, layers, layer_surfaces) {
    wlr_layer_surface_v1_destroy(layer->surface);
    layer->output = NULL;
  }
}
#endif

static void
frame_handler(struct wl_listener *listener, void *data)
{
  struct hikari_output *output = wl_container_of(listener, output, frame);
  struct wlr_scene *scene = output->server->scene;

  struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
    scene, output->wlr_output);
  if (scene_output == NULL) {
    return;
  }

  wlr_scene_output_commit(scene_output, NULL);

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(scene_output, &now);
}


static void
request_state_handler(struct wl_listener *listener, void *data)
{
  struct hikari_output *output = wl_container_of(listener, output, request_state);
  const struct wlr_output_event_request_state *event = data;
  wlr_output_commit_state(output->wlr_output, event->state);
}

static void
destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_output *output = wl_container_of(listener, output, destroy);

#ifndef NDEBUG
  printf("DESTROY OUTPUT %p\n", output);
#endif

  hikari_output_fini(output);
  hikari_free(output);
}

// [COMMENT] Function purpose: Initialize a new compositor output, allocating workspace and configuring state.
void
hikari_output_init(struct hikari_output *output, struct wlr_output *wlr_output)
{
  assert(!output->enabled);

  bool noop = wlr_output->backend == hikari_server.noop_backend;

  output->wlr_output = wlr_output;
  // [COMMENT] Action purpose: Ensure output->server is always valid after init,
  // regardless of caller. Frame handler (output.c:263) uses output->server->scene.
  output->server = &hikari_server;
  output->scene_output = NULL;
  output->lock_indicator_node = NULL;
  output->background = NULL;
  output->enabled = false;
  output->workspace = hikari_malloc(sizeof(struct hikari_workspace));
  assert(output->workspace != NULL);

#ifdef HAVE_XWAYLAND
  wl_list_init(&output->unmanaged_xwayland_views);
#endif
  wl_list_init(&output->views);

#ifdef HAVE_LAYERSHELL
  wl_list_init(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);
  wl_list_init(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);
  wl_list_init(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);
  wl_list_init(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
#endif

  hikari_workspace_init(output->workspace, output);
  wlr_output->data = output;

  output->frame.notify = frame_handler;
  wl_list_init(&output->frame.link);

  output->request_state.notify = request_state_handler;
  wl_list_init(&output->request_state.link);

  output->destroy.notify = destroy_handler;
  wl_signal_add(&wlr_output->events.destroy, &output->destroy);

  // [COMMENT] Action purpose: Skip hardware setup for headless/noop backend outputs.
  if (!noop) {
    bool first = wl_list_empty(&hikari_server.outputs);

    wl_list_init(&output->server_outputs);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    // [COMMENT] Action purpose: Set the monitor's EDID-preferred mode if available.
    // wlr_output_preferred_mode returns the mode flagged as preferred by the
    // monitor (native resolution at native refresh). Some backends (Wayland,
    // headless) have no modes -- the NULL check handles that.
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
      wlr_output_state_set_mode(&state, mode);
    }

    // [COMMENT] Action purpose: Fail loudly when the initial modeset commit
    // fails. wlroots logs the underlying cause (e.g. the GBM scanout swapchain
    // test) but this early return otherwise leaves a dark-but-alive session
    // with no hint about which output failed. Name the output on stderr; the
    // session stays alive on the noop output for any remaining connectors.
    if (!wlr_output_commit_state(wlr_output, &state)) {
      fprintf(stderr,
          "error: failed to commit initial mode for output \"%s\"; output "
          "will remain disabled\n",
          wlr_output->name);
      wlr_output_state_finish(&state);
      return;
    }
    wlr_output_state_finish(&state);

    output->enabled = true;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    wl_list_insert(&hikari_server.outputs, &output->server_outputs);

    // [COMMENT] Action purpose: Disable the output if lock mode requires it.
    if (hikari_server_in_lock_mode() &&
        hikari_lock_mode_are_outputs_disabled(&hikari_server.lock_mode)) {
      hikari_output_disable(output);
    }

    struct hikari_output_config *output_config =
        hikari_configuration_resolve_output_config(
            hikari_configuration, wlr_output->name);

    struct wlr_scene_output *scene_output = wlr_scene_output_create(hikari_server.scene, wlr_output);
    output->scene_output = scene_output;

    struct wlr_output_layout_output *l_output;
    if (output_config != NULL && output_config->position.value.type ==
                                     HIKARI_POSITION_CONFIG_TYPE_ABSOLUTE) {
      int x = output_config->position.value.config.absolute.x;
      int y = output_config->position.value.config.absolute.y;

      l_output = wlr_output_layout_add(hikari_server.output_layout, wlr_output, x, y);
    } else {
      struct wlr_box extents;
      wlr_output_layout_get_box(hikari_server.output_layout, NULL, &extents);
      l_output = wlr_output_layout_add(hikari_server.output_layout, wlr_output, extents.x + extents.width, 0);
    }
    wlr_scene_output_layout_add_output(hikari_server.scene_layout, l_output, scene_output);

    output_geometry(output);

    // [COMMENT] Action purpose: Load xcursor images at this output's actual scale factor.
    // wlr_output->scale is set by the backend (e.g., from EDID or sysctl on FreeBSD)
    // and may differ from the pre-loaded scales in hikari_cursor_init. Calling
    // wlr_xcursor_manager_load here ensures cursor images are available at the exact
    // density required by this display. Duplicate loads are no-ops internally.
    wlr_xcursor_manager_load(
        hikari_server.cursor.cursor_mgr, (float)wlr_output->scale);

    if (first) {
      hikari_workspace_merge(
          hikari_server.noop_output->workspace, output->workspace);
      hikari_workspace_focus_view(output->workspace, NULL);
    }
  }
}

// [COMMENT] Function purpose: Finalize and teardown an output, merging its workspace to another active output.
void
hikari_output_fini(struct hikari_output *output)
{
  bool noop = output->wlr_output->backend == hikari_server.noop_backend;

#ifdef HAVE_LAYERSHELL
  close_layers(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);
  close_layers(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);
  close_layers(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);
  close_layers(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
#endif

  hikari_output_disable(output);

  if (output->enabled) {
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    output->enabled = false;
  }

  wl_list_remove(&output->destroy.link);

  struct hikari_workspace *workspace = output->workspace;

  if (!noop) {
    struct hikari_workspace *merge_workspace;
    struct hikari_workspace *next_workspace = hikari_workspace_next(workspace);

    if (output->background != NULL) {
      wlr_scene_node_destroy(&output->background->node);
      output->background = NULL;
    }

    if (output->lock_indicator_node != NULL) {
      wlr_scene_node_destroy(&output->lock_indicator_node->node);
      output->lock_indicator_node = NULL;
    }

    if (workspace != next_workspace) {
      merge_workspace = next_workspace;
    } else {
      merge_workspace = hikari_server.noop_output->workspace;
    }

    hikari_workspace_merge(workspace, merge_workspace);

    if (!hikari_server_in_lock_mode()) {
      if (!hikari_server_in_normal_mode()) {
        hikari_server_enter_normal_mode(NULL);
      }

      hikari_workspace_focus_view(merge_workspace, NULL);
    } else {
      merge_workspace->focus_view = NULL;
      hikari_server.workspace = merge_workspace;
    }

    wl_list_remove(&output->server_outputs);
  } else {
    hikari_server.workspace = NULL;
  }

  hikari_workspace_fini(workspace);
  hikari_free(workspace);
}

void
hikari_output_move(struct hikari_output *output, double lx, double ly)
{
  wlr_output_layout_add(
     hikari_server.output_layout, output->wlr_output, lx, ly);
}

#define CYCLE_OUTPUT(name)                                                     \
  struct hikari_output *hikari_output_##name(struct hikari_output *output)     \
  {                                                                            \
    if (wl_list_empty(&hikari_server.outputs)) {                               \
      return NULL;                                                             \
    }                                                                          \
                                                                               \
    struct wl_list *name = output->server_outputs.name;                        \
                                                                               \
    if (name == &hikari_server.outputs) {                                      \
      name = hikari_server.outputs.name;                                       \
    }                                                                          \
                                                                               \
    struct hikari_output *name##_output =                                      \
        wl_container_of(name, name##_output, server_outputs);                  \
                                                                               \
    return name##_output;                                                      \
  }

CYCLE_OUTPUT(next)
CYCLE_OUTPUT(prev)
#undef CYCLE_OUTPUT

#ifdef HAVE_XWAYLAND
void
hikari_output_rearrange_xwayland_views(struct hikari_output *output)
{
  struct hikari_view *view;
  wl_list_for_each (view, &output->views, output_views) {
    if (view->move != NULL) {
      struct wlr_box *geometry = hikari_view_geometry(view);
      view->move(view, geometry->x, geometry->y);
    }
  }
}
#endif
