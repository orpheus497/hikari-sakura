#if !defined(HIKARI_INDICATOR_BAR_H)
#define HIKARI_INDICATOR_BAR_H

#include <stdbool.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>

struct hikari_indicator;
struct hikari_output;

/* Single text bar in the view indicator overlay, rendered as a
wlr_scene_buffer. */
struct hikari_indicator_bar {
  struct wlr_scene_buffer *scene_buffer;
  struct hikari_indicator *indicator;

  int width;
  int offset;

  float color[4];

  /* [COMMENT] Action purpose: Identity of the last rendered content, so
  hikari_indicator_bar_update() can skip the destroy+cairo-render+recreate
  cycle when called again with unchanged text/color -- mirrors the cache-key
  short-circuit already proven in hikari_bar_refresh() (src/bar.c). */
  char *cache_text;
  float cache_color[4];

  /* [COMMENT] Action purpose: Intended visibility, held independently of the
  scene node. hikari_indicator_bar_update() destroys and recreates the node on
  every content change and wlr_scene_buffer_create() returns it enabled, so a
  title change while the Logo key is up would flash a hidden bar back on unless
  the intent is recorded here and re-applied after each recreate. See
  DECISIONS_LOG Phase 59. */
  bool visible;
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

/* Function purpose: Reposition the indicator bar scene buffer relative to view geometry. */
void
hikari_indicator_bar_position(struct hikari_indicator_bar *indicator_bar,
    struct hikari_output *output,
    struct wlr_box *view_geometry);

/* Function purpose: Record that this bar should be visible and enable its
scene node if one currently exists. */
void
hikari_indicator_bar_show(struct hikari_indicator_bar *indicator_bar);

/* Function purpose: Record that this bar should be hidden and disable its
scene node if one currently exists. */
void
hikari_indicator_bar_hide(struct hikari_indicator_bar *indicator_bar);

#endif
