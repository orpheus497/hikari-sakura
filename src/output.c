// [COMMENT] Script function and purpose: Hikari output management, modesetting, and scene tree setup for display outputs.

#include <hikari/animation.h>
#include <hikari/output.h>

#include <limits.h>
#include <math.h>
#include <stdio.h>

#include <wayland-server-core.h>
#include <cairo/cairo.h>

#include <wlr/backend.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_scene.h>

#include <hikari/buffer.h>
#include <hikari/color.h>
#include <hikari/memory.h>

#include <hikari/output_management.h>
#include <hikari/server.h>
#ifdef HAVE_XWAYLAND
#include <hikari/view.h>
#include <hikari/xwayland_unmanaged_view.h>
#endif

/* Function purpose: Draw the background image onto the output-sized surface
using the configured fit. Returns false when Cairo cannot render it, in which
case the caller must leave the output without a background rather than using
the half-drawn surface. */
static inline bool
render_image_to_surface(cairo_surface_t *output,
    cairo_surface_t *image,
    enum hikari_background_fit fit,
    double output_width,
    double output_height,
    double scale)
{
  cairo_t *cairo = cairo_create(output);
  if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
    cairo_destroy(cairo);
    return false;
  }

  /* Action purpose: The surface is allocated in physical pixels so a scaled
  output gets a sharp wallpaper, but every fit below is expressed in logical
  ones -- centring and tiling would otherwise shrink with the scale factor. */
  cairo_scale(cairo, scale, scale);

  double width = cairo_image_surface_get_width(image);
  double height = cairo_image_surface_get_height(image);

  cairo_rectangle(cairo, 0, 0, output_width, output_height);
  cairo_fill(cairo);

  if (fit == HIKARI_BACKGROUND_STRETCH) {
    // [COMMENT] Action purpose: A zero-dimension source image would divide
    // by zero here, poisoning the cairo context's matrix; bail out instead
    // of feeding cairo_scale an infinite/NaN factor.
    if (width <= 0 || height <= 0) {
      cairo_destroy(cairo);
      return false;
    }
    cairo_scale(cairo, output_width / width, output_height / height);
    cairo_set_source_surface(cairo, image, 0, 0);
  } else if (fit == HIKARI_BACKGROUND_CENTER || fit == HIKARI_BACKGROUND_TILE) {
    /* Action purpose: Neither of these resamples the image -- centring places
    it at its own size and tiling repeats it at its own period -- so both want
    one image pixel to land on one PHYSICAL pixel. The context is in logical
    units, so the pattern matrix undoes that: it maps a logical unit to `scale`
    image pixels, leaving the image sharp and its tile period equal to its pixel
    size rather than growing with the display's scale factor. */
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(image);

    double logical_width = width / scale;
    double logical_height = height / scale;

    cairo_matrix_t matrix;
    cairo_matrix_init_scale(&matrix, scale, scale);

    if (fit == HIKARI_BACKGROUND_CENTER) {
      cairo_matrix_translate(&matrix,
          -(output_width / 2 - logical_width / 2),
          -(output_height / 2 - logical_height / 2));
    } else {
      cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    }

    cairo_pattern_set_matrix(pattern, &matrix);
    cairo_set_source(cairo, pattern);
    cairo_pattern_destroy(pattern);
  }

  cairo_paint(cairo);

  cairo_status_t status = cairo_status(cairo);
  cairo_destroy(cairo);

  return status == CAIRO_STATUS_SUCCESS;
}

// Function purpose: (Re)load an output's wallpaper from a PNG file and
// paint it into a scene buffer at the output's current geometry. If the PNG
// path is missing, unreadable, or fails to render into an output-sized
// surface, the output is simply left without a background. Once the image
// has rendered successfully, a scene-buffer allocation or creation failure
// falls back to a solid-color wlr_scene_rect instead. Called on output init
// and whenever a config reload resolves a new background for the output.
void
hikari_output_load_background(struct hikari_output *output,
    const char *path,
    enum hikari_background_fit background_fit)
{
  if (output->background != NULL) {
    wlr_scene_node_destroy(output->background);
    output->background = NULL;
  }

