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

  /* Every hikari_layer_popup parented to this layer, directly or (via a
  nested popup-of-a-popup chain) indirectly -- see get_layer(). Torn down
  in hikari_layer_fini() before the layer itself is freed, so a popup
  still open when its layer is destroyed never outlives it. */
  struct wl_list popups;

  struct wlr_box geometry;

  struct hikari_output *output;
  enum zwlr_layer_shell_v1_layer layer;
  bool mapped;
  bool configured;

  /* [COMMENT] Class purpose: Cached copy of every field of
  wlr_layer_surface_v1::current that arrange_layers() feeds to
  wlr_scene_layer_surface_v1_configure(). Their only job is to answer "did this
  commit change anything the arrangement depends on?", because wlroots 0.20
  sends a configure unconditionally -- it never compares the computed box
  against current.actual_width/actual_height -- so re-arranging on an unchanged
  commit hands the client a fresh configure, which it answers with another
  commit, at whatever rate it can render. The set must stay exhaustive: a field
  read by the arrangement but missing here leaves a surface stuck at a stale
  size. See layer_inputs_changed() in src/layer_shell.c. */
  uint32_t desired_width, desired_height;
  uint32_t anchor;
  struct {
    int32_t top, right, bottom, left;
  } margin;
  int32_t exclusive_zone;
  uint32_t exclusive_edge;
};

struct hikari_layer_popup {
  struct hikari_layer_node parent;

  /* Membership in the owning hikari_layer's `popups` list (see there). */
  struct wl_list link;

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

void
hikari_layer_shell_arrange(struct hikari_output *output);

#endif
