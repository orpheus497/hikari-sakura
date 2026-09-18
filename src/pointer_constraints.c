/* Script function and purpose: zwp_pointer_constraints_v1 policy. Decides which
 * client constraint is live, tells the client when it starts and stops, and
 * answers the two questions the cursor path asks: is the pointer held, and is it
 * held by this view. */

#include <hikari/pointer_constraints.h>

#include <assert.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>

#include <hikari/animation.h>
#include <hikari/cursor.h>
#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/view.h>

/* Function purpose: Resolve the view backing a constrained surface, so a
surface-local coordinate can be converted to a layout one.

A linear walk of the visible views rather than a lookup table, because it needs
no new bookkeeping to be kept correct as views map, migrate and unmap. The list
is the visible views only -- single digits in any real session -- and the one
caller that runs per motion event is the confinement clamp, which is already
doing region arithmetic on the same event. Returns NULL for a surface that
belongs to no visible view -- a layer surface, a subsurface, or a view that has
just been hidden -- and every caller treats that as "no conversion available". */
static struct hikari_view *
view_for_surface(struct wlr_surface *surface)
{
  struct hikari_view *view;

  wl_list_for_each (view, &hikari_server.visible_views, visible_server_views) {
    if (view->surface == surface) {
      return view;
    }
  }

  return NULL;
}

/* Function purpose: Walk a constrained surface up to the surface that a
hikari_view actually owns, through BOTH parent chains.

Two chains, and neither alone is enough. wlr_surface_get_root_surface() climbs
subsurface parents but stops at an xdg popup, because a popup is not a
subsurface -- it is an xdg_surface of its own with its own wl_surface. So a
constraint attached to a popup resolved to the popup and matched no view, and
that is reachable rather than theoretical: wlr_xdg_surface_surface_at() tries
wlr_xdg_surface_popup_surface_at() BEFORE wlr_surface_surface_at(), so a popup
surface can become the seat's focused surface and carry an activated constraint.

Alternating the two climbs handles a subsurface of a popup of a subsurface, which
is what a menu inside a CSD window looks like.

IDENTITY ONLY. The result must never be used as a coordinate origin: a popup and
a subsurface each have their own space, so a region expressed against one of them
is not expressed against the view. Confinement declines those cases at activation
instead -- see hikari_pointer_constraint_refresh().

The depth bound is cheap insurance. xdg-shell forbids a cycle among popup
parents, but a compositor that loops forever on malformed client state is a
denial of service, and nothing real nests anywhere near this deep. */
static struct wlr_surface *
constrained_view_surface(struct wlr_surface *surface)
{
  for (int depth = 0; surface != NULL && depth < 32; depth++) {
    surface = wlr_surface_get_root_surface(surface);

    struct wlr_xdg_surface *xdg_surface =
        wlr_xdg_surface_try_from_wlr_surface(surface);

    if (xdg_surface == NULL ||
        xdg_surface->role != WLR_XDG_SURFACE_ROLE_POPUP ||
        xdg_surface->popup == NULL) {
      return surface;
    }

    surface = xdg_surface->popup->parent;
  }

  return surface;
}

/* Function purpose: Layout-space origin of a view's content, which is what a
surface-local coordinate has to be added to.

The animation offset is the third term and it is not optional. hikari's geometry
jumps to the destination the moment a move is committed, while the scene node
travels there over the animation, so the two disagree for the length of every
animation. node_at() in src/server.c carries the same correction for the same
reason. */
static bool
view_origin(struct hikari_view *view, double *ox, double *oy)
{
  if (view->output == NULL) {
    return false;
  }

  struct wlr_box *geometry = hikari_view_geometry(view);

  int animation_dx;
  int animation_dy;
  hikari_animation_offset(view, &animation_dx, &animation_dy);

  *ox = view->output->geometry.x + geometry->x + animation_dx;
  *oy = view->output->geometry.y + geometry->y + animation_dy;

  return true;
}

/* Function purpose: Put the cursor where the client asked it to reappear.

Callable only once the constraint is no longer the active one, which is what
stops it recursing: hikari_cursor_warp() breaks the active constraint, and by
here there is none. view_for_surface() returning NULL is the ordinary case for a
surface whose view has already been hidden or unmapped, and skipping the warp is
the right answer there. */
static void
warp_to_cursor_hint(struct wlr_pointer_constraint_v1 *wlr_constraint)
{
  if (!(wlr_constraint->current.committed &
          WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT)) {
    return;
  }

  struct hikari_view *view = view_for_surface(wlr_constraint->surface);
  double ox, oy;

  if (view == NULL || !view_origin(view, &ox, &oy)) {
    return;
  }

  hikari_cursor_warp(&hikari_server.cursor,
      (int)(ox + wlr_constraint->current.cursor_hint.x),
      (int)(oy + wlr_constraint->current.cursor_hint.y));
}

