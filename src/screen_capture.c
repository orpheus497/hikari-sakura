// [COMMENT] Script function and purpose: Photograph an output's composited
// contents into CPU memory, so the lock screen can show a blurred still of the
// workspace the user was looking at.

#include <hikari/screen_capture.h>

#include <stdlib.h>
#include <string.h>

#include <drm_fourcc.h>

#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/server.h>

/* [COMMENT] Function purpose: Render the scene into a buffer of our own and
hand back the buffer, locked.

wlroots has no "give me a picture of this output" call for compositors -- the
screencopy protocol serves clients, not the compositor itself. What it does have
is wlr_scene_output_build_state()'s `swapchain` option, which renders the scene
into a swapchain we supply instead of the output's own and reports the resulting
buffer through the output state. Building a state and never committing it is the
supported way to render an output off-screen.

Returns NULL when no format the renderer accepts could be allocated. */
static struct wlr_buffer *
render_output_offscreen(struct hikari_output *output, uint32_t *format_out)
{
  struct wlr_output *wlr_output = output->wlr_output;

  /* [COMMENT] Action purpose: Size the swapchain by the TRANSFORMED resolution.
  wlroots validates a committed buffer with
  `assert(buffer->width == resolution_width && ...)` where those come from
  wlr_output_transformed_resolution(), so that is the size the scene renders at
  -- the rotation is baked into the rendered buffer, not applied afterwards at
  scanout. A 90 or 270 degree output therefore renders into a buffer with width
  and height swapped relative to its mode, and a swapchain sized from
  wlr_output.width/height would be rejected. */
  int width, height;
  wlr_output_transformed_resolution(wlr_output, &width, &height);

  if (width <= 0 || height <= 0) {
    return NULL;
  }

  /* [COMMENT] Action purpose: Format escalation ladder. wlroots 0.20 exposes
  wlr_renderer_get_texture_formats() but no equivalent query for render TARGET
  formats, so the right choice cannot simply be looked up -- it has to be tried.
  Implicit modifiers come first because that is what the GBM allocator uses for
  an ordinary render target; LINEAR is the fallback that a driver refusing
  implicit modifiers will still accept; ARGB8888 last, for a renderer that will
  not render into an X-channel format. Each rung is attempted in turn and the
  one that works is logged, so a failure here is diagnosable from the log rather
  than presenting as "the blur silently did nothing". */
  static const struct {
    uint32_t format;
    uint64_t modifier;
    const char *name;
  } candidates[] = {
    { DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_INVALID, "XRGB8888 (implicit)" },
    { DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, "XRGB8888 (linear)" },
    { DRM_FORMAT_ARGB8888, DRM_FORMAT_MOD_INVALID, "ARGB8888 (implicit)" },
    { DRM_FORMAT_ARGB8888, DRM_FORMAT_MOD_LINEAR, "ARGB8888 (linear)" },
  };

  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    /* [COMMENT] Action purpose: Build the format through wlr_drm_format_set_add()
    rather than by filling in a struct wlr_drm_format directly.

    That struct has invariants the compositor is not meant to maintain -- its
    `capacity` field is documented "do not use" -- and wlroots asserts on them:
    `format->len > 0` in render/allocator/gbm.c, and
    `src->len <= src->capacity` in render/drm_format_set.c. Hand-constructing it
    violated one of those on each of two successive attempts, aborting the
    compositor both times. The set API allocates the modifier array and keeps
    len and capacity consistent, so there is nothing left to get wrong.

    DRM_FORMAT_MOD_INVALID is the sentinel requesting implicit modifiers: the
    allocator tries gbm_bo_create_with_modifiers2() and, finding INVALID listed,
    is permitted to fall back to a plain modifier-less gbm_bo_create(). An empty
    list does NOT mean that -- it is simply invalid.

    It is also why this ladder can only recover from *returned* failures. An
    assertion inside a dependency is not recoverable: it takes the process down
    before this loop regains control, so every rung must be independently
    well-formed rather than relying on the next one to catch it. */
    struct wlr_drm_format_set format_set = { 0 };

    if (!wlr_drm_format_set_add(
            &format_set, candidates[i].format, candidates[i].modifier)) {
      wlr_drm_format_set_finish(&format_set);
      continue;
    }

    const struct wlr_drm_format *format =
        wlr_drm_format_set_get(&format_set, candidates[i].format);
    if (format == NULL) {
      wlr_drm_format_set_finish(&format_set);
      continue;
    }

    struct wlr_swapchain *swapchain =
        wlr_swapchain_create(hikari_server.allocator, width, height, format);

    // [COMMENT] Action purpose: wlr_swapchain_create() deep-copies the format,
    // so the set can be released as soon as it returns either way.
    wlr_drm_format_set_finish(&format_set);

    if (swapchain == NULL) {
      continue;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);

    /* [COMMENT] Action purpose: Force the scene to actually render this frame.
    wlr_scene_output_build_state() tracks damage, and a screen that has not
    changed since the last commit has none -- which is the normal state of an
    idle desktop at the moment someone locks it. Without this the capture would
    work while something on screen was animating and silently produce nothing
    the rest of the time, which is the worst possible failure shape: it would
    look like an intermittent bug rather than a missing frame.

    wlr_output_update_needs_frame() lives in wlroots' backend-implementer
    header rather than the compositor-facing one, which is why this file
    includes <wlr/interfaces/wlr_output.h> -- the same precedent src/buffer.c
    sets with <wlr/interfaces/wlr_buffer.h>. Setting the flag on our own output
    is benign: at worst it costs one extra render, and the lock screen needs a
    frame immediately afterwards regardless. */
    wlr_output_update_needs_frame(wlr_output);

    struct wlr_scene_output_state_options options = { .swapchain = swapchain };

    struct wlr_buffer *buffer = NULL;
    if (wlr_scene_output_build_state(output->scene_output, &state, &options) &&
        (state.committed & WLR_OUTPUT_STATE_BUFFER) && state.buffer != NULL) {
      /* [COMMENT] Action purpose: Take our own reference before the state is
      finished. wlr_output_state_finish() releases the state's reference, and
      destroying the swapchain below releases the slot -- without this lock the
      pixels would be freed out from under the caller. */
      buffer = wlr_buffer_lock(state.buffer);
      *format_out = candidates[i].format;
    }

    wlr_output_state_finish(&state);
    wlr_swapchain_destroy(swapchain);

    if (buffer != NULL) {
      wlr_log(WLR_DEBUG,
          "screen_capture: output %s captured as %s",
          wlr_output->name,
          candidates[i].name);
      return buffer;
    }
  }

  return NULL;
}

