#include <hikari/lock_indicator.h>

#include <hikari/buffer.h>

#include <drm_fourcc.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <wlr/backend.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

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

  lock_indicator->message = NULL;
  lock_indicator->message_width = 0;
  lock_indicator->message_height = 0;

  lock_indicator->reset_state = wl_event_loop_add_timer(
      hikari_server.event_loop, reset_state_handler, lock_indicator);
  if (lock_indicator->reset_state == NULL) {
    wlr_log(WLR_ERROR,
        "hikari_lock_indicator_init: failed to create reset-state timer -- "
        "indicator auto-reset disabled for this lock session");
  }
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

    // [COMMENT] Action purpose: The message node goes the same way and for the
    // same reason -- the lock layer is reused by the next lock rather than
    // recreated, so a diagnostic left parented to it would reappear on the
    // next lock screen explaining a refusal from the previous session.
    if (output->lock_message_node != NULL) {
      wlr_scene_node_destroy(&output->lock_message_node->node);
      output->lock_message_node = NULL;
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
  if (lock_indicator->message != NULL) {
    wlr_buffer_drop(lock_indicator->message);
    lock_indicator->message = NULL;
  }

  if (lock_indicator->reset_state != NULL) {
    wl_event_source_remove(lock_indicator->reset_state);
  }
}

void
hikari_lock_indicator_set_type(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  lock_indicator->current = lock_indicator->type;
  hikari_lock_indicator_damage(lock_indicator);
  if (lock_indicator->reset_state != NULL) {
    wl_event_source_timer_update(lock_indicator->reset_state, 100);
  }
}

void
hikari_lock_indicator_set_verify(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  lock_indicator->current = lock_indicator->verify;
  hikari_lock_indicator_damage(lock_indicator);
  if (lock_indicator->reset_state != NULL) {
    wl_event_source_timer_update(lock_indicator->reset_state, 0);
  }
}

void
hikari_lock_indicator_set_deny(struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  lock_indicator->current = lock_indicator->deny;
  hikari_lock_indicator_damage(lock_indicator);
  if (lock_indicator->reset_state != NULL) {
    wl_event_source_timer_update(lock_indicator->reset_state, 1000);
  }
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
  if (lock_indicator->reset_state != NULL) {
    wl_event_source_timer_update(lock_indicator->reset_state, 0);
  }
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

/* [COMMENT] Function purpose: Forward declaration so the damage path below can
reach the message placement defined further down, without moving either
function away from the code it belongs next to. */
static void damage_message(struct hikari_lock_indicator *lock_indicator);

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

  /* [COMMENT] Action purpose: Place the message on every output too, for the
  same reason the loop above places the circles: an output that appeared after
  the message was set has no message node yet. hikari_output_init() nulls
  lock_message_node along with lock_indicator_node, and damage_message() was
  reachable only from hikari_lock_indicator_set_message(), so a monitor plugged
  in while a diagnostic was on screen got the circle back on the next keystroke
  and never got the message -- until a new message happened to arrive.

  Guarded on message, so this is a no-op whenever no diagnostic is displayed,
  which is the usual case. When one is, the work is the same kind the circle
  path above already does on every call: set_buffer, position, enable, raise. */
  if (lock_indicator->message != NULL) {
    damage_message(lock_indicator);
  }
}

/* [COMMENT] Function purpose: Apply the one layout configuration the
diagnostic line uses, so the measuring pass and the drawing pass cannot drift
apart.

[COMMENT] Action purpose: The wrap width and the three-line cap are the bound
on a buffer whose content comes from a PAM module rather than from hikari.
Measured with pangocairo on this host: with this configuration, 500 bytes of
text -- one unbreakable 500-character word, a hundred short words, anything in
between -- produce exactly 3 lines and a 600x51 pixel layout, and the ellipsis
says that something was dropped.

The cap holds only because the text cannot contain a mandatory line break.
pango_layout_set_height() with a negative value limits WRAPPED lines and does
nothing about paragraph breaks: the same measurement gave 251 lines and a
4267-pixel-tall layout for 250 repetitions of "x\n". sanitize_message() in
src/lock_mode.c is what guarantees the input has none, and the two belong
together.

"Mandatory line break" and not "newline", because the set is wider than it
looks and getting it wrong cost this cap once already. U+2029 PARAGRAPH
SEPARATOR is category Zp rather than Cc, so it survived a filter that dropped
every control codepoint, and it produced the identical 251 lines and
4267 pixels that "x\n" does. sanitize_message() now drops U+2028 and U+2029
by name alongside the controls; if that filter is ever narrowed, this cap
stops being a bound. */
static void
configure_message_layout(PangoLayout *layout, const char *text)
{
  const int max_width = 600;
  const int max_lines = 3;

  pango_layout_set_font_description(
      layout, hikari_configuration->lock.date_font.desc);
  pango_layout_set_text(layout, text, -1);
  pango_layout_set_width(layout, max_width * PANGO_SCALE);
  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
  pango_layout_set_height(layout, -max_lines);
  pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
}

