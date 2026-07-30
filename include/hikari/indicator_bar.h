#if !defined(HIKARI_INDICATOR_BAR_H)
#define HIKARI_INDICATOR_BAR_H

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>

struct hikari_indicator;
struct hikari_output;

/* ##Class purpose: Single text bar in the view indicator overlay, rendered as a wlr_scene_buffer. */
struct hikari_indicator_bar {
  struct wlr_scene_buffer *scene_buffer;
  struct hikari_indicator *indicator;

  int width;
  int offset;

  float color[4];
};

void
hikari_indicator_bar_init(struct hikari_indicator_bar *indicator_bar,
    struct hikari_indicator *indicator,
    int offset,
    float color[static 4]);

void
hikari_indicator_bar_fini(struct hikari_indicator_bar *indicator_bar);

void
hikari_indicator_bar_set_color(
    struct hikari_indicator_bar *indicator_bar, float color[static 4]);

void
hikari_indicator_bar_update(struct hikari_indicator_bar *indicator_bar,
    struct hikari_output *output,
    const char *text);

/* ##Function purpose: Reposition the indicator bar scene buffer relative to view geometry. */
void
hikari_indicator_bar_position(struct hikari_indicator_bar *indicator_bar,
    struct hikari_output *output,
    struct wlr_box *view_geometry);

#endif
