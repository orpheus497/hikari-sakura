#include <hikari/border.h>

#include <assert.h>



#include <wlr/types/wlr_output.h>

#include <hikari/color.h>
#include <hikari/configuration.h>
#include <hikari/node.h>
#include <hikari/output.h>


void
hikari_border_init(struct hikari_border *border, struct wlr_scene_tree *parent)
{
  float color[4] = {0, 0, 0, 0}; // Transparent initially
  border->top = wlr_scene_rect_create(parent, 0, 0, color);
  border->bottom = wlr_scene_rect_create(parent, 0, 0, color);
  border->left = wlr_scene_rect_create(parent, 0, 0, color);
  border->right = wlr_scene_rect_create(parent, 0, 0, color);

  wlr_scene_node_lower_to_bottom(&border->top->node);
  wlr_scene_node_lower_to_bottom(&border->bottom->node);
  wlr_scene_node_lower_to_bottom(&border->left->node);
  wlr_scene_node_lower_to_bottom(&border->right->node);

  wlr_scene_node_set_enabled(&border->top->node, false);
  wlr_scene_node_set_enabled(&border->bottom->node, false);
  wlr_scene_node_set_enabled(&border->left->node, false);
  wlr_scene_node_set_enabled(&border->right->node, false);
}

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

  wlr_scene_node_set_position(&border->top->node, border->geometry.x, border->geometry.y);
  wlr_scene_rect_set_size(border->top, border->geometry.width, border_width);

  wlr_scene_node_set_position(&border->bottom->node, border->geometry.x, border->geometry.y + border->geometry.height - border_width);
  wlr_scene_rect_set_size(border->bottom, border->geometry.width, border_width);

  wlr_scene_node_set_position(&border->left->node, border->geometry.x, border->geometry.y);
  wlr_scene_rect_set_size(border->left, border_width, border->geometry.height);

  wlr_scene_node_set_position(&border->right->node, border->geometry.x + border->geometry.width - border_width, border->geometry.y);
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
