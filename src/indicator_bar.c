#include <hikari/indicator_bar.h>

#include <hikari/buffer.h>

#include <cairo/cairo.h>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>
#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/wlr_renderer.h>

#include <hikari/color.h>
#include <hikari/configuration.h>
#include <hikari/font.h>
#include <hikari/indicator.h>
#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/view.h>

// Function purpose: Initialise an indicator bar's rendering state (no scene
// node yet -- that is created lazily by the first hikari_indicator_bar_update
// call) and set its background color.
void
hikari_indicator_bar_init(struct hikari_indicator_bar *indicator_bar,
    struct hikari_indicator *indicator,
    int offset,
    float color[static 4])
{
  indicator_bar->scene_buffer = NULL;
  indicator_bar->indicator = indicator;
  indicator_bar->offset = offset;
  indicator_bar->cache_text = NULL;

  /* [COMMENT] Action purpose: Indicators are only shown while the Logo key is
  held, so a bar starts hidden. Without this the very first content update
  would leave it enabled permanently. */
  indicator_bar->visible = false;

  hikari_indicator_bar_set_color(indicator_bar, color);
}

// Function purpose: Record that this bar should be visible and apply that to
// the scene node when one exists. The flag is authoritative and outlives the
// node, which hikari_indicator_bar_update() destroys and recreates freely.
void
hikari_indicator_bar_show(struct hikari_indicator_bar *indicator_bar)
{
  indicator_bar->visible = true;

  if (indicator_bar->scene_buffer != NULL) {
    wlr_scene_node_set_enabled(&indicator_bar->scene_buffer->node, true);
  }
}

// Function purpose: Inverse of hikari_indicator_bar_show.
void
hikari_indicator_bar_hide(struct hikari_indicator_bar *indicator_bar)
{
  indicator_bar->visible = false;

  if (indicator_bar->scene_buffer != NULL) {
    wlr_scene_node_set_enabled(&indicator_bar->scene_buffer->node, false);
  }
}

void
hikari_indicator_bar_set_color(
    struct hikari_indicator_bar *indicator_bar, float color[static 4])
{
  indicator_bar->color[0] = color[0];
  indicator_bar->color[1] = color[1];
  indicator_bar->color[2] = color[2];
  indicator_bar->color[3] = color[3];
}

// Function purpose: Destroy the indicator bar's scene node (if any) and
// release its cached render state.
void
hikari_indicator_bar_fini(struct hikari_indicator_bar *indicator_bar)
{
  if (indicator_bar->scene_buffer != NULL) {
    wlr_scene_node_destroy(&indicator_bar->scene_buffer->node);
    indicator_bar->scene_buffer = NULL;
  }

  hikari_free(indicator_bar->cache_text);
  indicator_bar->cache_text = NULL;
}

// [COMMENT] Function purpose: Reposition the indicator bar scene buffer
// relative to view geometry.
void
hikari_indicator_bar_position(struct hikari_indicator_bar *indicator_bar,
    struct hikari_output *output,
    struct wlr_box *view_geometry)
{
  if (indicator_bar->scene_buffer == NULL) {
    return;
  }

  // [COMMENT] Action purpose: Add the output's layout origin to the view-local
  // geometry. View geometry is stored output-local, while scene node positions
  // are layout-global (the same conversion output_layout_change_handler applies
  // when repositioning view scene nodes). Without it, indicator bars render at
  // the wrong global position on any output not placed at layout origin (0,0).
  wlr_scene_node_set_position(&indicator_bar->scene_buffer->node,
      output->geometry.x + view_geometry->x + 5,
      output->geometry.y + view_geometry->y + indicator_bar->offset);
}