/* [COMMENT] Function purpose: Render the diagnostic line into a single
wlr_buffer, laid out the way the indicator circles above are: one buffer at
logical scale, shared by every output and positioned per output, rather than
re-rendered per output the way the clock is. A diagnostic line does not warrant
the per-output scale handling the clock carries.

Returns NULL and writes nothing on any failure, which the caller treats as
"keep whatever was on screen" rather than as fatal.

[COMMENT] Action purpose: The font and both colours are existing, already
parsed, already reloadable configuration -- the date face and the clock colour
from the lock block, and the conflict colour the deny circle is drawn in. No
new configuration key is introduced for a diagnostic line.

The text is drawn twice, offset black first. That is not decoration: the line
sits over a blurred photograph of the user's own desktop whose brightness is
unknown, and the shadow is what guarantees an edge against a pale wallpaper.
The same reasoning and the same offset as draw_centred() in src/lock_clock.c. */
static struct wlr_buffer *
render_message(
    const char *text, bool is_error, int *out_width, int *out_height)
{
  const int margin = 8;

  cairo_surface_t *measure_surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t *measure_cairo = cairo_create(measure_surface);
  PangoLayout *measure_layout = pango_cairo_create_layout(measure_cairo);

  configure_message_layout(measure_layout, text);
  pango_cairo_update_layout(measure_cairo, measure_layout);

  /* [COMMENT] Action purpose: get_pixel_extents() and not get_pixel_size(),
  because the latter reports only the logical width and height and discards the
  logical x. configure_message_layout() above sets a 600 pixel layout width
  together with PANGO_ALIGN_CENTER, so pango offsets every line by roughly
  (600 - line_width) / 2 inside that layout. The surface below is sized from the
  TEXT width, so drawing at the layout origin puts the glyphs that far to the
  right of a buffer that has no room for them.

  Measured with pangocairo on this host, font "sans 20": "Login incorrect" --
  the ordinary pam_unix failure string -- measures 190 pixels wide with a
  logical x of 205, is given a 206 pixel surface, and lands at x=213..403. The
  buffer handed to hikari_buffer_create_argb8888() below contained zero
  non-transparent pixels: the user saw a deny circle and no reason for it.
  "Sorry, try again." kept 5% of its pixels, "Permission denied" 23%.

  Only a line that fills the whole 600 pixel wrap width has a logical x of 0.
  That is why the 500-byte filler the comment above configure_message_layout()
  measures renders perfectly while every real diagnostic does not, and it is why
  this survived the measurement that produced those numbers. The offset is
  subtracted at both draw sites below, which keeps the lines centred relative to
  one another -- switching to PANGO_ALIGN_LEFT would also fix the clipping, but
  it would left-align a multi-line message instead. */
  PangoRectangle logical;
  pango_layout_get_pixel_extents(measure_layout, NULL, &logical);

  int text_width = logical.width;
  int text_height = logical.height;
  int x_offset = logical.x;
  int y_offset = logical.y;

  g_object_unref(measure_layout);
  cairo_destroy(measure_cairo);
  cairo_surface_destroy(measure_surface);

  if (text_width <= 0 || text_height <= 0) {
    return NULL;
  }

  int width = text_width + margin * 2;
  int height = text_height + margin * 2;

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return NULL;
  }

  cairo_t *cairo = cairo_create(surface);
  PangoLayout *layout = pango_cairo_create_layout(cairo);

  configure_message_layout(layout, text);
  pango_cairo_update_layout(cairo, layout);

  float *color = is_error ? hikari_configuration->indicator_conflict
                          : hikari_configuration->lock.clock_color;

  cairo_set_source_rgba(cairo, 0, 0, 0, 0.45);
  cairo_move_to(cairo, margin + 2 - x_offset, margin + 2 - y_offset);
  pango_cairo_show_layout(cairo, layout);

  cairo_set_source_rgba(cairo, color[0], color[1], color[2], color[3]);
  cairo_move_to(cairo, margin - x_offset, margin - y_offset);
  pango_cairo_show_layout(cairo, layout);

  cairo_surface_flush(surface);

  unsigned char *data = cairo_image_surface_get_data(surface);
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);

  struct wlr_buffer *buffer =
      hikari_buffer_create_argb8888(width, height, data, stride);

  g_object_unref(layout);
  cairo_destroy(cairo);
  cairo_surface_destroy(surface);

  if (buffer == NULL) {
    return NULL;
  }

  *out_width = width;
  *out_height = height;

  return buffer;
}

