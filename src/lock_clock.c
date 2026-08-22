// [COMMENT] Script function and purpose: The lock screen's clock. Renders the
// time and date with cairo/Pango into a CPU buffer per output and attaches it
// to the lock layer, on a timer that ticks on the minute.

#include <hikari/lock_clock.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include <hikari/buffer.h>
#include <hikari/configuration.h>
#include <hikari/output.h>
#include <hikari/server.h>

/* [COMMENT] Function purpose: Shape one line of text and report the pixel size
Pango will need for it, without drawing anything.

Sizing has to happen before the surface is allocated, and the only reliable way
to know how large a Pango layout is is to build it. hikari_font's cached
character_width is no help here: the clock is set in a proportional face at a
large size, where per-character widths differ. */
static void
measure(struct hikari_font *font, const char *text, int *width, int *height)
{
  *width = 0;
  *height = 0;

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t *cairo = cairo_create(surface);
  PangoLayout *layout = pango_cairo_create_layout(cairo);

  pango_layout_set_font_description(layout, font->desc);
  pango_layout_set_text(layout, text, -1);
  pango_cairo_update_layout(cairo, layout);
  pango_layout_get_pixel_size(layout, width, height);

  g_object_unref(layout);
  cairo_destroy(cairo);
  cairo_surface_destroy(surface);
}

/* [COMMENT] Function purpose: Draw one line centred horizontally at `y`, with a
soft shadow behind it.

The shadow is not decoration. The clock sits over a blurred photograph of the
user's own desktop, whose brightness is entirely unknown -- white text alone
disappears against a pale wallpaper. Drawing the same glyphs in translucent
black at a small offset first guarantees an edge whatever is behind it, which is
cheaper and more predictable than measuring the backdrop and picking a colour. */
static void
draw_centred(cairo_t *cairo,
    struct hikari_font *font,
    const char *text,
    int surface_width,
    int y,
    float color[static 4])
{
  PangoLayout *layout = pango_cairo_create_layout(cairo);

  pango_layout_set_font_description(layout, font->desc);
  pango_layout_set_text(layout, text, -1);
  pango_cairo_update_layout(cairo, layout);

  int width, height;
  pango_layout_get_pixel_size(layout, &width, &height);

  double x = (surface_width - width) / 2.0;

  cairo_set_source_rgba(cairo, 0, 0, 0, 0.45);
  cairo_move_to(cairo, x + 2, y + 2);
  pango_cairo_show_layout(cairo, layout);

  cairo_set_source_rgba(cairo, color[0], color[1], color[2], color[3]);
  cairo_move_to(cairo, x, y);
  pango_cairo_show_layout(cairo, layout);

  g_object_unref(layout);
}

/* [COMMENT] Function purpose: Convert a physical distance in millimetres into
logical pixels on this output.

Placement offsets expressed in pixels drift with display density -- the same
constant is a very different distance on a 96 DPI monitor and a HiDPI laptop
panel. EDID reports the panel's physical size, so a real millimetre is
computable: divide the mode's pixel height by the panel height in millimetres
for pixels-per-millimetre, then by the output scale, because scene nodes are
positioned in logical coordinates rather than physical ones.

Falls back to the Wayland convention that logical space is nominally 96 DPI
when EDID reports no physical size, which virtual machines and some displays
do. */
static int
mm_to_logical_pixels(struct hikari_output *output, double mm)
{
  struct wlr_output *wlr_output = output->wlr_output;

  double scale = wlr_output->scale > 0 ? wlr_output->scale : 1.0;

  if (wlr_output->phys_height > 0 && wlr_output->height > 0) {
    double px_per_mm = (double)wlr_output->height / (double)wlr_output->phys_height;

    return (int)(mm * px_per_mm / scale);
  }

  return (int)(mm * 96.0 / 25.4);
}

