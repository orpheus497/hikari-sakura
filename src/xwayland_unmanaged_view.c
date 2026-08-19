#ifdef HAVE_XWAYLAND
#include <hikari/xwayland_unmanaged_view.h>

#include <wlr/xwayland.h>

#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/workspace.h>

static bool
was_updated(struct wlr_xwayland_surface *surface,
    struct wlr_box *geometry,
    struct hikari_output *output)
{
  return !((surface->x - output->geometry.x == geometry->x) &&
           (surface->y - output->geometry.y == geometry->y) &&
           (surface->width == geometry->width) &&
           (surface->height == geometry->height));
}

static void
recalculate_geometry(struct wlr_box *geometry,
    struct wlr_xwayland_surface *surface,
    struct hikari_output *output)
{
  geometry->x = surface->x - output->geometry.x;
  geometry->y = surface->y - output->geometry.y;
  geometry->width = surface->width;
  geometry->height = surface->height;
}

static void
commit_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
      wl_container_of(listener, xwayland_unmanaged_view, commit);

  struct hikari_output *output = xwayland_unmanaged_view->workspace->output;
  struct wlr_xwayland_surface *surface = xwayland_unmanaged_view->surface;
  struct wlr_box *geometry = &xwayland_unmanaged_view->geometry;

  if (was_updated(surface, geometry, output)) {
    hikari_output_add_damage(output, &xwayland_unmanaged_view->geometry);

    recalculate_geometry(geometry, surface, output);

    hikari_output_add_damage(output, geometry);
  } else if (output->enabled) {
    hikari_output_add_effective_surface_damage(
        output, surface->surface, geometry->x, geometry->y);
  }
}

static void
map_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
      wl_container_of(listener, xwayland_unmanaged_view, map);

#if !defined(NDEBUG)
  printf("UNMANAGED XWAYLAND MAP %p %d %d\n",
      xwayland_unmanaged_view,
      xwayland_unmanaged_view->surface->x,
      xwayland_unmanaged_view->surface->y);
#endif

  struct wlr_box *geometry = &xwayland_unmanaged_view->geometry;
  struct wlr_xwayland_surface *xwayland_surface =
      xwayland_unmanaged_view->surface;
  struct hikari_output *output = xwayland_unmanaged_view->workspace->output;

  xwayland_unmanaged_view->hidden = false;

  recalculate_geometry(geometry, xwayland_surface, output);

  xwayland_unmanaged_view->commit.notify = commit_handler;
  wl_signal_add(&xwayland_surface->surface->events.commit,
      &xwayland_unmanaged_view->commit);

  wl_list_insert(&output->unmanaged_xwayland_views,
      &xwayland_unmanaged_view->unmanaged_output_views);

  hikari_output_add_damage(output, geometry);
}

static void
unmap(struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view)
{
  wl_list_remove(&xwayland_unmanaged_view->commit.link);
  wl_list_remove(&xwayland_unmanaged_view->unmanaged_output_views);

  xwayland_unmanaged_view->hidden = true;

  hikari_output_add_damage(xwayland_unmanaged_view->workspace->output,
      &xwayland_unmanaged_view->geometry);
}

static void
unmap_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
      wl_container_of(listener, xwayland_unmanaged_view, unmap);

#if !defined(NDEBUG)
  printf("UNMANAGED XWAYLAND UNMAP %p\n", xwayland_unmanaged_view);
#endif

  unmap(xwayland_unmanaged_view);
}

static void
destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
      wl_container_of(listener, xwayland_unmanaged_view, destroy);

#if !defined(NDEBUG)
  printf("UNMANAGED XWAYLAND DESTROY %p\n", xwayland_unmanaged_view);
#endif

  if (!xwayland_unmanaged_view->hidden) {
    unmap(xwayland_unmanaged_view);
    hikari_server_cursor_focus();
  }

  wl_list_remove(&xwayland_unmanaged_view->map.link);
  wl_list_remove(&xwayland_unmanaged_view->unmap.link);
  wl_list_remove(&xwayland_unmanaged_view->associate.link);
  wl_list_remove(&xwayland_unmanaged_view->dissociate.link);
  wl_list_remove(&xwayland_unmanaged_view->destroy.link);
  wl_list_remove(&xwayland_unmanaged_view->request_configure.link);

  hikari_free(xwayland_unmanaged_view);
}

static void
request_configure_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
      wl_container_of(listener, xwayland_unmanaged_view, request_configure);

  struct wlr_xwayland_surface *surface = xwayland_unmanaged_view->surface;
  struct wlr_xwayland_surface_configure_event *event = data;

  wlr_xwayland_surface_configure(
      surface, event->x, event->y, event->width, event->height);
}

static struct wlr_surface *
surface_at(
    struct hikari_node *node, double lx, double ly, double *sx, double *sy)
{
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
      (struct hikari_xwayland_unmanaged_view *)node;

  struct wlr_box *geometry = &xwayland_unmanaged_view->geometry;

  double x = lx - geometry->x;
  double y = ly - geometry->y;

  return wlr_surface_surface_at(
      xwayland_unmanaged_view->surface->surface, x, y, sx, sy);
}

static void
focus(struct hikari_node *node)
{}

