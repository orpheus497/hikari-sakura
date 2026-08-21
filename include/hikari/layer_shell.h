#if !defined(HIKARI_LAYER_SHELL_H)
#define HIKARI_LAYER_SHELL_H

#include <wlr/types/wlr_layer_shell_v1.h>

#include <hikari/node.h>

struct hikari_output;
struct hikari_layer;
struct hikari_layer_popup;
struct wlr_scene_layer_surface_v1;

enum hikari_layer_node_type {
  HIKARI_LAYER_NODE_TYPE_LAYER,
  HIKARI_LAYER_NODE_TYPE_POPUP
};

struct hikari_layer_node {
  enum hikari_layer_node_type type;

  union {
    struct hikari_layer *layer;
    struct hikari_layer_popup *popup;
  } node;
};

struct hikari_layer {
  struct hikari_node node;

  struct wl_list layer_surfaces;

  struct wlr_layer_surface_v1 *surface;

  /* [COMMENT] Scene-graph attachment for this layer surface. Created in
  hikari_layer_init() via wlr_scene_layer_surface_v1_create(); without it the
  surface is configured but never rendered. */
  struct wlr_scene_layer_surface_v1 *scene_layer_surface;

  struct wl_listener commit;
  struct wl_listener destroy;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener new_popup;

  struct wlr_box geometry;

  struct hikari_output *output;
  enum zwlr_layer_shell_v1_layer layer;
  bool mapped;
  bool configured;

  uint32_t desired_width, desired_height;
  uint32_t anchor;
  struct {
    int32_t top, right, bottom, left;
  } margin;
};

struct hikari_layer_popup {
  struct hikari_layer_node parent;

  struct wlr_xdg_popup *popup;

  /* [COMMENT] Scene tree for this popup, created by
  wlr_scene_xdg_surface_create() and parented to the owning layer surface's
  tree (or to the parent popup's tree when nested). wlroots owns its lifetime,
  so hikari must never destroy it. Without this the popup has no scene node at
  all and never renders -- wlr_scene_layer_surface_v1_create() covers only the
  layer surface itself and its subsurfaces, not its popups. */
  struct wlr_scene_tree *scene_tree;

  struct wl_listener commit;
  struct wl_listener destroy;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener new_popup;

  struct wlr_box geometry;
};

void
hikari_layer_init(struct hikari_layer *layer_surface,
    struct wlr_layer_surface_v1 *wlr_layer_surface);

void
hikari_layer_fini(struct hikari_layer *layer_surface);

#endif