  assert(output->background == NULL);

  if (path == NULL) {
    goto done;
  }

  cairo_surface_t *image = cairo_image_surface_create_from_png(path);
  if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
    fprintf(stderr,
        "error: could not load background \"%s\": %s\n",
        path,
        cairo_status_to_string(cairo_surface_status(image)));
    cairo_surface_destroy(image);
    goto done;
  }

  int output_width = output->geometry.width;
  int output_height = output->geometry.height;

  float scale = output->wlr_output->scale;
  if (scale <= 0.0f) {
    scale = 1.0f;
  }

  /* Action purpose: The scale is client-controlled through output management,
  and wlroots only rejects a non-positive one -- there is no upper bound, so the
  product can exceed what an int can hold and converting it would be undefined.
  Check the doubles before the conversion, not after. */
  double scaled_width = (double)output_width * scale;
  double scaled_height = (double)output_height * scale;

  if (!isfinite(scaled_width) || !isfinite(scaled_height) ||
      scaled_width > (double)INT_MAX - 1.0 ||
      scaled_height > (double)INT_MAX - 1.0) {
    cairo_surface_destroy(image);
    goto done;
  }

  int pixel_width = (int)(scaled_width + 0.5);
  int pixel_height = (int)(scaled_height + 0.5);

  if (pixel_width <= 0 || pixel_height <= 0) {
    cairo_surface_destroy(image);
    goto done;
  }

  cairo_surface_t *output_surface = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, pixel_width, pixel_height);
  if (cairo_surface_status(output_surface) != CAIRO_STATUS_SUCCESS) {
    fprintf(stderr,
        "error: could not allocate background surface for output \"%s\": %s\n",
        output->wlr_output->name,
        cairo_status_to_string(cairo_surface_status(output_surface)));
    cairo_surface_destroy(image);
    cairo_surface_destroy(output_surface);
    goto done;
  }

  bool rendered = render_image_to_surface(
      output_surface, image, background_fit, output_width, output_height, scale);
  if (!rendered || cairo_surface_status(output_surface) != CAIRO_STATUS_SUCCESS) {
    fprintf(stderr,
        "error: could not render background \"%s\" for output \"%s\"\n",
        path,
        output->wlr_output->name);
    cairo_surface_destroy(image);
    cairo_surface_destroy(output_surface);
    goto done;
  }

  unsigned char *data = cairo_image_surface_get_data(output_surface);
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pixel_width);

  size_t byte_count = (size_t)stride * (size_t)pixel_height;
  if (byte_count == 0 || byte_count / (size_t)stride != (size_t)pixel_height) {
    fprintf(stderr, "error: output background buffer size overflow\n");
    cairo_surface_destroy(image);
    cairo_surface_destroy(output_surface);
    goto done;
  }

  // [COMMENT] Action purpose: Graceful-degradation allocation. A background
  // image is cosmetic -- the function already has a solid-color scene-rect
  // fallback for when wlr_scene_buffer_create() fails below; an allocation
  // failure for the buffer itself now falls through to that same fallback
  // instead of aborting the compositor to display a wallpaper. See
  // DECISIONS_LOG Finding 4.
  struct wlr_buffer *bg_buffer =
      hikari_buffer_create_argb8888(pixel_width, pixel_height, data, stride);

  struct wlr_scene_buffer *scene_buffer = NULL;

  if (bg_buffer != NULL) {
    scene_buffer =
        wlr_scene_buffer_create(hikari_server.layers.background, bg_buffer);
  }

  if (scene_buffer != NULL) {
    output->background = &scene_buffer->node;

    /* Action purpose: The buffer is in physical pixels; the scene works in
    logical ones, so without this the wallpaper would overhang the screen by
    the scale factor. */
    wlr_scene_buffer_set_dest_size(scene_buffer, output_width, output_height);
    wlr_scene_node_set_position(
        output->background, output->geometry.x, output->geometry.y);
    wlr_scene_node_lower_to_bottom(output->background);
  } else {
    if (bg_buffer != NULL) {
      fprintf(stderr,
          "error: could not create scene buffer for background on output \"%s\"\n",
          output->wlr_output->name);
    } else {
      fprintf(stderr,
          "error: could not allocate background buffer for output \"%s\"; "
          "falling back to solid color\n",
          output->wlr_output->name);
    }

    /* [COMMENT] Action purpose: wlr_scene_rect_create() requires PREMULTIPLIED
    colour, while configuration colours are stored straight (Cairo's
    convention). The two only agree at alpha 1.0, so a `clear` configured as
    "#RRGGBBAA" would otherwise render too bright. */
    float color[4];
    hikari_color_premultiply(color, hikari_configuration->clear);

    struct wlr_scene_rect *rect = wlr_scene_rect_create(
        hikari_server.layers.background, output_width, output_height, color);
    if (rect != NULL) {
      output->background = &rect->node;
      wlr_scene_node_set_position(
          output->background, output->geometry.x, output->geometry.y);
      wlr_scene_node_lower_to_bottom(output->background);
    } else {
      fprintf(stderr,
          "error: could not create scene rect for background on output \"%s\"\n",
          output->wlr_output->name);
      output->background = NULL;
    }
  }

  // [COMMENT] Action purpose: wlr_buffer_drop(NULL) is unsafe (see Phase 38's
  // hikari_lock_indicator_fini fix); bg_buffer is NULL here whenever
  // allocation failed above, so guard the call.
  if (bg_buffer != NULL) {
    wlr_buffer_drop(bg_buffer);
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

  /* [COMMENT] Action purpose: Delegate so the enabled-output guard applies
  here too. This checked only scene_output, so it scheduled frames on disabled
  outputs -- lock_mode's disable_outputs() calls it immediately after
  hikari_output_disable(). */
  hikari_output_schedule_frame(output);
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

  /* Action purpose: An atomic modeset scans out of the primary plane, and
  wlroots only borrows the plane's existing framebuffer when the state carries
  none. An output that was fully torn down -- or that never came up -- has no
  such framebuffer, and the commit is then refused however valid the mode is.
  Setting a mode and rendering one frame from the scene supplies both. */
  if (wlr_output->current_mode == NULL) {
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
      wlr_output_state_set_mode(&state, mode);
    }
  }

  if (output->scene_output != NULL) {
    wlr_scene_output_build_state(output->scene_output, &state, NULL);
  }

  if (!wlr_output_commit_state(wlr_output, &state)) {
    fprintf(stderr,
        "error: failed to enable output \"%s\"; it may already be lit at "
        "the hardware level but will not receive frame updates\n",
        wlr_output->name);
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
void
hikari_output_update_geometry(struct hikari_output *output)
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

  /* [COMMENT] Action purpose: Reserve the top bar's strip before anything else
  consumes the usable area. Layer-shell's arrange pass re-derives usable_area
  from the full output box, so it applies the same reservation itself -- both
  paths must agree or views would be laid out under the bar. */
  hikari_bar_reserve(&output->bar, &output->usable_area);

  /* [COMMENT] Action purpose: Re-render the bar so it tracks the new output
  width and origin after a mode change or layout move. */
  hikari_bar_refresh(&output->bar);

  // [COMMENT] Action purpose: Reposition background scene node to match updated output geometry.
  if (output->background != NULL) {
    wlr_scene_node_set_position(output->background,
        output->geometry.x, output->geometry.y);
  }
}