/* [COMMENT] Function purpose: Render the clock for one output and attach it to
that output's lock layer, replacing whatever was there. */
static void
refresh_output(struct hikari_output *output, const struct tm *now)
{
  struct hikari_lock_config *lock_config = &hikari_configuration->lock;

  char time_text[128];
  char date_text[256];

  if (strftime(time_text, sizeof(time_text), lock_config->clock_format, now) ==
      0) {
    // [COMMENT] Action purpose: strftime returns 0 both for an empty result and
    // for overflow, and cannot distinguish them. Either way there is nothing
    // worth drawing, so leave the previous buffer alone rather than replacing
    // it with an empty one.
    return;
  }

  bool has_date = lock_config->date_format != NULL &&
      lock_config->date_format[0] != '\0' &&
      strftime(date_text, sizeof(date_text), lock_config->date_format, now) > 0;

  int time_width, time_height;
  measure(&lock_config->clock_font, time_text, &time_width, &time_height);

  int date_width = 0, date_height = 0;
  if (has_date) {
    measure(&lock_config->date_font, date_text, &date_width, &date_height);
  }

  // [COMMENT] Action purpose: Gap between the time and the date, and a small
  // margin so the shadow is not clipped by the surface edge.
  const int gap = has_date ? 12 : 0;
  const int margin = 8;

  int width = (time_width > date_width ? time_width : date_width) + margin * 2;
  int height = time_height + gap + date_height + margin * 2;

  if (width <= 0 || height <= 0) {
    return;
  }

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return;
  }

  cairo_t *cairo = cairo_create(surface);

  draw_centred(cairo,
      &lock_config->clock_font,
      time_text,
      width,
      margin,
      lock_config->clock_color);

  if (has_date) {
    draw_centred(cairo,
        &lock_config->date_font,
        date_text,
        width,
        margin + time_height + gap,
        lock_config->clock_color);
  }

  cairo_surface_flush(surface);

  unsigned char *data = cairo_image_surface_get_data(surface);
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);

  struct wlr_buffer *buffer =
      hikari_buffer_create_argb8888(width, height, data, stride);

  cairo_destroy(cairo);
  cairo_surface_destroy(surface);

  if (buffer == NULL) {
    return;
  }

  if (output->lock_clock_node == NULL) {
    output->lock_clock_node =
        wlr_scene_buffer_create(hikari_server.layers.lock, buffer);
  } else {
    wlr_scene_buffer_set_buffer(output->lock_clock_node, buffer);
  }

  wlr_buffer_drop(buffer);

  if (output->lock_clock_node == NULL) {
    return;
  }

  /* [COMMENT] Action purpose: Centre horizontally, and sit above the vertical
  centre so the password indicator has room beneath it without the two
  colliding. Coordinates are layout-absolute because the lock layer is parented
  to the scene root, not to a per-output tree.

  The 40px clears the indicator; the additional centimetre is a deliberate
  raise, and is expressed in millimetres rather than pixels so it stays a
  centimetre on a HiDPI panel instead of shrinking with the pixel density. */
  int x = output->geometry.x + (output->geometry.width - width) / 2;
  int y = output->geometry.y + output->geometry.height / 2 - height - 40 -
      mm_to_logical_pixels(output, 10);

  if (y < output->geometry.y) {
    y = output->geometry.y;
  }

  wlr_scene_node_set_position(&output->lock_clock_node->node, x, y);
  wlr_scene_node_set_enabled(&output->lock_clock_node->node, true);
}

/* [COMMENT] Function purpose: Timer callback. Redraws and re-arms itself for
the start of the next minute. */
static int
tick_handler(void *data)
{
  struct hikari_lock_clock *clock = data;

  hikari_lock_clock_refresh(clock);

  return 0;
}

void
hikari_lock_clock_init(struct hikari_lock_clock *clock)
{
  clock->tick = wl_event_loop_add_timer(
      hikari_server.event_loop, tick_handler, clock);

  if (clock->tick == NULL) {
    // [COMMENT] Action purpose: Losing the timer costs a ticking clock, not a
    // clock: hikari_lock_clock_refresh() still draws once, and the lock screen
    // shows the time as of the moment it was locked. Degrading is far better
    // than refusing to lock.
    wlr_log(WLR_ERROR,
        "lock_clock: could not create the tick timer; the clock will not "
        "update while the screen stays locked");
  }
}

void
hikari_lock_clock_fini(struct hikari_lock_clock *clock)
{
  if (clock->tick != NULL) {
    wl_event_source_remove(clock->tick);
    clock->tick = NULL;
  }

  // [COMMENT] Action purpose: Destroy the per-output nodes here rather than
  // relying on the lock layer being disabled -- the layer is reused for the
  // next lock, and a stale clock buffer left parented to it would reappear
  // showing the time the previous session was locked at.
  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    if (output->lock_clock_node != NULL) {
      wlr_scene_node_destroy(&output->lock_clock_node->node);
      output->lock_clock_node = NULL;
    }
  }
}

void
hikari_lock_clock_refresh(struct hikari_lock_clock *clock)
{
  if (!hikari_configuration->lock.clock) {
    return;
  }

  time_t raw = time(NULL);
  struct tm now;

  if (localtime_r(&raw, &now) == NULL) {
    return;
  }

  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    refresh_output(output, &now);
  }

  /* [COMMENT] Action purpose: Re-arm for the start of the next minute rather
  than a fixed 60 s from now. A fixed interval drifts against the wall clock, so
  the displayed minute would change up to a minute late; aligning to the
  boundary keeps the change simultaneous with the actual minute rollover. The
  extra second guards against firing a hair early and redrawing the same minute
  twice. */
  if (clock->tick != NULL) {
    int delay_ms = (60 - now.tm_sec) * 1000 + 1000;
    wl_event_source_timer_update(clock->tick, delay_ms);
  }
}