/* Function purpose: Wire map/unmap listeners to the underlying wlr_surface
once wlroots associates it with the X11 surface. Called either directly from
hikari_xwayland_unmanaged_view_init (when surface is already associated) or
deferred via associate_handler (wlroots 0.20 may deliver surface later). */
static void
attach_surface_listeners_unmanaged(
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view)
{
  struct wlr_xwayland_surface *xwayland_surface =
      xwayland_unmanaged_view->surface;

  xwayland_unmanaged_view->map.notify = map_handler;
  wl_signal_add(
      &xwayland_surface->surface->events.map, &xwayland_unmanaged_view->map);

  xwayland_unmanaged_view->unmap.notify = unmap_handler;
  wl_signal_add(
      &xwayland_surface->surface->events.unmap,
      &xwayland_unmanaged_view->unmap);
}

/* Function purpose: wlroots 0.20 lifecycle hook -- the wlr_surface only
becomes valid when `associate` fires (it is NULL between new_surface and
associate for some surface types). Mirrors associate_handler in
xwayland_view.c. */
static void
associate_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
      wl_container_of(listener, xwayland_unmanaged_view, associate);

  attach_surface_listeners_unmanaged(xwayland_unmanaged_view);
}

/* Function purpose: Drop map/unmap listeners when wlroots dissociates the
wlr_surface (e.g. X11 surface recreation). Links are re-initialised so a
later re-associate and destroy_handler remain safe. Mirrors
dissociate_handler in xwayland_view.c. */
static void
dissociate_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
      wl_container_of(listener, xwayland_unmanaged_view, dissociate);

  wl_list_remove(&xwayland_unmanaged_view->map.link);
  wl_list_init(&xwayland_unmanaged_view->map.link);
  wl_list_remove(&xwayland_unmanaged_view->unmap.link);
  wl_list_init(&xwayland_unmanaged_view->unmap.link);
}

/* Function purpose: Initialise an unmanaged (override-redirect) XWayland
view wrapper. Wires the full wlroots 0.20 lifecycle: map/unmap links are
pre-initialised with wl_list_init so destroy_handler can always call
wl_list_remove safely, regardless of whether surface association has
occurred. Associate/dissociate listeners defer or withdraw map/unmap wiring
around the wlr_surface lifetime, mirroring hikari_xwayland_view_init. */
void
hikari_xwayland_unmanaged_view_init(
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view,
    struct wlr_xwayland_surface *xwayland_surface,
    struct hikari_workspace *workspace)
{
  xwayland_unmanaged_view->workspace = workspace;
  xwayland_unmanaged_view->node.surface_at = surface_at;
  xwayland_unmanaged_view->node.focus = focus;

#if !defined(NDEBUG)
  printf("UNMANAGED XWAYLAND NEW %p\n", xwayland_unmanaged_view);
#endif

  wlr_xwayland_surface_ping(xwayland_surface);

  xwayland_unmanaged_view->surface = xwayland_surface;
  xwayland_unmanaged_view->surface->data =
      (struct hikari_node *)xwayland_unmanaged_view;
  xwayland_unmanaged_view->hidden = true;

  /* Action purpose: Pre-initialise map/unmap listener links as empty lists so
  wl_list_remove in destroy_handler is always safe, even when the surface was
  never associated (and therefore map/unmap were never wl_signal_add'd). This
  eliminates the undefined behaviour on every override-redirect window close. */
  wl_list_init(&xwayland_unmanaged_view->map.link);
  wl_list_init(&xwayland_unmanaged_view->unmap.link);

  /* Action purpose: Subscribe to associate/dissociate so map/unmap listeners
  are wired to the wlr_surface only while it is valid. wlroots 0.20 may not
  have populated xwayland_surface->surface yet at new-surface time. */
  xwayland_unmanaged_view->associate.notify = associate_handler;
  wl_signal_add(
      &xwayland_surface->events.associate,
      &xwayland_unmanaged_view->associate);

  xwayland_unmanaged_view->dissociate.notify = dissociate_handler;
  wl_signal_add(
      &xwayland_surface->events.dissociate,
      &xwayland_unmanaged_view->dissociate);

  /* Action purpose: If the wlr_surface is already associated at init time
  (common for override-redirect surfaces), wire map/unmap immediately rather
  than waiting for the associate signal, matching xwayland_view.c:513. */
  if (xwayland_surface->surface != NULL) {
    attach_surface_listeners_unmanaged(xwayland_unmanaged_view);
  }

  xwayland_unmanaged_view->destroy.notify = destroy_handler;
  wl_signal_add(
      &xwayland_surface->events.destroy, &xwayland_unmanaged_view->destroy);

  xwayland_unmanaged_view->request_configure.notify = request_configure_handler;
  wl_signal_add(&xwayland_surface->events.request_configure,
      &xwayland_unmanaged_view->request_configure);
}

void
hikari_xwayland_unmanaged_evacuate(
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view,
    struct hikari_workspace *workspace)
{
  struct hikari_output *output = workspace->output;
  struct wlr_xwayland_surface *surface = xwayland_unmanaged_view->surface;
  struct wlr_box *geometry = &xwayland_unmanaged_view->geometry;

  xwayland_unmanaged_view->workspace = workspace;

  recalculate_geometry(geometry, surface, output);

  hikari_output_add_damage(output, &xwayland_unmanaged_view->geometry);
}
#endif