#ifdef HAVE_LAYERSHELL
/* [COMMENT] Action purpose: wlr_layer_surface_v1_destroy() synchronously fires
the layer surface's destroy signal, and hikari's destroy_handler (layer_shell.c)
responds by unmapping, finalising, and freeing `layer` before the call returns.
`layer` is therefore already-freed memory by the time this loop regains
control -- it must not be touched afterwards. */
static void
close_layers(struct wl_list *layers)
{
  struct hikari_layer *layer, *layer_temp;
  wl_list_for_each_safe (layer, layer_temp, layers, layer_surfaces) {
    wlr_layer_surface_v1_destroy(layer->surface);
  }
}
#endif

static void
frame_handler(struct wl_listener *listener, void *data)
{
  struct hikari_output *output = wl_container_of(listener, output, frame);

  // [COMMENT] Action purpose: Skip frame commits while the session is inactive
  // (VT switched away) or the output is disabled. wlr_scene_output_commit on
  // an inactive CRTC returns false and can corrupt swapchain state. Both flags
  // are set/cleared by session_active_handler and hikari_output_enable.
  if (!hikari_server.session_active || !output->enabled) {
    return;
  }

  struct wlr_scene *scene = output->server->scene;

  struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
    scene, output->wlr_output);
  if (scene_output == NULL) {
    return;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  /* [COMMENT] Action purpose: Advance window motion BEFORE the commit, so this
  frame draws the positions this tick just computed rather than the previous
  frame's. Inert unless `ui { animation { enabled = true } }`, in which case it
  walks this output's views and repositions the ones still travelling.

  The next frame is requested here rather than after the commit so that a failed
  commit cannot strand an animation half way -- the early return below would
  otherwise skip the reschedule and leave the window parked mid-flight until
  something else damaged the output. */
  uint32_t now_msec = (uint32_t)((uint64_t)now.tv_sec * 1000 +
                                 (uint64_t)now.tv_nsec / 1000000);

  if (hikari_animation_tick(output, now_msec)) {
    wlr_output_schedule_frame(output->wlr_output);
  }

  // [COMMENT] Action purpose: Check the commit return value so failures are
  // logged rather than silently discarded. send_frame_done is only called on
  // success to avoid advancing client buffer timestamps after a failed commit.
  if (!wlr_scene_output_commit(scene_output, NULL)) {
    wlr_log(WLR_ERROR,
        "frame_handler: wlr_scene_output_commit failed for output %s",
        output->wlr_output->name);

    /* Action purpose: Ask for another frame, or this output never gets one
    again. No frame-done goes out when the commit fails, so a client throttled
    on frame callbacks stops committing; with a fullscreen window that client is
    the only thing damaging the output, so nothing reschedules and the picture
    is frozen until an unrelated damage source -- in practice moving the pointer
    across that screen -- happens to wake it.

    The damage survives: wlr_scene clears it from a commit listener, which a
    failed commit never reaches, so the retry redraws the same region rather
    than a stale one. */
    wlr_output_schedule_frame(output->wlr_output);
    return;
  }

  wlr_scene_output_send_frame_done(scene_output, &now);
}


