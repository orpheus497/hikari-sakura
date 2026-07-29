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
  if (border->state == HIKARI_BORDER_NONE) {
    border->geometry = *geometry;
    return;
  }

  int border_width = hikari_configuration->border;

  border->geometry.x = geometry->x - border_width;
  border->geometry.y = geometry->y - border_width;
  border->geometry.width = geometry->width + border_width * 2;
  border->geometry.height = geometry->height + border_width * 2;

  border->top.x = border->geometry.x;
  border->top.y = border->geometry.y;
  border->top.width = border->geometry.width;
  border->top.height = border_width;

  border->bottom.x = border->geometry.x;
  border->bottom.y =
      border->geometry.y + border->geometry.height - border_width;
  border->bottom.width = border->geometry.width;
  border->bottom.height = border_width;

  border->left.x = border->geometry.x;
  border->left.y = border->geometry.y;
  border->left.width = border_width;
  border->left.height = border->geometry.height;

  border->right.x = border->geometry.x + border->geometry.width - border_width;
  border->right.y = border->geometry.y;
  border->right.width = border_width;
  border->right.height = border->geometry.height;
}