bool
hikari_screen_capture_init(
    struct hikari_screen_capture *capture, struct hikari_output *output)
{
  if (capture == NULL || output == NULL || output->scene_output == NULL ||
      !output->enabled) {
    return false;
  }

  uint32_t format = DRM_FORMAT_XRGB8888;
  struct wlr_buffer *buffer = render_output_offscreen(output, &format);
  if (buffer == NULL) {
    wlr_log(WLR_ERROR,
        "screen_capture: could not render output %s off-screen; the lock "
        "screen will fall back to a plain backdrop",
        output->wlr_output->name);
    return false;
  }

  bool success = false;
  unsigned char *data = NULL;

  /* [COMMENT] Action purpose: Read the pixels back through the texture API
  rather than by mapping the buffer. wlr_texture_read_pixels() is implemented
  with glReadPixels on the GLES2 renderer, so it never needs the buffer to be
  CPU-mappable -- which matters because a GBM scanout buffer generally is not.
  This is the same reason the compositor's own UI buffers are hand-rolled; see
  BLUEPRINT.md section 13, FB-2. */
  struct wlr_texture *texture =
      wlr_texture_from_buffer(hikari_server.renderer, buffer);
  if (texture == NULL) {
    wlr_log(WLR_ERROR,
        "screen_capture: could not create a texture from the captured buffer "
        "for output %s",
        output->wlr_output->name);
    goto done;
  }

  int width = buffer->width;
  int height = buffer->height;
  int stride = width * 4;

  size_t byte_count = (size_t)stride * (size_t)height;
  if (byte_count == 0 || byte_count / (size_t)stride != (size_t)height) {
    goto done_texture;
  }

  data = hikari_try_malloc(byte_count);
  if (data == NULL) {
    goto done_texture;
  }

  /* [COMMENT] Action purpose: Read into ARGB8888 regardless of what the buffer
  itself is. The capture may have been allocated as XRGB8888, which carries no
  alpha; requesting ARGB8888 here makes the readback fill the alpha channel with
  0xff, which is what the blur and the cairo compositing downstream expect. */
  struct wlr_texture_read_pixels_options read_options = {
    .data = data,
    .format = DRM_FORMAT_ARGB8888,
    .stride = (uint32_t)stride,
    .dst_x = 0,
    .dst_y = 0,
  };

  if (!wlr_texture_read_pixels(texture, &read_options)) {
    wlr_log(WLR_ERROR,
        "screen_capture: pixel readback failed for output %s",
        output->wlr_output->name);
    hikari_free(data);
    data = NULL;
    goto done_texture;
  }

  /* [COMMENT] Action purpose: Force every pixel opaque. The capture is usually
  allocated as XRGB8888, which carries no alpha at all, so what the readback
  writes into the alpha byte depends on the driver -- and a backdrop that came
  back with alpha 0 would simply not be visible, which is a hard failure to
  diagnose from a screenshot of a lock screen that looks unblurred.

  It also makes the blur correct. ARGB8888 is premultiplied by wlr_scene's
  convention, and averaging premultiplied and straight pixels together gives
  different answers -- but at full alpha the two representations coincide, so
  normalising here means box_blur_line() can average the channels directly
  without needing to know which it was handed. */
  for (int y = 0; y < height; y++) {
    unsigned char *row = data + (size_t)y * stride;

    for (int x = 0; x < width; x++) {
      row[x * 4 + 3] = 0xff;
    }
  }

  capture->data = data;
  capture->width = width;
  capture->height = height;
  capture->stride = stride;
  data = NULL;
  success = true;

done_texture:
  wlr_texture_destroy(texture);

done:
  wlr_buffer_unlock(buffer);
  hikari_free(data);

  return success;
}

void
hikari_screen_capture_fini(struct hikari_screen_capture *capture)
{
  if (capture == NULL) {
    return;
  }

  hikari_free(capture->data);
  capture->data = NULL;
  capture->width = 0;
  capture->height = 0;
  capture->stride = 0;
}