static void
request_state_handler(struct wl_listener *listener, void *data)
{
  struct hikari_output *output =
      wl_container_of(listener, output, request_state);
  const struct wlr_output_event_request_state *event = data;

  // [COMMENT] Action purpose: Discard DRM state change requests while the
  // compositor is in the background (VT switched away). Forwarding an output
  // state commit to an inactive CRTC can corrupt hardware state and races
  // with the VT owner.
  if (!hikari_server.session_active) {
    return;
  }

  // [COMMENT] Action purpose: Guard against forwarding a disable-CRTC commit
  // from wlroots during initial DRM probing, before the output is fully
  // initialised by hikari. wlroots 0.20 may emit a request_state with
  // enabled=false while negotiating modeset feasibility; blindly forwarding
  // that commit produces "Failed to disable CRTC <N>" on startup.
  // WLR_OUTPUT_STATE_ENABLED (1<<3) presence in the committed bitmask
  // indicates the state is explicitly toggling the enabled flag — only then
  // do we inspect the value. If the field is not committed, forward as-is.
  if ((event->state->committed & WLR_OUTPUT_STATE_ENABLED) &&
      !event->state->enabled && !output->enabled) {
    return;
  }

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

  /* Action purpose: wlroots tears the client-facing head down by itself when
  the output goes away, but it only marks the manager dirty -- the `done` that
  tells clients the enumeration has settled is sent on the next publish. Without
  this an unplugged monitor leaves every output-management client waiting. Runs
  after the free, so the output is already out of hikari_server.outputs. */
  hikari_output_management_broadcast();
}

