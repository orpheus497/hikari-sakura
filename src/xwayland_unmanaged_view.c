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

  /* [COMMENT] Action purpose: Safe-bail when this view has outlived its output.
  hikari_xwayland_unmanaged_detach() clears workspace at output teardown; both
  it and the output it pointed at are freed immediately afterwards, so
  dereferencing here would be a use-after-free. */
  if (xwayland_unmanaged_view->workspace == NULL) {
    return;
  }

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

/* Function purpose: Perform the map transition for an override-redirect view.
Split out of map_handler so hikari_xwayland_unmanaged_view_init can drive it
directly when adopting a surface that is already mapped (the re-adoption path
from set_override_redirect_handler), where the wlr_surface map event has
already fired and will not fire again. */
static void
map_unmanaged(
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view)
{
  /* [COMMENT] Action purpose: Safe-bail when this view has outlived its output
  (see commit_handler). Mapping it would relink it into a freed output list. */
  if (xwayland_unmanaged_view->workspace == NULL) {
    return;
  }

  /* [COMMENT] Action purpose: Ignore a redundant map. wlr_surface_map already
  suppresses duplicate events, but the init-time adoption above can race one,
  and a second insert of the same link would corrupt the output list. */
  if (!xwayland_unmanaged_view->hidden) {
    return;
  }

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

  map_unmanaged(xwayland_unmanaged_view);
}

static void
unmap(struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view)
{
  /* [COMMENT] Action purpose: Make unmap idempotent. unmap_handler calls this
  unconditionally on every wlr_surface unmap, while destroy_handler calls it
  only when not already hidden -- so without this guard a detached view (one
  whose output was torn down under it) would re-run the removals below and then
  damage a freed output. */
  if (xwayland_unmanaged_view->hidden) {
    return;
  }

  wl_list_remove(&xwayland_unmanaged_view->commit.link);
  wl_list_init(&xwayland_unmanaged_view->commit.link);

  /* [COMMENT] Action purpose: Remove-then-init, matching the convention used
  for every other link in the tree. Leaving the link pointing at the output
  list it was just removed from makes a second removal -- or a removal after
  that output has been freed -- an arbitrary write instead of a no-op. */
  wl_list_remove(&xwayland_unmanaged_view->unmanaged_output_views);
  wl_list_init(&xwayland_unmanaged_view->unmanaged_output_views);

  xwayland_unmanaged_view->hidden = true;

  if (xwayland_unmanaged_view->workspace != NULL) {
    hikari_output_add_damage(xwayland_unmanaged_view->workspace->output,
        &xwayland_unmanaged_view->geometry);
  }
}

void
hikari_xwayland_unmanaged_detach(
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view)
{
  /* [COMMENT] Action purpose: Run the normal unmap teardown first, while the
  output is still alive, so the commit listener and the output-list link are
  released through the single path that owns them. unmap() is idempotent and
  no-ops if this view was already unmapped. */
  unmap(xwayland_unmanaged_view);

  /* [COMMENT] Action purpose: Drop the workspace reference last. The workspace
  and its output are freed immediately after hikari_output_fini() returns, so
  every consumer must see NULL rather than a stale pointer from here on. */
  xwayland_unmanaged_view->workspace = NULL;
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

/* Function purpose: Tear the unmanaged wrapper down completely and free it.
Shared by destroy_handler (the X11 surface going away) and
set_override_redirect_handler (the same surface being re-adopted as a managed
view), so both paths release exactly the same listener set. */
static void
xwayland_unmanaged_view_destroy(
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view)
{
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
  wl_list_remove(&xwayland_unmanaged_view->set_override_redirect.link);

  hikari_free(xwayland_unmanaged_view);
}

static void
destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
      wl_container_of(listener, xwayland_unmanaged_view, destroy);

#if !defined(NDEBUG)
  printf("UNMANAGED XWAYLAND DESTROY %p\n", xwayland_unmanaged_view);
#endif

  xwayland_unmanaged_view_destroy(xwayland_unmanaged_view);
}

