#include <hikari/lock_indicator.h>

#include <hikari/buffer.h>

#include <drm_fourcc.h>

#include <wlr/backend.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_scene.h>

#include <hikari/configuration.h>
#include <hikari/geometry.h>
#include <hikari/output.h>
#include <hikari/server.h>

#define HIKARI_PI 3.14159265358979323846

// [COMMENT] Function purpose: Initialize a colored indicator circle as a
// wlr_buffer.
static struct wlr_buffer *
init_indicator_circle(float color[static 4])
{
  const int size = 100;

  struct wlr_buffer *wlr_buffer;

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);

  cairo_t *cairo = cairo_create(surface);
  PangoLayout *layout = pango_cairo_create_layout(cairo);

  float *border_inactive = hikari_configuration->border_active;
  cairo_set_source_rgba(cairo,
      border_inactive[0],
      border_inactive[1],
      border_inactive[2],
      border_inactive[3]);
  cairo_set_line_width(cairo, 5);
  cairo_translate(cairo, size / 2.0, size / 2.0);
  cairo_arc(cairo, 0, 0, (size - 5) / 2.0, 0, 2 * HIKARI_PI);
  cairo_stroke_preserve(cairo);
  cairo_set_source_rgba(cairo, color[0], color[1], color[2], color[3]);
  cairo_fill(cairo);

  cairo_surface_flush(surface);

  unsigned char *data = cairo_image_surface_get_data(surface);
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, size);

  wlr_buffer = hikari_buffer_create_argb8888(size, size, data, stride);

  cairo_surface_destroy(surface);
  g_object_unref(layout);
  cairo_destroy(cairo);

  return wlr_buffer;
}

static int
reset_state_handler(void *data)
{
  struct hikari_lock_indicator *lock_indicator = data;

  if (lock_indicator->current == lock_indicator->deny) {
    hikari_lock_indicator_clear(lock_indicator);
  } else {
    hikari_lock_indicator_set_wait(lock_indicator);
  }

  return 0;
}

// [COMMENT] Function purpose: Initialize lock indicator state.
void
hikari_lock_indicator_init(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  lock_indicator->wait = init_indicator_circle(hikari_configuration->clear);
  lock_indicator->type =
      init_indicator_circle(hikari_configuration->indicator_insert);
  lock_indicator->verify =
      init_indicator_circle(hikari_configuration->indicator_selected);
  lock_indicator->deny =
      init_indicator_circle(hikari_configuration->indicator_conflict);

  lock_indicator->current = NULL;

  lock_indicator->reset_state = wl_event_loop_add_timer(
      hikari_server.event_loop, reset_state_handler, lock_indicator);
}

// [COMMENT] Function purpose: Finalize lock indicator state, destroy scene
// nodes on all outputs, and drop indicator buffers.
void
hikari_lock_indicator_fini(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  // [COMMENT] Action purpose: Destroy lock indicator scene nodes on all outputs
  // to ensure no indicator remains visible when lock mode ends.
  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    if (output->lock_indicator_node != NULL) {
      wlr_scene_node_destroy(&output->lock_indicator_node->node);
      output->lock_indicator_node = NULL;
    }
  }

  // [COMMENT] Action purpose: Guard each drop against NULL. init_indicator_circle
  // returns NULL when buffer creation fails (no ARGB8888 texture format,
  // allocator failure, or a failed mapped-write), and wlr_buffer_drop does not
  // accept NULL. Without these guards, unlocking after any indicator buffer
  // failed to allocate dereferences a null pointer during teardown.
  if (lock_indicator->wait != NULL) {
    wlr_buffer_drop(lock_indicator->wait);
  }
  if (lock_indicator->type != NULL) {
    wlr_buffer_drop(lock_indicator->type);
  }
  if (lock_indicator->verify != NULL) {
    wlr_buffer_drop(lock_indicator->verify);
  }
  if (lock_indicator->deny != NULL) {
    wlr_buffer_drop(lock_indicator->deny);
  }

  wl_event_source_remove(lock_indicator->reset_state);
}

void
hikari_lock_indicator_set_type(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  lock_indicator->current = lock_indicator->type;
  hikari_lock_indicator_damage(lock_indicator);
  wl_event_source_timer_update(lock_indicator->reset_state, 100);
}

void
hikari_lock_indicator_set_verify(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  lock_indicator->current = lock_indicator->verify;
  hikari_lock_indicator_damage(lock_indicator);
  wl_event_source_timer_update(lock_indicator->reset_state, 0);
}

void
hikari_lock_indicator_set_deny(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  lock_indicator->current = lock_indicator->deny;
  hikari_lock_indicator_damage(lock_indicator);
  wl_event_source_timer_update(lock_indicator->reset_state, 1000);
}

void
hikari_lock_indicator_set_wait(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  lock_indicator->current = lock_indicator->wait;
  hikari_lock_indicator_damage(lock_indicator);
}

void
hikari_lock_indicator_clear(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  lock_indicator->current = NULL;
  hikari_lock_indicator_damage(lock_indicator);
  wl_event_source_timer_update(lock_indicator->reset_state, 0);
}

static inline void
get_geometry(struct hikari_output *output, struct wlr_box *geometry)
{
  const int size = 100;

  geometry->width = size;
  geometry->height = size;

  struct wlr_box output_geometry = { .x = 0,
    .y = 0,
    .width = output->geometry.width,
    .height = output->geometry.height };

  hikari_geometry_position_center(
      geometry, &output_geometry, &geometry->x, &geometry->y);
}

// [COMMENT] Function purpose: Update or damage lock indicator rendering on all
// outputs.
void
hikari_lock_indicator_damage(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  struct hikari_output *output;
  // [COMMENT] Action purpose: Iterate over all outputs to update lock indicator
  // nodes.
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    // [COMMENT] Action purpose: Determine if lock indicator should be visible
    // or hidden.
    if (lock_indicator->current != NULL) {
      // [COMMENT] Action purpose: Create new scene buffer node if none exists.
      if (output->lock_indicator_node == NULL) {
        output->lock_indicator_node = wlr_scene_buffer_create(
            hikari_server.layers.lock, lock_indicator->current);
      } else {
        wlr_scene_buffer_set_buffer(
            output->lock_indicator_node, lock_indicator->current);
      }
      // [COMMENT] Action purpose: Guard against NULL from failed
      // wlr_scene_buffer_create before positioning or enabling.
      if (output->lock_indicator_node != NULL) {
        struct wlr_box geometry;
        get_geometry(output, &geometry);
        wlr_scene_node_set_position(
            &output->lock_indicator_node->node, geometry.x + output->geometry.x, geometry.y + output->geometry.y);
        wlr_scene_node_set_enabled(&output->lock_indicator_node->node, true);
        wlr_scene_node_raise_to_top(&output->lock_indicator_node->node);
      }
    } else {
      // [COMMENT] Action purpose: Guard against dereferencing a null lock
      // indicator node when hiding.
      if (output->lock_indicator_node != NULL) {
        wlr_scene_node_set_enabled(&output->lock_indicator_node->node, false);
      }
    }
  }
}