/* Function purpose: Nearest point inside a surface-local region to a
surface-local point that is outside it.

Needed because wlr_region_confine() refuses to work from a point that is already
outside the region, and a client may legitimately shrink its region out from
under the pointer at any commit. Without a way back in, the clamp in
hikari_pointer_constraint_confine() would hold the cursor still for ever.

Walks the region's rectangles and clamps into each, keeping the nearest result.
x2/y2 are exclusive, so the inclusive maximum is one short of each. */
static bool
region_closest_point(
    const pixman_region32_t *region, double x, double y, double *ox, double *oy)
{
  int nboxes = 0;
  const pixman_box32_t *boxes = pixman_region32_rectangles(
      (pixman_region32_t *)region, &nboxes);

  bool found = false;
  double best_x = 0;
  double best_y = 0;
  double best_distance = 0;

  for (int i = 0; i < nboxes; i++) {
    double cx = x;
    double cy = y;

    if (cx < boxes[i].x1) {
      cx = boxes[i].x1;
    } else if (cx > boxes[i].x2 - 1) {
      cx = boxes[i].x2 - 1;
    }

    if (cy < boxes[i].y1) {
      cy = boxes[i].y1;
    } else if (cy > boxes[i].y2 - 1) {
      cy = boxes[i].y2 - 1;
    }

    double dx = cx - x;
    double dy = cy - y;
    double distance = dx * dx + dy * dy;

    if (!found || distance < best_distance) {
      found = true;
      best_distance = distance;
      best_x = cx;
      best_y = cy;
    }
  }

  *ox = best_x;
  *oy = best_y;

  return found;
}

/* Function purpose: Put the cursor inside a confined constraint's region when it
is not already there. Runs when the constraint activates and again whenever the
client commits a new region.

Warps through wlr_cursor directly rather than hikari_cursor_warp(): this is the
constraint being honoured, not the compositor overriding it, so it must not take
the constraint down on its way past. */
static void
confine_cursor_to_region(struct hikari_pointer_constraint *constraint)
{
  struct wlr_pointer_constraint_v1 *wlr_constraint = constraint->wlr_constraint;

  if (wlr_constraint->type != WLR_POINTER_CONSTRAINT_V1_CONFINED) {
    return;
  }

  struct hikari_view *view = view_for_surface(wlr_constraint->surface);
  double ox, oy;

  if (view == NULL || !view_origin(view, &ox, &oy)) {
    return;
  }

  struct wlr_cursor *cursor = hikari_server.cursor.wlr_cursor;

  double cx = cursor->x - ox;
  double cy = cursor->y - oy;

  if (pixman_region32_contains_point(
          &wlr_constraint->region, (int)cx, (int)cy, NULL)) {
    return;
  }

  double nx, ny;

  if (region_closest_point(&wlr_constraint->region, cx, cy, &nx, &ny)) {
    wlr_cursor_warp_closest(cursor, NULL, nx + ox, ny + oy);
  }
}

/* Function purpose: End the active constraint.

The ordering below is load-bearing and must not be rearranged.
wlr_pointer_constraint_v1_send_deactivated() DESTROYS a ONESHOT constraint
outright, which runs constraint_destroy_handler and frees the wrapper -- so
hikari_server.active_constraint is cleared before the send rather than after, and
the hint is read out first. */
static void
deactivate(bool honour_hint)
{
  struct hikari_pointer_constraint *constraint =
      hikari_server.active_constraint;

  if (constraint == NULL) {
    return;
  }

  struct wlr_pointer_constraint_v1 *wlr_constraint = constraint->wlr_constraint;

  bool warp_to_hint = honour_hint &&
      (wlr_constraint->current.committed &
          WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT);
  double hint_x = wlr_constraint->current.cursor_hint.x;
  double hint_y = wlr_constraint->current.cursor_hint.y;
  struct wlr_surface *surface = wlr_constraint->surface;

  hikari_server.active_constraint = NULL;

  wlr_pointer_constraint_v1_send_deactivated(wlr_constraint);

  /* Action purpose: Read from the copies, not from wlr_constraint -- a ONESHOT
  constraint was freed by the call above. */
  if (warp_to_hint) {
    struct hikari_view *view = view_for_surface(surface);
    double ox, oy;

    if (view != NULL && view_origin(view, &ox, &oy)) {
      hikari_cursor_warp(
          &hikari_server.cursor, (int)(ox + hint_x), (int)(oy + hint_y));
    }
  }
}

