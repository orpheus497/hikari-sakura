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

  /* [COMMENT] Action purpose: wlr_scene_rect_set_color() requires PREMULTIPLIED
  colour, while configuration colours are stored straight (Cairo's convention).
  The two only agree at alpha 1.0, so a border configured as "#RRGGBBAA" would
  otherwise render too bright over whatever is behind it. */
  float premultiplied[4];
  hikari_color_premultiply(premultiplied, color);

  wlr_scene_rect_set_color(border->top, premultiplied);
  wlr_scene_rect_set_color(border->bottom, premultiplied);
  wlr_scene_rect_set_color(border->left, premultiplied);
  wlr_scene_rect_set_color(border->right, premultiplied);
}

/* [COMMENT] Function purpose: Crop one border rect to `clip` and place it.

`nominal` is the rect's unclipped parent-relative box. Both boxes are in the
view's content-local space. An intersection that comes out empty disables the
node rather than setting a zero-sized rect, because a zero width or height is
not a size wlr_scene_rect_set_size() is meant to be handed. */
static void
clip_rect(struct wlr_scene_rect *rect,
    struct wlr_box *nominal,
    struct wlr_box *clip)
{
  if (clip == NULL) {
    wlr_scene_node_set_position(&rect->node, nominal->x, nominal->y);
    wlr_scene_rect_set_size(rect, nominal->width, nominal->height);
    wlr_scene_node_set_enabled(&rect->node, true);
    return;
  }

  struct wlr_box cropped;
  if (!wlr_box_intersection(&cropped, nominal, clip)) {
    wlr_scene_node_set_enabled(&rect->node, false);
    return;
  }

  wlr_scene_node_set_position(&rect->node, cropped.x, cropped.y);
  wlr_scene_rect_set_size(rect, cropped.width, cropped.height);
  wlr_scene_node_set_enabled(&rect->node, true);
}

void
hikari_border_clip(struct hikari_border *border, struct wlr_box *clip)
{
  if (border->top == NULL || border->state == HIKARI_BORDER_NONE) {
    return;
  }

  int border_width = hikari_configuration->border;

  /* [COMMENT] Action purpose: The content box is recovered from the border box
  rather than passed in, so this can be called from anywhere that has already
  run hikari_border_refresh_geometry() without threading the view's geometry
  through as well. The two are the same rectangle inflated by border_width on
  every side, which is exactly what refresh_geometry above computes. */
  int content_width = border->geometry.width - border_width * 2;
  int content_height = border->geometry.height - border_width * 2;

  /* [COMMENT] Action purpose: The same four parent-relative boxes
  hikari_border_refresh_geometry() lays out, restated here as data so the crop
  is a pure intersection. Keep in step with that function. */
  struct wlr_box nominal_top = { .x = -border_width,
    .y = -border_width,
    .width = border->geometry.width,
    .height = border_width };

  struct wlr_box nominal_bottom = { .x = -border_width,
    .y = content_height,
    .width = border->geometry.width,
    .height = border_width };

  struct wlr_box nominal_left = { .x = -border_width,
    .y = -border_width,
    .width = border_width,
    .height = border->geometry.height };

  struct wlr_box nominal_right = { .x = content_width,
    .y = -border_width,
    .width = border_width,
    .height = border->geometry.height };

  clip_rect(border->top, &nominal_top, clip);
  clip_rect(border->bottom, &nominal_bottom, clip);
  clip_rect(border->left, &nominal_left, clip);
  clip_rect(border->right, &nominal_right, clip);
}
