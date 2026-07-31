#include <hikari/indicator_bar.h>

#include <cairo/cairo.h>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>
#include <string.h>

#include <wlr/render/allocator.h>
#include <wlr/interfaces/wlr_buffer.h>

#include <hikari/color.h>
#include <hikari/configuration.h>
#include <hikari/font.h>
#include <hikari/indicator.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/view.h>

void
hikari_indicator_bar_init(struct hikari_indicator_bar *indicator_bar,
    struct hikari_indicator *indicator,
    int offset,
    float color[static 4])
{
  indicator_bar->scene_buffer = NULL;
  indicator_bar->indicator = indicator;
  indicator_bar->offset = offset;

  hikari_indicator_bar_set_color(indicator_bar, color);
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

void
hikari_indicator_bar_fini(struct hikari_indicator_bar *indicator_bar)
{
  if (indicator_bar->scene_buffer != NULL) {
    wlr_scene_node_destroy(&indicator_bar->scene_buffer->node);
    indicator_bar->scene_buffer = NULL;
  }
}

/* ##Function purpose: Reposition the indicator bar scene buffer relative to view geometry. */
void
hikari_indicator_bar_position(struct hikari_indicator_bar *indicator_bar,
    struct hikari_output *output,
    struct wlr_box *view_geometry)
{
  if (indicator_bar->scene_buffer == NULL) {
    return;
  }

  wlr_scene_node_set_position(&indicator_bar->scene_buffer->node, 
      view_geometry->x + 5, 
      view_geometry->y + indicator_bar->offset);
}

/* ##Function purpose: Replace the existing scene buffer (if any) and create a new one with rendered text content. */
void
hikari_indicator_bar_update(struct hikari_indicator_bar *indicator_bar,
    struct hikari_output *output,
    const char *text)
{
  /* ##Condition purpose: Clean up existing scene buffer before recreation. */
  if (indicator_bar->scene_buffer != NULL) {
    hikari_indicator_bar_fini(indicator_bar);
  }

  /* ##Condition purpose: Skip creation for empty indicator text. */
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

  /* ##Action purpose: Declare wlr_drm_format using only the public API contract:
   * zero-initialise the struct, then set .format. The .len=0 and .modifiers=NULL
   * fields indicate no explicit modifier list, allowing the allocator to choose
   * the best available modifier. Accessing .capacity is forbidden — it is a
   * private internal field used by wlroots for dynamic array bookkeeping. */
  struct wlr_drm_format format = {0};
  format.format = DRM_FORMAT_ARGB8888;
  struct wlr_buffer *buffer = wlr_allocator_create_buffer(hikari_server.allocator, width, height, &format);
  
  /* ##Condition purpose: Check if buffer allocation succeeded. */
  if (buffer != NULL) {
    void *mapped_data;
    uint32_t mapped_format;
    size_t mapped_stride;
    /* ##Condition purpose: Guard against failed buffer data mapping. */
    if (wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &mapped_data, &mapped_format, &mapped_stride)) {
      for (int y = 0; y < height; y++) {
        memcpy((char*)mapped_data + y * mapped_stride, data + y * stride, width * 4);
      }
      wlr_buffer_end_data_ptr_access(buffer);

      indicator_bar->scene_buffer = wlr_scene_buffer_create(&hikari_server.scene->tree, buffer);
    }
    wlr_buffer_drop(buffer);
  }

  cairo_surface_destroy(surface);
  g_object_unref(layout);
  cairo_destroy(cairo);
}