static void
constraint_destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_pointer_constraint *constraint =
      wl_container_of(listener, constraint, destroy);

  /* Action purpose: Drop the reference before the storage goes away.
  Deliberately NOT through deactivate() -- wlroots is already destroying the
  resource, so there is nothing left to tell the client, and send_deactivated()
  on a constraint mid-destruction is what the `destroying` flag in wlroots exists
  to defend against.

  The hint is still honoured. This is the ordinary way a lock ends -- a client
  releasing the pointer destroys its locked_pointer object rather than waiting to
  be deactivated -- so skipping it here would mean the cursor almost never
  reappeared where the client asked. */
  if (hikari_server.active_constraint == constraint) {
    hikari_server.active_constraint = NULL;
    warp_to_cursor_hint(constraint->wlr_constraint);
  }

  /* Action purpose: Both removals are mandatory. wlroots asserts these listener
  lists are empty immediately after emitting this signal, and release builds
  define NDEBUG -- so omitting either does not abort, it leaves a freed list
  reachable. */
  wl_list_remove(&constraint->set_region.link);
  wl_list_remove(&constraint->destroy.link);

  hikari_free(constraint);
}

/* Class purpose: Armed when a region change needs the activation decision taken
again, cleared when it fires. One at a time -- the decision reads global state,
so a second pending call would compute the same answer. */
static struct wl_event_source *refresh_idle = NULL;

static void
refresh_idle_handler(void *data)
{
  refresh_idle = NULL;

  hikari_pointer_constraint_refresh();
}

/* Function purpose: Re-take the activation decision, but NOT from inside the
surface commit that prompted it.

This deferral is mandatory and must not be inlined away. Re-deciding can
deactivate, deactivating sends `unconfined`, and
wlr_pointer_constraint_v1_send_deactivated() DESTROYS a ONESHOT constraint --
which calls wlr_surface_synced_finish() and so wl_list_remove()s the constraint's
synced entry. wlroots is iterating exactly that list with a plain,
non-safe wl_list_for_each when it dispatches the commit
(types/wlr_compositor.c, the loop that calls synced->impl->commit), so tearing
the constraint down underneath it frees the node the loop is standing on.

An idle runs once the commit has unwound, where the destroy is harmless. */
static void
schedule_refresh(void)
{
  if (refresh_idle != NULL) {
    return;
  }

  refresh_idle = wl_event_loop_add_idle(
      hikari_server.event_loop, refresh_idle_handler, NULL);

  if (refresh_idle == NULL) {
    wlr_log(WLR_ERROR,
        "could not schedule a pointer-constraint refresh; a constraint whose "
        "region just emptied will stay active until the next focus change");
  }
}

/* Function purpose: The constrained region changed.

Nothing is cached across this: the clamp reads wlr_constraint->region fresh on
every motion event, so a new region takes effect on the next one with no
invalidation needed. Two things do need doing.

The cursor is rescued if the client has just shrunk the region out from under it
-- see confine_cursor_to_region(). And the activation decision is taken again,
because a region that has become EMPTY can no longer be enforced and the client
should be told so rather than left believing a confinement that is not happening.
A later non-empty region reactivates through the same path.

A locked pointer consults no region, so only the confined case has work here, and
only for the constraint that is actually in force. */
static void
constraint_set_region_handler(struct wl_listener *listener, void *data)
{
  struct hikari_pointer_constraint *constraint =
      wl_container_of(listener, constraint, set_region);

  /* Action purpose: Only the constraint in force has a cursor worth rescuing. */
  if (hikari_server.active_constraint == constraint) {
    confine_cursor_to_region(constraint);
  }

  /* Action purpose: Re-decide for EVERY constraint, active or not, and that
  asymmetry is the point. A confined constraint declined for an empty region is
  not the active one, so gating this on that test would strand it: the client
  could commit a perfectly good region afterwards and nothing would look again
  until an unrelated focus change happened by. */
  schedule_refresh();
}

static void
new_constraint_handler(struct wl_listener *listener, void *data)
{
  struct wlr_pointer_constraint_v1 *wlr_constraint = data;

  struct hikari_pointer_constraint *constraint =
      hikari_malloc(sizeof(struct hikari_pointer_constraint));

  constraint->wlr_constraint = wlr_constraint;
  wlr_constraint->data = constraint;

  constraint->set_region.notify = constraint_set_region_handler;
  wl_signal_add(&wlr_constraint->events.set_region, &constraint->set_region);

  constraint->destroy.notify = constraint_destroy_handler;
  wl_signal_add(&wlr_constraint->events.destroy, &constraint->destroy);

  /* Action purpose: A constraint is created the moment a client asks, which is
  usually while it already holds pointer focus -- so offer it straight away
  rather than waiting for the next motion event. */
  hikari_pointer_constraint_refresh();
}

