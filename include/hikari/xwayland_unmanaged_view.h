#if !defined(HIKARI_XWAYLAND_UNMANAGED_VIEW_H)
#define HIKARI_XWAYLAND_UNMANAGED_VIEW_H

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hikari/node.h>

struct hikari_workspace;

struct hikari_xwayland_unmanaged_view {
  struct hikari_node node;

  struct wlr_xwayland_surface *surface;

  struct hikari_workspace *workspace;
  bool hidden;

  struct wlr_box geometry;

  /* [COMMENT] Class purpose: The wlroots-managed tree displaying this
  override-redirect surface.

  This file previously contained no reference to wlr_scene at all, so X11
  menus, tooltips, dropdowns and drag icons were hit-tested but never drawn.
  Unlike a managed view there is no hikari-owned parent tree to hang it from --
  an override-redirect surface has no border and no indicator frame -- so this
  attaches straight to the view layer and is positioned in layout-absolute
  coordinates, which is what wlr_xwayland_surface.x/y already are.

  surface_tree_destroy shares ownership the same way the managed view does:
  wlroots tears these trees down with the surface, and dissociate destroys it
  here, so the listener nulls the pointer whichever side goes first. */
  struct wlr_scene_tree *surface_tree;
  struct wl_listener surface_tree_destroy;

  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener request_configure;
  struct wl_listener commit;
  struct wl_listener associate;
  struct wl_listener dissociate;
  struct wl_listener set_override_redirect;

  struct wl_list unmanaged_output_views;
};

void
hikari_xwayland_unmanaged_view_init(
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view,
    struct wlr_xwayland_surface *xwayland_surface,
    struct hikari_workspace *workspace);

void
hikari_xwayland_unmanaged_evacuate(
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view,
    struct hikari_workspace *workspace);

/* Function purpose: Sever every reference this view holds to its output and
workspace, for the teardown case where there is no surviving workspace to
evacuate it to (compositor shutdown, noop output). Leaves the view alive and
its own destroy path intact, but marks it as having outlived its output --
map/unmap/commit all bail on a NULL workspace afterwards. Idempotent. */
void
hikari_xwayland_unmanaged_detach(
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view);

#endif
