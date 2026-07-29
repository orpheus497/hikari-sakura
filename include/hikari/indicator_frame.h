/* ##Script function and purpose: Indicator frame overlay using wlr_scene_rect nodes for modal view highlighting. */

#if !defined(HIKARI_INDICATOR_FRAME_H)
#define HIKARI_INDICATOR_FRAME_H

#include <wlr/types/wlr_scene.h>

struct hikari_view;

/* ##Class purpose: Scene-graph backed colored rectangle frame overlay shown around views during modal operations (group assign, sheet assign, etc.). */
struct hikari_indicator_frame {
  struct wlr_scene_rect *top;
  struct wlr_scene_rect *bottom;
  struct wlr_scene_rect *left;
  struct wlr_scene_rect *right;
};

void
hikari_indicator_frame_init(
    struct hikari_indicator_frame *indicator_frame,
    struct wlr_scene_tree *parent);

void
hikari_indicator_frame_fini(struct hikari_indicator_frame *indicator_frame);

void
hikari_indicator_frame_refresh_geometry(
    struct hikari_indicator_frame *indicator_frame, struct hikari_view *view);

void
hikari_indicator_frame_show(
    struct hikari_indicator_frame *indicator_frame, float color[static 4]);

void
hikari_indicator_frame_hide(struct hikari_indicator_frame *indicator_frame);

#endif
