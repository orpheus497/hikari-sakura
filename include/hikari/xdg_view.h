#if !defined(HIKARI_XDG_VIEW_H)
#define HIKARI_XDG_VIEW_H

#include <wayland-server-core.h>

#include <wlr/types/wlr_xdg_shell.h>

#include <hikari/view.h>


struct hikari_xdg_view {
  struct hikari_view view;

  struct wlr_xdg_surface *surface;
  struct wlr_xdg_toplevel *xdg_toplevel;
  /* scene_tree is the hikari-owned parent tree for
  this view -- hikari controls its lifetime and positions it at the view's
  layout-absolute origin. surface_tree is the wlroots-owned tree returned by
  wlr_scene_xdg_surface_create, parented under scene_tree; wlroots destroys it
  itself from its own xdg_surface destroy listener, so it must NOT hold any
  hikari-owned nodes. */
  struct wlr_scene_tree *scene_tree;
  struct wlr_scene_tree *surface_tree;

  /* [COMMENT] Class purpose: A fullscreen or maximize request that arrived
  while a resize was still in flight.

  There is one pending_operation slot per view, so a state request that lands
  mid-resize cannot be actioned immediately -- but the client has already been
  sent its configure and believes the state changed, so DROPPING the request
  desynchronises the two permanently. Parking the desired state here and
  draining it from commit_handler(), once the in-flight operation has been
  acked, turns a permanent desync into a one-frame delay. */
  bool pending_fullscreen;
  bool pending_fullscreen_value;
  bool pending_maximized;
  bool pending_maximized_value;

  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener commit;
  struct wl_listener new_popup;
  struct wl_listener set_title;
  struct wl_listener request_fullscreen;
  /* [COMMENT] xdg-shell requires a configure in reply to set_maximized even
  when the state does not change -- wlr_xdg_shell.h states that omitting it is a
  protocol violation. hikari had no listener at all here, which is why a client's
  own titlebar maximize button did nothing. Toplevel-scoped, so released in
  toplevel_destroy like request_fullscreen. */
  struct wl_listener request_maximize;
  /* [COMMENT] Listener on the xdg_toplevel's own destroy signal. wlroots
  destroys the toplevel role object BEFORE emitting the xdg_surface destroy
  signal, and destroy_xdg_toplevel() asserts that every toplevel-scoped signal
  has no listeners left. Anything registered on xdg_toplevel->events.* must
  therefore be released here, not in the xdg_surface destroy handler, which runs
  too late. See DECISIONS_LOG Phase 57. */
  struct wl_listener toplevel_destroy;
};

void
hikari_xdg_view_init(struct hikari_xdg_view *xdg_view,
    struct wlr_xdg_surface *xdg_surface,
    struct hikari_workspace *workspace);

struct hikari_xdg_popup {
  struct hikari_view_child view_child;

  struct wlr_xdg_popup *popup;

  /* [COMMENT] Scene tree for this popup, created by
  wlr_scene_xdg_surface_create() and parented to the parent surface's tree.
  wlroots owns its lifetime (it destroys the tree from its own xdg_surface
  destroy listener), so hikari must never destroy it. Without this the popup
  has no scene node at all and never renders. */
  struct wlr_scene_tree *scene_tree;

  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener commit;
  struct wl_listener new_popup;
};

#endif