void
hikari_pointer_constraints_setup(struct hikari_server *server)
{
  server->active_constraint = NULL;

  if (server->pointer_constraints == NULL) {
    wl_list_init(&server->new_pointer_constraint.link);
    return;
  }

  server->new_pointer_constraint.notify = new_constraint_handler;
  wl_signal_add(&server->pointer_constraints->events.new_constraint,
      &server->new_pointer_constraint);
}

void
hikari_pointer_constraints_fini(struct hikari_server *server)
{
  /* Action purpose: The link is always initialised -- by wl_signal_add above, or
  by wl_list_init on the no-manager path -- so this is unconditionally safe.

  No constraint teardown happens here and none is needed: hikari_server_stop()
  calls wl_display_destroy_clients() before reaching this, so every constraint
  has already been destroyed by its own client and constraint_destroy_handler has
  already run for each. */
  wl_list_remove(&server->new_pointer_constraint.link);
  server->active_constraint = NULL;

  /* Action purpose: Disarm a pending refresh. A client's last commit can arm one
  moments before shutdown, and the event loop outlives this function -- it is
  destroyed with the display at the very end of hikari_server_stop() -- so an
  idle left armed here would still fire, against a seat and constraint set that
  are already gone. */
  if (refresh_idle != NULL) {
    wl_event_source_remove(refresh_idle);
    refresh_idle = NULL;
  }
}

void
hikari_pointer_constraint_refresh(void)
{
  struct hikari_server *server = &hikari_server;

  if (server->pointer_constraints == NULL) {
    return;
  }

  struct wlr_pointer_constraint_v1 *wanted = NULL;

  /* Action purpose: Only normal mode may hold the pointer. Every other mode
  either drags the cursor itself (move, resize), redirects it (dnd), or has taken
  the screen away entirely (lock) -- and hikari_server_in_normal_mode() excludes
  all of them, lock mode included, in one test. */
  if (hikari_server_in_normal_mode()) {
    struct wlr_surface *surface = server->seat->pointer_state.focused_surface;

    if (surface != NULL) {
      wanted = wlr_pointer_constraints_v1_constraint_for_surface(
          server->pointer_constraints, surface, server->seat);
    }
  }

  /* Action purpose: Never tell a client it is CONFINED unless the region can
  actually be enforced.

  Confinement needs an exact layout origin for the constrained surface, and
  view_for_surface() resolves only a view's own top-level surface. A constraint
  attached to anything else -- a subsurface, a popup, a layer surface -- has no
  origin here, so hikari_pointer_constraint_confine() would return false on every
  motion event and the pointer would roam freely while the client believed it
  held. Declining to activate is the honest answer: a client that is never told
  it is confined knows that it is not.

  An EMPTY effective region fails for the same reason and is declined the same
  way. wlroots builds it as the client's region intersected with the surface's
  input region, so it is legitimately empty whenever those do not overlap, and a
  region that confines to nowhere cannot be enforced at all. Declining here also
  makes a region that BECOMES empty deactivate rather than silently stop working,
  because constraint_set_region_handler() re-enters this function.

  LOCKED constraints need no origin and no region, and are deliberately not
  gated. A locked pointer simply does not move, whatever surface it is attached
  to; the origin matters only to the cursor hint on release, which already skips
  the warp when it cannot be resolved. */
  if (wanted != NULL && wanted->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
    struct hikari_view *view = view_for_surface(wanted->surface);
    double ox, oy;

    if (view == NULL || !view_origin(view, &ox, &oy) ||
        !pixman_region32_not_empty(&wanted->region)) {
      wanted = NULL;
    }
  }

  struct hikari_pointer_constraint *active = server->active_constraint;

  if (active != NULL && active->wlr_constraint == wanted) {
    return;
  }

  deactivate(true);

  if (wanted != NULL) {
    server->active_constraint = wanted->data;
    wlr_pointer_constraint_v1_send_activated(wanted);

    /* Action purpose: A client may confine to a region the cursor is not
    currently inside -- it picks the region, not the moment. Pull the cursor in
    on activation so the very first motion event has a valid starting point;
    without it wlr_region_confine() would refuse from outside and the clamp
    below would hold the pointer still. No-ops for a locked constraint. */
    confine_cursor_to_region(server->active_constraint);
  }
}

