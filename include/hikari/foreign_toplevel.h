/* [COMMENT] Script function and purpose: zwlr_foreign_toplevel_management_v1 --
the protocol an external window switcher, dock or task manager binds to in order
to ACT on a window: focus it, close it, minimise it, maximise it.

hikari already advertises ext-foreign-toplevel-list-v1 (see BLUEPRINT section
16), which carries title and app_id and nothing else -- it is listing only. This
module is the acting half, and it is the only route available: there is no
standards-track toplevel-control protocol in wayland-protocols, and
xdg-activation-v1 cannot substitute because its activate request takes a
wl_surface the requester owns, which a switcher does not have for another
client's window.

One handle per MAPPED view, mirroring the lifetime of the ext-list handle in
view.c exactly. */

#if !defined(HIKARI_FOREIGN_TOPLEVEL_H)
#define HIKARI_FOREIGN_TOPLEVEL_H

#include <stdbool.h>

#include <wayland-server-core.h>

struct hikari_server;
struct hikari_view;

struct wlr_foreign_toplevel_handle_v1;
struct wlr_output;

/* [COMMENT] Class purpose: A view's participation in
zwlr_foreign_toplevel_management_v1. Embedded in hikari_view rather than
separately allocated, like hikari_view_decoration, so a mapped view carries no
extra allocation.

`handle` is NULL whenever the view is not mapped, which is what makes a closed
window disappear from a switcher rather than lingering as an entry nothing can
act on. `published_output` is the output last announced through the handle, kept
so an output change emits leave-then-enter rather than a second enter. */
struct hikari_foreign_toplevel {
  struct wlr_foreign_toplevel_handle_v1 *handle;
  struct hikari_view *view;
  struct wlr_output *published_output;

  struct wl_listener request_maximize;
  struct wl_listener request_minimize;
  struct wl_listener request_activate;
  struct wl_listener request_fullscreen;
  struct wl_listener request_close;
  struct wl_listener handle_destroy;
};

void
hikari_foreign_toplevel_manager_setup(struct hikari_server *server);

void
hikari_foreign_toplevel_init(
    struct hikari_foreign_toplevel *foreign_toplevel, struct hikari_view *view);

void
hikari_foreign_toplevel_create(
    struct hikari_foreign_toplevel *foreign_toplevel);

void
hikari_foreign_toplevel_destroy(
    struct hikari_foreign_toplevel *foreign_toplevel);

void
hikari_foreign_toplevel_publish_title(
    struct hikari_foreign_toplevel *foreign_toplevel);

void
hikari_foreign_toplevel_publish_state(
    struct hikari_foreign_toplevel *foreign_toplevel);

void
hikari_foreign_toplevel_publish_activated(
    struct hikari_foreign_toplevel *foreign_toplevel, bool activated);

void
hikari_foreign_toplevel_publish_output(
    struct hikari_foreign_toplevel *foreign_toplevel);

#endif
