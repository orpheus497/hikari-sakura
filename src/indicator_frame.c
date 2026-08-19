// [COMMENT] Script function and purpose: Implements the indicator frame overlay using wlr_scene_rect nodes,
// providing a colored frame around views during modal interactive operations.

#include <hikari/indicator_frame.h>

#include <assert.h>

#include <wlr/types/wlr_output.h>

#include <hikari/border.h>
#include <hikari/color.h>
#include <hikari/configuration.h>
#include <hikari/output.h>
#include <hikari/view.h>

// [COMMENT] Function purpose: Create 4 scene_rect nodes (disabled, transparent) as children of the given parent tree.
void
hikari_indicator_frame_init(
    struct hikari_indicator_frame *indicator_frame,
    struct wlr_scene_tree *parent)
{
  float color[4] = {0, 0, 0, 0};
  indicator_frame->top = wlr_scene_rect_create(parent, 0, 0, color);
  indicator_frame->bottom = wlr_scene_rect_create(parent, 0, 0, color);
  indicator_frame->left = wlr_scene_rect_create(parent, 0, 0, color);
  indicator_frame->right = wlr_scene_rect_create(parent, 0, 0, color);

  // [COMMENT] Action purpose: Place frame rects above surface content but below other overlays.
  wlr_scene_node_raise_to_top(&indicator_frame->top->node);
  wlr_scene_node_raise_to_top(&indicator_frame->bottom->node);
  wlr_scene_node_raise_to_top(&indicator_frame->left->node);
  wlr_scene_node_raise_to_top(&indicator_frame->right->node);

  wlr_scene_node_set_enabled(&indicator_frame->top->node, false);
  wlr_scene_node_set_enabled(&indicator_frame->bottom->node, false);
  wlr_scene_node_set_enabled(&indicator_frame->left->node, false);
  wlr_scene_node_set_enabled(&indicator_frame->right->node, false);
}

// [COMMENT] Function purpose: Destroy the 4 scene_rect nodes.
void
hikari_indicator_frame_fini(struct hikari_indicator_frame *indicator_frame)
{
  if (indicator_frame->top != NULL) {
    wlr_scene_node_destroy(&indicator_frame->top->node);
    wlr_scene_node_destroy(&indicator_frame->bottom->node);
    wlr_scene_node_destroy(&indicator_frame->left->node);
    wlr_scene_node_destroy(&indicator_frame->right->node);

    indicator_frame->top = NULL;
    indicator_frame->bottom = NULL;
    indicator_frame->left = NULL;
    indicator_frame->right = NULL;
  }
}

// [COMMENT] Function purpose: Recompute the frame geometry to match the current view bounds. Does not change visibility.
void
hikari_indicator_frame_refresh_geometry(
    struct hikari_indicator_frame *indicator_frame, struct hikari_view *view)
{
  if (indicator_frame->top == NULL) {
    return;
  }

  struct wlr_box *top_bottom_geometry;
  struct wlr_box *left_right_geometry;

  if (view->border.state == HIKARI_BORDER_NONE) {
    top_bottom_geometry = hikari_view_geometry(view);
    left_right_geometry = top_bottom_geometry;
  } else if (view->maximized_state == NULL) {
    top_bottom_geometry = hikari_view_border_geometry(view);
    left_right_geometry = top_bottom_geometry;
  } else {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        top_bottom_geometry = hikari_view_geometry(view);
        left_right_geometry = hikari_view_border_geometry(view);
        break;

      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        top_bottom_geometry = hikari_view_border_geometry(view);
        left_right_geometry = hikari_view_geometry(view);
        break;

      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        top_bottom_geometry = hikari_view_geometry(view);
        left_right_geometry = top_bottom_geometry;
        break;
    }
  }

  int border = hikari_configuration->border;

  /* [COMMENT] Action purpose: Convert to PARENT-RELATIVE coordinates. These
  rects are children of the view's scene tree, which is already positioned at
  the view's layout-absolute origin, and wlr_scene_node_set_position is
  relative to the parent. The geometries selected above are absolute, so the
  view origin is subtracted out; using them directly would offset the frame by
  the view position twice. */
  struct wlr_box *origin = hikari_view_geometry(view);

  // [COMMENT] Action purpose: Position and size each rect to form a frame around the view.
  wlr_scene_node_set_position(&indicator_frame->top->node,
      top_bottom_geometry->x - origin->x, top_bottom_geometry->y - origin->y);
  wlr_scene_rect_set_size(indicator_frame->top,
      top_bottom_geometry->width, border);

  wlr_scene_node_set_position(&indicator_frame->bottom->node,
      top_bottom_geometry->x - origin->x,
      top_bottom_geometry->y + top_bottom_geometry->height - border
          - origin->y);
  wlr_scene_rect_set_size(indicator_frame->bottom,
      top_bottom_geometry->width, border);

  wlr_scene_node_set_position(&indicator_frame->left->node,
      left_right_geometry->x - origin->x, left_right_geometry->y - origin->y);
  wlr_scene_rect_set_size(indicator_frame->left,
      border, left_right_geometry->height);

  wlr_scene_node_set_position(&indicator_frame->right->node,
      left_right_geometry->x + left_right_geometry->width - border - origin->x,
      left_right_geometry->y - origin->y);
  wlr_scene_rect_set_size(indicator_frame->right,
      border, left_right_geometry->height);
}

// [COMMENT] Function purpose: Enable the indicator frame with the given color.
void
hikari_indicator_frame_show(
    struct hikari_indicator_frame *indicator_frame, float color[static 4])
{
  if (indicator_frame->top == NULL) {
    return;
  }

  wlr_scene_rect_set_color(indicator_frame->top, color);
  wlr_scene_rect_set_color(indicator_frame->bottom, color);
  wlr_scene_rect_set_color(indicator_frame->left, color);
  wlr_scene_rect_set_color(indicator_frame->right, color);

  wlr_scene_node_set_enabled(&indicator_frame->top->node, true);
  wlr_scene_node_set_enabled(&indicator_frame->bottom->node, true);
  wlr_scene_node_set_enabled(&indicator_frame->left->node, true);
  wlr_scene_node_set_enabled(&indicator_frame->right->node, true);
}

// [COMMENT] Function purpose: Disable (hide) the indicator frame.
void
hikari_indicator_frame_hide(struct hikari_indicator_frame *indicator_frame)
{
  if (indicator_frame->top == NULL) {
    return;
  }

  wlr_scene_node_set_enabled(&indicator_frame->top->node, false);
  wlr_scene_node_set_enabled(&indicator_frame->bottom->node, false);
  wlr_scene_node_set_enabled(&indicator_frame->left->node, false);
  wlr_scene_node_set_enabled(&indicator_frame->right->node, false);
}
