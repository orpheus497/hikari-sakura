#if !defined(HIKARI_XWAYLAND_VIEW_H)
#define HIKARI_XWAYLAND_VIEW_H

#include <wayland-server-core.h>

#include <wlr/types/wlr_scene.h>

#include <hikari/view.h>



struct hikari_xwayland_view {
  struct hikari_view view;

  struct wlr_xwayland_surface *surface;
  struct wlr_scene_tree *scene_tree;

  /* [COMMENT] Class purpose: The wlroots-managed tree that actually displays
  the X11 surface and its subsurfaces, parented under scene_tree alongside the
  border and indicator frame.

  Until this existed, hikari built scene_tree and attached only its own border
  and indicator rects to it -- nothing ever attached the surface -- so every
  managed X11 window drew as an empty bordered rectangle. Created when the
  wlr_surface becomes available (`associate`, not init: it is NULL before
  that) and released on `dissociate`.

  surface_tree_destroy exists because ownership is shared: wlroots tears these
  trees down with the surface, and hikari destroys it on dissociate. The
  listener nulls the pointer whichever side goes first, so neither a
  double-destroy nor a stale pointer is possible. */
  struct wlr_scene_tree *surface_tree;
  struct wl_listener surface_tree_destroy;

  /* [COMMENT] wlroots 0.20 xwayland lifecycle: the wlr_surface is not
  available at new_surface time; map/unmap listeners are registered on it
  when `associate` fires and dropped on `dissociate`. */
  struct wl_listener associate;
  struct wl_listener dissociate;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener request_configure;
  struct wl_listener commit;
  struct wl_listener set_title;
  struct wl_listener set_override_redirect;
};

void
hikari_xwayland_view_init(struct hikari_xwayland_view *xwayland_view,
    struct wlr_xwayland_surface *xwayland_surface,
    struct hikari_workspace *workspace);

#endif