/* Function purpose: Re-adopt this surface as a managed view when the client
clears the override_redirect attribute after the surface was created -- the
inverse of the handler in xwayland_view.c, and needed for the same reason:
the managed/unmanaged decision was previously made once and never revisited.

This runs re-entrantly: it frees the wrapper holding the currently-executing
listener and then registers a fresh listener on the very signal being emitted.
Both halves are safe. wl_signal_emit_mutable() exists precisely to let a
listener remove itself mid-emission, and the replacement wrapper installs the
OPPOSITE guard -- xwayland_view.c's handler returns unless override_redirect is
now set, which it is not on this path -- so even if that new listener were
reached by the in-flight emission it would do nothing. The transition therefore
cannot loop back on itself. */
static void
set_override_redirect_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
      wl_container_of(listener, xwayland_unmanaged_view, set_override_redirect);

  struct wlr_xwayland_surface *xwayland_surface =
      xwayland_unmanaged_view->surface;

  if (xwayland_surface->override_redirect) {
    return;
  }

  /* [COMMENT] Action purpose: Capture the surface before the wrapper is freed,
  then hand it to the shared adoption path, which re-wraps it as the managed
  type and re-maps it if it is already mapped. */
  xwayland_unmanaged_view_destroy(xwayland_unmanaged_view);

  hikari_server_adopt_xwayland_surface(xwayland_surface);
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

  /* [COMMENT] Action purpose: Initialise the output-list link before it is ever
  inserted. hikari_malloc does not zero, so until map_handler inserts it this
  field otherwise holds allocator garbage, and any unlink reached before the
  first map (evacuation of a never-mapped surface, or the output_fini sweep)
  would write through those garbage pointers. */
  wl_list_init(&xwayland_unmanaged_view->unmanaged_output_views);

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

  /* [COMMENT] Action purpose: Watch for the override_redirect attribute being
  cleared after creation, so a surface that stops being a menu/tooltip is
  re-adopted as a managed view rather than staying wrongly unmanaged. */
  xwayland_unmanaged_view->set_override_redirect.notify =
      set_override_redirect_handler;
  wl_signal_add(&xwayland_surface->events.set_override_redirect,
      &xwayland_unmanaged_view->set_override_redirect);

  /* [COMMENT] Action purpose: Adopt a surface that is already mapped -- the
  re-adoption path from the managed side, whose wlr_surface map event has
  already fired and will not fire again. map_unmanaged() no-ops when the view
  is not currently hidden, so this is safe on the ordinary new_surface path. */
  if (xwayland_surface->surface != NULL && xwayland_surface->surface->mapped) {
    map_unmanaged(xwayland_unmanaged_view);
  }
}

/* Function purpose: Re-home an override-redirect view onto another workspace
when its current output is being torn down (VT switch, monitor unplug), so it
outlives that output rather than pointing into it. The managed counterpart is
hikari_view_evacuate() in view.c; the two must stay symmetric.

The list move below is the reason this function exists and was the defect it
was missing: hikari_workspace_merge() calls this from hikari_output_fini(),
which then frees both the workspace and the output. Updating ->workspace while
leaving unmanaged_output_views linked into the OLD output's list left every
live menu, tooltip and dropdown holding a link into a freed wl_list head --
the next commit, unmap or node_at lookup wrote through it, producing either an
immediate SIGSEGV or silent heap corruption surfacing later as a SIGABRT. This
mirrors view.c's own documented reasoning for moving links unconditionally. */
void
hikari_xwayland_unmanaged_evacuate(
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view,
    struct hikari_workspace *workspace)
{
  struct hikari_output *output = workspace->output;
  struct wlr_xwayland_surface *surface = xwayland_unmanaged_view->surface;
  struct wlr_box *geometry = &xwayland_unmanaged_view->geometry;

  xwayland_unmanaged_view->workspace = workspace;

  /* [COMMENT] Action purpose: Move the output-list link to the destination
  output before anything else can observe the view. Done unconditionally: the
  link is wl_list_init'ed at init and re-init'ed by unmap(), so for a currently
  unmapped view this is a self-referencing remove followed by a re-init, which
  leaves it exactly as it was and keeps the mapped/unmapped cases identical. */
  wl_list_remove(&xwayland_unmanaged_view->unmanaged_output_views);
  if (!xwayland_unmanaged_view->hidden) {
    wl_list_insert(&output->unmanaged_xwayland_views,
        &xwayland_unmanaged_view->unmanaged_output_views);
  } else {
    wl_list_init(&xwayland_unmanaged_view->unmanaged_output_views);
  }

  recalculate_geometry(geometry, surface, output);

  hikari_output_add_damage(output, &xwayland_unmanaged_view->geometry);
}
#endif