/* [COMMENT] Function purpose: Point every output's message node at the current
message buffer and place it, mirroring hikari_lock_indicator_damage() below for
the circles. */
static void
damage_message(struct hikari_lock_indicator *lock_indicator)
{
  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    if (output->lock_message_node == NULL) {
      output->lock_message_node = wlr_scene_buffer_create(
          hikari_server.layers.lock, lock_indicator->message);
    } else {
      wlr_scene_buffer_set_buffer(
          output->lock_message_node, lock_indicator->message);
    }

    // [COMMENT] Action purpose: Guard against NULL from a failed
    // wlr_scene_buffer_create before positioning or enabling, exactly as the
    // circle path does. Losing the diagnostic is not a reason to skip the
    // remaining outputs.
    if (output->lock_message_node == NULL) {
      continue;
    }

    /* [COMMENT] Action purpose: Centred horizontally and placed in the band
    below the indicator. The indicator is 100px tall and centred on the output
    (get_geometry() above), and the clock reserves the 40px above it and
    everything higher (src/lock_clock.c), so the free space on this screen
    starts 40px below the indicator's lower edge. Coordinates are
    layout-absolute because the lock layer is parented to the scene root rather
    than to a per-output tree.

    Clamped to the output afterwards: the band is only free on a screen tall
    enough to have one, and a short mode would otherwise push a three-line
    message off the bottom where it cannot be read at all. */
    int x = output->geometry.x +
        (output->geometry.width - lock_indicator->message_width) / 2;
    int y = output->geometry.y + output->geometry.height / 2 + 50 + 40;

    int bottom = output->geometry.y + output->geometry.height;
    if (y + lock_indicator->message_height > bottom) {
      y = bottom - lock_indicator->message_height;
    }
    if (y < output->geometry.y) {
      y = output->geometry.y;
    }

    wlr_scene_node_set_position(&output->lock_message_node->node, x, y);
    wlr_scene_node_set_enabled(&output->lock_message_node->node, true);
    wlr_scene_node_raise_to_top(&output->lock_message_node->node);
  }
}

void
hikari_lock_indicator_set_message(
    struct hikari_lock_indicator *lock_indicator,
    const char *text,
    bool is_error)
{
  assert(lock_indicator != NULL);

  if (text == NULL || text[0] == '\0') {
    hikari_lock_indicator_clear_message(lock_indicator);
    return;
  }

  int width = 0;
  int height = 0;
  struct wlr_buffer *buffer = render_message(text, is_error, &width, &height);

  if (buffer == NULL) {
    return;
  }

  /* [COMMENT] Action purpose: Repoint the scene nodes at the new buffer BEFORE
  dropping the previous one. Each node holds its own reference, so the order is
  not strictly required for correctness, but doing it this way means there is
  never a moment at which a node is the last thing referring to a buffer this
  code has already disowned. */
  struct wlr_buffer *previous = lock_indicator->message;

  lock_indicator->message = buffer;
  lock_indicator->message_width = width;
  lock_indicator->message_height = height;

  damage_message(lock_indicator);

  if (previous != NULL) {
    wlr_buffer_drop(previous);
  }
}

void
hikari_lock_indicator_clear_message(
    struct hikari_lock_indicator *lock_indicator)
{
  assert(lock_indicator != NULL);

  if (lock_indicator->message == NULL) {
    return;
  }

  // [COMMENT] Action purpose: Hide rather than destroy, matching how the
  // circles are taken down. The nodes are reused by the next message and are
  // destroyed for good in hikari_lock_indicator_fini().
  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    if (output->lock_message_node != NULL) {
      wlr_scene_node_set_enabled(&output->lock_message_node->node, false);
    }
  }

  wlr_buffer_drop(lock_indicator->message);
  lock_indicator->message = NULL;
  lock_indicator->message_width = 0;
  lock_indicator->message_height = 0;
}