// [COMMENT] Function purpose: Initialize a new compositor output, allocating workspace and configuring state.
void
hikari_output_init(struct hikari_output *output, struct wlr_output *wlr_output)
{
  bool noop = wlr_output->backend == hikari_server.noop_backend;

  output->wlr_output = wlr_output;
  // [COMMENT] Action purpose: Ensure output->server is always valid after init,
  // regardless of caller. Frame handler (output.c:263) uses output->server->scene.
  output->server = &hikari_server;
  output->scene_output = NULL;
  output->lock_indicator_node = NULL;
  output->lock_backdrop_node = NULL;
  output->lock_clock_node = NULL;
  output->background = NULL;

  /* Action purpose: hikari_malloc() does not zero, and these two are otherwise
  written only by hikari_output_update_geometry(), which runs for real outputs
  only. The noop output becomes current when the last real one goes away, after
  which every geometry path would read an indeterminate box. */
  output->geometry = (struct wlr_box){ 0 };
  output->usable_area = (struct wlr_box){ 0 };

  /* [COMMENT] Action purpose: Initialise the bar before output_geometry() runs,
  since that path calls hikari_bar_reserve()/hikari_bar_refresh() on it. */
  hikari_bar_init(&output->bar, output);
  // [COMMENT] Action purpose: Establish the disabled baseline BEFORE asserting
  // on it. Callers (new_output_handler, init_noop_output) allocate the output
  // with hikari_malloc, which does not zero memory, so asserting first would
  // read an indeterminate value and could abort on a perfectly valid output in
  // debug builds.
  output->enabled = false;
  output->wants_enabled = false;
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
    output->wants_enabled = true;
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
    if (scene_output == NULL) {
      fprintf(stderr,
          "error: failed to create scene output for \"%s\"; output "
          "will remain disabled\n",
          wlr_output->name);

      struct wlr_output_state state;
      wlr_output_state_init(&state);
      wlr_output_state_set_enabled(&state, false);
      if (wlr_output_commit_state(wlr_output, &state)) {
        wl_list_remove(&output->server_outputs);
        wl_list_init(&output->server_outputs);
        wl_list_remove(&output->frame.link);
        wl_list_remove(&output->request_state.link);
        output->scene_output = NULL;
        output->enabled = false;
      }
      wlr_output_state_finish(&state);
      return;
    }
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

    // [COMMENT] Action purpose: wlr_output_layout_add can fail (allocation
    // failure). Roll back the enable/registration performed above and leave
    // the output disabled rather than passing a NULL l_output into the scene
    // layout, which would crash.
    if (l_output == NULL) {
      fprintf(stderr,
          "error: failed to add output \"%s\" to the output layout; output "
          "will remain disabled\n",
          wlr_output->name);

      struct wlr_output_state state;
      wlr_output_state_init(&state);
      wlr_output_state_set_enabled(&state, false);
      if (wlr_output_commit_state(wlr_output, &state)) {
        wl_list_remove(&output->server_outputs);
        wl_list_init(&output->server_outputs);
        wl_list_remove(&output->frame.link);
        wl_list_remove(&output->request_state.link);
        wlr_scene_output_destroy(scene_output);
        output->scene_output = NULL;
        output->enabled = false;
      }
      wlr_output_state_finish(&state);
      return;
    }

    wlr_scene_output_layout_add_output(hikari_server.scene_layout, l_output, scene_output);

    hikari_output_update_geometry(output);

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

static void
destroy_output_nodes(struct hikari_output *output)
{
  if (output->background != NULL) {
    wlr_scene_node_destroy(output->background);
    output->background = NULL;
  }

  if (output->lock_indicator_node != NULL) {
    wlr_scene_node_destroy(&output->lock_indicator_node->node);
    output->lock_indicator_node = NULL;
  }

  if (output->lock_backdrop_node != NULL) {
    wlr_scene_node_destroy(&output->lock_backdrop_node->node);
    output->lock_backdrop_node = NULL;
  }

  if (output->lock_clock_node != NULL) {
    wlr_scene_node_destroy(&output->lock_clock_node->node);
    output->lock_clock_node = NULL;
  }
}

// Function purpose: Hand an output's views and focus to another output.
static void
evacuate_output(struct hikari_output *output)
{
  struct hikari_workspace *workspace = output->workspace;
  struct hikari_workspace *next_workspace = hikari_workspace_next(workspace);
  struct hikari_workspace *merge_workspace;

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
}

/* Function purpose: Give an output its place in the layout and a scene output,
without touching the CRTC.

Separate from enabling it because the order is forced: a mode-setting commit
needs a framebuffer, the only way to produce one is to render a frame, and
rendering a frame needs the scene output to exist first. */
bool
hikari_output_attach(struct hikari_output *output)
{
  assert(output != NULL);

  if (output->scene_output != NULL) {
    return true;
  }

  /* Action purpose: geometry still holds the box this output had when it was
  last part of the layout, so it comes back where it left. */
  struct wlr_output_layout_output *l_output =
      wlr_output_layout_add(hikari_server.output_layout,
          output->wlr_output,
          output->geometry.x,
          output->geometry.y);

  if (l_output == NULL) {
    fprintf(stderr,
        "error: failed to add output \"%s\" back to the output layout\n",
        output->wlr_output->name);
    return false;
  }

  struct wlr_scene_output *scene_output =
      wlr_scene_output_create(hikari_server.scene, output->wlr_output);

  if (scene_output == NULL) {
    fprintf(stderr,
        "error: failed to recreate the scene output for \"%s\"\n",
        output->wlr_output->name);
    wlr_output_layout_remove(hikari_server.output_layout, output->wlr_output);
    return false;
  }

  output->scene_output = scene_output;
  wlr_scene_output_layout_add_output(
      hikari_server.scene_layout, l_output, scene_output);

  return true;
}

void
hikari_output_detach(struct hikari_output *output)
{
  assert(output != NULL);

  if (output->scene_output != NULL) {
    wlr_scene_output_destroy(output->scene_output);
    output->scene_output = NULL;
  }

  wlr_output_layout_remove(hikari_server.output_layout, output->wlr_output);

  /* Action purpose: The bar node is not owned by the output's scene output, so
  it survives at the coordinates the output has just vacated -- visible again
  the moment another output is positioned over that region. Only the node is
  switched off: bar->enabled still governs the usable-area reservation, and
  hikari_bar_refresh() re-asserts the node on the way back in. */
  if (output->bar.scene_buffer != NULL) {
    wlr_scene_node_set_enabled(&output->bar.scene_buffer->node, false);
  }
}

void
hikari_output_set_wants_enabled(
    struct hikari_output *output, bool wants_enabled)
{
  assert(output != NULL);

  if (output->wants_enabled == wants_enabled) {
    return;
  }

  if (!wants_enabled) {
    /* Action purpose: Cleared first so hikari_workspace_next() below skips this
    output when choosing where the views go. */
    output->wants_enabled = false;

    evacuate_output(output);
    destroy_output_nodes(output);
    hikari_output_disable(output);
    hikari_output_detach(output);
    return;
  }

  if (!hikari_output_attach(output)) {
    return;
  }

  output->wants_enabled = true;

  /* Action purpose: Lighting it now would break a blanked lock screen. The flag
  above is what matters -- enable_outputs() brings it up with the rest on the
  next keystroke. Same rule hikari_output_init() applies to a monitor connected
  while locked. */
  if (!hikari_server_in_lock_mode() ||
      !hikari_lock_mode_are_outputs_disabled(&hikari_server.lock_mode)) {
    hikari_output_enable(output);
  }

  hikari_output_update_geometry(output);

  /* Action purpose: A touch device naming this output was mapped to the whole
  layout while it was gone, because the lookup skips outputs that are not part
  of the desktop. Now that it is back, resolve them again. */
  hikari_server_map_touch_devices();

  struct hikari_output_config *output_config =
      hikari_configuration_resolve_output_config(
          hikari_configuration, output->wlr_output->name);

  if (output_config != NULL && output_config->background.value != NULL) {
    hikari_output_load_background(output,
        output_config->background.value,
        output_config->background_fit.value);
  }
}

// [COMMENT] Function purpose: Finalize and teardown an output, merging its workspace to another active output.
void
hikari_output_fini(struct hikari_output *output)
{
  bool noop = output->wlr_output->backend == hikari_server.noop_backend;

  /* [COMMENT] Action purpose: Destroy the bar's scene node before the rest of
  the output tears down, so no refresh can race a half-destroyed output. */
  hikari_bar_fini(&output->bar);

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
    destroy_output_nodes(output);
    evacuate_output(output);

    wl_list_remove(&output->server_outputs);
  } else {
    hikari_server.workspace = NULL;
  }

#ifdef HAVE_XWAYLAND
  /* [COMMENT] Action purpose: Last-resort detach of any override-redirect view
  still referencing this output. hikari_workspace_merge() above evacuates them
  on the normal path, but the noop-output branch never merges at all, and at
  shutdown there is no surviving workspace to evacuate to. This output and its
  workspace are both freed the moment this function returns, so anything left
  pointing at either would be holding freed memory -- this sweep exists to make
  that impossible rather than merely unlikely.

  Detaching sets workspace to NULL, which map_handler, unmap() and
  commit_handler all now treat as "this view has outlived its output" and bail
  on. That is the approved safe-bail policy: the view stops being hit-tested
  and stops damaging a dead output, while its own destroy_handler still tears
  down every listener correctly afterwards. */
  struct hikari_xwayland_unmanaged_view *unmanaged, *unmanaged_temp;
  wl_list_for_each_safe (unmanaged,
      unmanaged_temp,
      &output->unmanaged_xwayland_views,
      unmanaged_output_views) {
    /* Action purpose: On the noop path there is no surviving workspace to
    evacuate to, so leftover views are expected and this is routine. Anywhere
    else hikari_workspace_merge() should already have emptied the list, and a
    remaining view means that failed -- worth an error. */
    wlr_log(noop ? WLR_DEBUG : WLR_ERROR,
        "hikari_output_fini: override-redirect view %p still linked to output "
        "%s at teardown; detaching to avoid a dangling reference",
        (void *)unmanaged,
        output->wlr_output->name);

    hikari_xwayland_unmanaged_detach(unmanaged);
  }
#endif

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
    while (name != &output->server_outputs) {                                  \
      if (name == &hikari_server.outputs) {                                    \
        name = hikari_server.outputs.name;                                     \
        continue;                                                              \
      }                                                                        \
                                                                               \
      struct hikari_output *name##_output =                                    \
          wl_container_of(name, name##_output, server_outputs);                \
                                                                               \
      if (name##_output->wants_enabled) {                                      \
        return name##_output;                                                  \
      }                                                                        \
                                                                               \
      name = name->name;                                                       \
    }                                                                          \
                                                                               \
    return output;                                                             \
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
