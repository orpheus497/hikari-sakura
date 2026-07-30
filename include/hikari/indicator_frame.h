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

/* ##Function purpose: Create four disabled wlr_scene_rect nodes as children of the parent tree. */
void
hikari_indicator_frame_init(
    struct hikari_indicator_frame *indicator_frame,
    struct wlr_scene_tree *parent);

/* ##Function purpose: Destroy the four scene_rect nodes and NULL the pointers. */
void
hikari_indicator_frame_fini(struct hikari_indicator_frame *indicator_frame);

/* ##Function purpose: Reposition and resize the frame rectangles to match current view bounds. */
void
hikari_indicator_frame_refresh_geometry(
    struct hikari_indicator_frame *indicator_frame, struct hikari_view *view);

/* ##Function purpose: Enable the four frame rectangles with the given RGBA color array. */
void
hikari_indicator_frame_show(
    struct hikari_indicator_frame *indicator_frame, float color[static 4]);

/* ##Function purpose: Disable the four frame rectangles without destroying them. */
void
hikari_indicator_frame_hide(struct hikari_indicator_frame *indicator_frame);

#endif
