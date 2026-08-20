#include <hikari/border.h>

#include <assert.h>



#include <wlr/types/wlr_output.h>

#include <hikari/color.h>
#include <hikari/configuration.h>
#include <hikari/node.h>
#include <hikari/output.h>


// [COMMENT] Function purpose: Create the four scene_rect nodes (disabled,
// transparent) that make up a view's border, as children of the view's scene
// tree. Called once per view; hikari_border_refresh_geometry positions and
// enables them afterward.
void
hikari_border_init(struct hikari_border *border, struct wlr_scene_tree *parent)
{
  float color[4] = {0, 0, 0, 0}; // Transparent initially
  border->top = wlr_scene_rect_create(parent, 0, 0, color);
  border->bottom = wlr_scene_rect_create(parent, 0, 0, color);
  border->left = wlr_scene_rect_create(parent, 0, 0, color);
  border->right = wlr_scene_rect_create(parent, 0, 0, color);

  // [COMMENT] Action purpose: wlr_scene_rect_create can return NULL under
  // memory/resource pressure. Every consumer (refresh_geometry, fini) already
  // treats border->top == NULL as "no border" and returns early, so on partial
  // failure here we destroy whatever succeeded and null out all four fields
  // to preserve that all-or-nothing invariant instead of dereferencing NULL
  // below.
  if (border->top == NULL || border->bottom == NULL ||
      border->left == NULL || border->right == NULL) {
    if (border->top != NULL) {
      wlr_scene_node_destroy(&border->top->node);
    }
    if (border->bottom != NULL) {
      wlr_scene_node_destroy(&border->bottom->node);
    }
    if (border->left != NULL) {
      wlr_scene_node_destroy(&border->left->node);
    }
    if (border->right != NULL) {
      wlr_scene_node_destroy(&border->right->node);
    }
    border->top = NULL;
    border->bottom = NULL;
    border->left = NULL;
    border->right = NULL;
    return;
  }

  wlr_scene_node_lower_to_bottom(&border->top->node);
  wlr_scene_node_lower_to_bottom(&border->bottom->node);
  wlr_scene_node_lower_to_bottom(&border->left->node);
  wlr_scene_node_lower_to_bottom(&border->right->node);

  wlr_scene_node_set_enabled(&border->top->node, false);
  wlr_scene_node_set_enabled(&border->bottom->node, false);
  wlr_scene_node_set_enabled(&border->left->node, false);
  wlr_scene_node_set_enabled(&border->right->node, false);
}

// [COMMENT] Function purpose: Recompute and reposition the four border rects
// against the view's current geometry, and enable/disable them per
// border->state. Called by hikari_view_refresh_geometry whenever a view's
// geometry changes (map, move, resize, maximize, tile).
void
hikari_border_refresh_geometry(
    struct hikari_border *border, struct wlr_box *geometry)
{
  if (border->top == NULL) {
    return;
  }

  if (border->state == HIKARI_BORDER_NONE) {
    border->geometry = *geometry;
    wlr_scene_node_set_enabled(&border->top->node, false);
    wlr_scene_node_set_enabled(&border->bottom->node, false);
    wlr_scene_node_set_enabled(&border->left->node, false);
    wlr_scene_node_set_enabled(&border->right->node, false);
    return;
  }

  int border_width = hikari_configuration->border;

  border->geometry.x = geometry->x - border_width;
  border->geometry.y = geometry->y - border_width;
  border->geometry.width = geometry->width + border_width * 2;
  border->geometry.height = geometry->height + border_width * 2;

  /* [COMMENT] Action purpose: Position the rects PARENT-RELATIVE. These nodes
  are children of the view's scene tree, which hikari_view_refresh_geometry has
  already positioned at the view's layout-absolute origin, and
  wlr_scene_node_set_position is relative to the parent. Using the absolute
  border->geometry here would apply the view origin twice and draw the border
  at roughly double the intended offset. border->geometry itself stays absolute
  because hit-testing and damage tracking consume it in layout coordinates. */
  wlr_scene_node_set_position(&border->top->node, -border_width, -border_width);
  wlr_scene_rect_set_size(border->top, border->geometry.width, border_width);

  wlr_scene_node_set_position(
      &border->bottom->node, -border_width, geometry->height);
  wlr_scene_rect_set_size(border->bottom, border->geometry.width, border_width);

  wlr_scene_node_set_position(
      &border->left->node, -border_width, -border_width);
  wlr_scene_rect_set_size(border->left, border_width, border->geometry.height);

  wlr_scene_node_set_position(
      &border->right->node, geometry->width, -border_width);
  wlr_scene_rect_set_size(border->right, border_width, border->geometry.height);

  wlr_scene_node_set_enabled(&border->top->node, true);
  wlr_scene_node_set_enabled(&border->bottom->node, true);
  wlr_scene_node_set_enabled(&border->left->node, true);
  wlr_scene_node_set_enabled(&border->right->node, true);

  float *color = border->state == HIKARI_BORDER_ACTIVE 
    ? hikari_configuration->border_active 
    : hikari_configuration->border_inactive;
    
  wlr_scene_rect_set_color(border->top, color);
  wlr_scene_rect_set_color(border->bottom, color);
  wlr_scene_rect_set_color(border->left, color);
  wlr_scene_rect_set_color(border->right, color);
}