// Function purpose: Replace the existing scene buffer (if any) and
// create a new one with rendered text content.
void
hikari_indicator_bar_update(struct hikari_indicator_bar *indicator_bar,
    struct hikari_output *output,
    const char *text)
{
  // [COMMENT] Action purpose: Skip the destroy+cairo-render+recreate cycle
  // when called again with text and color identical to the last render.
  // hikari_indicator_update() fires on every focus change and on every
  // keystroke while assigning a mark/group/sheet -- without this check every
  // such call reallocated a cairo surface, re-shaped the text through Pango,
  // copied it into a second buffer, and destroyed/recreated the scene node,
  // even when nothing visible changed. Mirrors the cache-key short-circuit
  // already proven in hikari_bar_refresh() (src/bar.c).
  if (indicator_bar->scene_buffer != NULL && indicator_bar->cache_text != NULL &&
      text != NULL && strcmp(indicator_bar->cache_text, text) == 0 &&
      memcmp(indicator_bar->cache_color,
          indicator_bar->color,
          sizeof(indicator_bar->color)) == 0) {
    return;
  }

  // [COMMENT] Action purpose: Clean up existing scene buffer before recreation.
  if (indicator_bar->scene_buffer != NULL) {
    hikari_indicator_bar_fini(indicator_bar);
  }

  // [COMMENT] Action purpose: Skip creation for empty indicator text.
  if (text == NULL || !strcmp(text, "")) {
    return;
  }

  size_t len = strlen(text);

  struct hikari_font *font = &hikari_configuration->font;
  int width = hikari_configuration->font.character_width * len + 8;
  int height = hikari_configuration->font.height;

  indicator_bar->width = width;

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

  cairo_t *cairo = cairo_create(surface);
  PangoLayout *layout = pango_cairo_create_layout(cairo);

  float *background = indicator_bar->color;

  cairo_set_source_rgba(
      cairo, background[0], background[1], background[2], background[3]);
  cairo_paint(cairo);

  float *border_inactive = hikari_configuration->border_inactive;
  cairo_set_source_rgba(cairo,
      border_inactive[0],
      border_inactive[1],
      border_inactive[2],
      border_inactive[3]);
  cairo_rectangle(cairo, 0, 0, width, height);
  cairo_set_line_width(cairo, 1);
  cairo_stroke(cairo);

  cairo_set_source_rgba(cairo, 0, 0, 0, 1);
  pango_layout_set_font_description(layout, font->desc);
  cairo_move_to(cairo, 4, 4);
  pango_layout_set_text(layout, text, -1);

  pango_cairo_update_layout(cairo, layout);
  pango_cairo_show_layout(cairo, layout);

  cairo_surface_flush(surface);

  unsigned char *data = cairo_image_surface_get_data(surface);
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);

  struct wlr_buffer *buffer =
      hikari_buffer_create_argb8888(width, height, data, stride);

  if (buffer != NULL) {
    indicator_bar->scene_buffer =
        wlr_scene_buffer_create(hikari_server.layers.overlay, buffer);
    wlr_buffer_drop(buffer);

    /* [COMMENT] Action purpose: Re-apply the recorded visibility to the newly
    created node. wlr_scene_buffer_create() returns an ENABLED node, so without
    this every content change (a window retitling itself, a mark/group/sheet
    keystroke) would make a hidden indicator reappear until something else
    hid it again. See DECISIONS_LOG Phase 59. */
    if (indicator_bar->scene_buffer != NULL) {
      wlr_scene_node_set_enabled(
          &indicator_bar->scene_buffer->node, indicator_bar->visible);
    }

    // [COMMENT] Action purpose: Record what was actually rendered so the next
    // call can short-circuit if nothing changed. Only done on success -- a
    // failed render must not poison the cache into skipping a retry.
    indicator_bar->cache_text = hikari_malloc(strlen(text) + 1);
    strcpy(indicator_bar->cache_text, text);
    memcpy(indicator_bar->cache_color,
        indicator_bar->color,
        sizeof(indicator_bar->color));
  }

  cairo_surface_destroy(surface);
  g_object_unref(layout);
  cairo_destroy(cairo);
}