/* Function purpose: Apply a confined constraint to one motion event.

Answers the cursor path's question -- "is this pointer confined, and if so where
does this delta actually land it" -- in layout coordinates, so cursor.c needs to
know nothing about regions or surface-local space.

Returns false when nothing is confined, and the caller moves the cursor normally.
Returns true having written the destination, INCLUDING the case where the cursor
is already outside the region and must not move: that is a legitimate state after
a client shrinks its region, and reporting it as "no confinement" would let the
pointer escape on the one event where the region is smallest. */
bool
hikari_pointer_constraint_confine(
    double dx, double dy, double *out_x, double *out_y)
{
  struct hikari_pointer_constraint *constraint =
      hikari_server.active_constraint;

  if (constraint == NULL) {
    return false;
  }

  struct wlr_pointer_constraint_v1 *wlr_constraint = constraint->wlr_constraint;

  if (wlr_constraint->type != WLR_POINTER_CONSTRAINT_V1_CONFINED) {
    return false;
  }

  /* Action purpose: Surface-local to layout, through the same origin the
  confinement region is expressed against -- output position, view geometry, and
  the animation offset that node_at() also carries. Without the third term the
  boundary would sit where a moving window is HEADED rather than where it is
  drawn, and the pointer would fight an invisible wall for the length of every
  animation. */
  struct hikari_view *view = view_for_surface(wlr_constraint->surface);
  double ox, oy;

  if (view == NULL || !view_origin(view, &ox, &oy)) {
    return false;
  }

  /* Action purpose: An EMPTY effective region confines to nowhere, so report it
  as unconfined and let the pointer move normally.

  wlroots builds this region as the client's region intersected with the
  surface's input region, so it is legitimately empty whenever the two do not
  overlap -- including for a surface with no input region at all. Falling through
  would freeze the pointer outright: wlr_region_confine() returns false for an
  empty region, the branch below would hold the cursor still, and
  confine_cursor_to_region() could not rescue it either because
  region_closest_point() has no rectangle to clamp into.

  This is NOT made redundant by the matching gate in
  hikari_pointer_constraint_refresh(). That gate takes the constraint down for
  real, but it runs from an idle, and motion events dispatched between the commit
  that emptied the region and that idle arrive here with the constraint still
  active. This covers exactly that window. */
  if (!pixman_region32_not_empty(&wlr_constraint->region)) {
    return false;
  }

  struct wlr_cursor *cursor = hikari_server.cursor.wlr_cursor;

  double cx = cursor->x - ox;
  double cy = cursor->y - oy;

  double nx, ny;

  if (!wlr_region_confine(
          &wlr_constraint->region, cx, cy, cx + dx, cy + dy, &nx, &ny)) {
    /* Action purpose: The OLD point was already outside the region, which
    wlr_region_confine() reports as false and which is not an error. Hold still
    rather than moving; constraint_set_region_handler() is what puts the cursor
    back inside when the region is what changed. */
    *out_x = cursor->x;
    *out_y = cursor->y;

    return true;
  }

  *out_x = nx + ox;
  *out_y = ny + oy;

  return true;
}

void
hikari_pointer_constraint_deactivate(void)
{
  deactivate(true);
}

void
hikari_pointer_constraint_break_for_warp(void)
{
  deactivate(false);
}

bool
hikari_pointer_constraint_is_locked(void)
{
  struct hikari_pointer_constraint *constraint =
      hikari_server.active_constraint;

  return constraint != NULL &&
         constraint->wlr_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED;
}

bool
hikari_pointer_constraint_holds_view(struct hikari_view *view)
{
  struct hikari_pointer_constraint *constraint =
      hikari_server.active_constraint;

  if (constraint == NULL || view == NULL || view->surface == NULL) {
    return false;
  }

  /* Action purpose: Resolve the constrained surface to the one its VIEW owns,
  because a constraint may be attached to a child of the window rather than to
  the window's own surface, and this question is about which view holds the
  pointer.

  A direct comparison against view->surface answered "no" for every such
  constraint, and each caller does real damage on a false negative: the three
  geometry-commit sites would recentre the cursor and break a lock the window
  never lost, and -- far worse -- hikari_view_unmap() would skip its deactivation
  and leave the pointer frozen for the rest of the session, which is the exact
  failure that guard exists to prevent.

  Both parent chains are walked, subsurface and xdg popup; see
  constrained_view_surface() for why either alone leaves a hole. That resolution
  is deliberately NOT used for the coordinate lookups in this file -- a child
  surface's region stays expressed in ITS OWN space, so identity is sound but the
  view's origin is not a substitute for the child's. Confinement declines those
  cases at activation instead. */
  return constrained_view_surface(constraint->wlr_constraint->surface) ==
         view->surface;
}
