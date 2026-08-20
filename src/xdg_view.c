// [COMMENT] Script function and purpose: Native Wayland XDG shell surface view implementation and event handling.

#include <hikari/xdg_view.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <wlr/types/wlr_cursor.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/edges.h>

#include <hikari/configuration.h>
#include <hikari/geometry.h>
#include <hikari/indicator_frame.h>
#include <hikari/mark.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/sheet.h>
#include <hikari/view.h>
#include <hikari/view_config.h>
#include <hikari/workspace.h>

static void
new_popup_handler(struct wl_listener *listener, void *data);

static void
request_fullscreen_handler(struct wl_listener *listener, void *data);

static void
apply_requested_fullscreen(struct hikari_xdg_view *xdg_view);

static void
set_title_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xdg_view *xdg_view =
      wl_container_of(listener, xdg_view, set_title);

  hikari_view_set_title(
      (struct hikari_view *)xdg_view, xdg_view->surface->toplevel->title);
}

static void
commit_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xdg_view *xdg_view =
      wl_container_of(listener, xdg_view, commit);

  struct hikari_view *view = (struct hikari_view *)xdg_view;
  struct wlr_xdg_surface *surface = xdg_view->surface;

  // [COMMENT] Action purpose: Handle the wlroots 0.20 initial_commit lifecycle.
// When an xdg_surface performs its first commit, the compositor must reply
// with a configure (set_size 0,0 lets the client pick its own dimensions).
// This sets surface->initialized = true inside wlroots, allowing the
// client to map. Without this, all subsequent configure calls will crash
// with assert(surface->initialized). See tinywl.c xdg_toplevel_commit.
  if (surface->initial_commit) {
    // [COMMENT] Action purpose: Call wlr_xdg_toplevel_set_size to properly initialize and configure the toplevel surface in wlroots 0.20 instead of scheduling configure directly.
    wlr_xdg_toplevel_set_size(xdg_view->xdg_toplevel, 0, 0);
    return;
  }

  if (!surface->surface->mapped) {
    return;
  }

  uint32_t serial = surface->current.configure_serial;

  assert(view->surface != NULL);

  if (hikari_view_was_updated(view, serial)) {
    struct wlr_box new_geometry = surface->geometry;

    switch (view->pending_operation.type) {
      case HIKARI_OPERATION_TYPE_TILE:
      case HIKARI_OPERATION_TYPE_FULL_MAXIMIZE:
      case HIKARI_OPERATION_TYPE_VERTICAL_MAXIMIZE:
      case HIKARI_OPERATION_TYPE_HORIZONTAL_MAXIMIZE:
        wlr_xdg_toplevel_set_tiled(xdg_view->xdg_toplevel,
            WLR_EDGE_LEFT | WLR_EDGE_RIGHT | WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
        break;

      case HIKARI_OPERATION_TYPE_RESET:
      case HIKARI_OPERATION_TYPE_UNMAXIMIZE:
        wlr_xdg_toplevel_set_tiled(xdg_view->xdg_toplevel, WLR_EDGE_NONE);
        break;

      case HIKARI_OPERATION_TYPE_RESIZE:
        break;
    }
    hikari_view_commit_pending_operation(view, &new_geometry);
  } else {
    struct wlr_box *geometry = hikari_view_geometry(view);
    struct hikari_output *output = view->output;
    bool visible = !hikari_view_is_hidden(view);

    struct wlr_box new_geometry = surface->geometry;

    if (new_geometry.width != geometry->width ||
        new_geometry.height != geometry->height) {
      if (visible) {
        hikari_view_damage_whole(view);
      }

      geometry->width = new_geometry.width;
      geometry->height = new_geometry.height;

      hikari_view_refresh_geometry(view, geometry);

      if (visible) {
        hikari_view_damage_whole(view);
      } else if (output->enabled) {
        hikari_output_schedule_frame(output);
      }
    } else if (output->enabled) {
      if (visible) {
        hikari_output_add_effective_surface_damage(
            output, surface->surface, geometry->x, geometry->y);
      } else {
        hikari_output_schedule_frame(output);
      }
    }
  }
}

static inline const char *
get_app_id(struct hikari_xdg_view *xdg_view)
{
  const char *app_id = xdg_view->surface->toplevel->app_id;

  return app_id == NULL ? "" : app_id;
}

/* [COMMENT] Function purpose: Perform one-time configuration of a newly
mapped xdg view -- adopt the surface geometry, resolve the view config by
app_id, set the title, and configure. Invoked by map_handler on first map. */
static void
first_map(struct hikari_xdg_view *xdg_view)
{
  struct wlr_xdg_surface *xdg_surface = xdg_view->surface;
  assert(xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

  struct hikari_view *view = (struct hikari_view *)xdg_view;
  struct wlr_box *geometry = &xdg_view->view.geometry;

  if (xdg_surface->surface->mapped) {
    *geometry = xdg_surface->geometry;
    if (geometry->width <= 0 || geometry->height <= 0) {
      *geometry = (struct wlr_box){0, 0, 1, 1};
    }
  } else {
    *geometry = (struct wlr_box){0, 0, 1, 1};
  }
  hikari_view_refresh_geometry(view, geometry);

  const char *app_id = get_app_id(xdg_view);
#if !defined(NDEBUG)
  printf("APP ID %s\n", app_id);
#endif

  struct hikari_view_config *view_config =
      hikari_configuration_resolve_view_config(hikari_configuration, app_id);

  struct wlr_xdg_toplevel *xdg_toplevel = xdg_surface->toplevel;

  hikari_view_set_title(view, xdg_toplevel->title);
  hikari_view_configure(view, app_id, view_config);
}

static struct wlr_surface *
surface_at(
    struct hikari_node *node, double ox, double oy, double *sx, double *sy)
{
  struct hikari_xdg_view *xdg_view = (struct hikari_xdg_view *)node;

  struct wlr_box *geometry = hikari_view_geometry(&xdg_view->view);

  return wlr_xdg_surface_surface_at(xdg_view->surface, ox - geometry->x, oy - geometry->y, sx, sy);
}

static void
map(struct hikari_view *view, bool focus)
{
#if !defined(NDEBUG)
  printf("XDG MAP %p\n", view);
#endif

  struct hikari_xdg_view *xdg_view = (struct hikari_xdg_view *)view;
  struct wlr_xdg_surface *xdg_surface = xdg_view->surface;

  xdg_view->set_title.notify = set_title_handler;
  wl_signal_add(
      &xdg_view->xdg_toplevel->events.set_title, &xdg_view->set_title);

  xdg_view->new_popup.notify = new_popup_handler;
  wl_signal_add(&xdg_surface->events.new_popup, &xdg_view->new_popup);

  // [COMMENT] Action purpose: commit listener is now registered in hikari_xdg_view_init
// (at new_toplevel time) to catch initial_commit before map.

  hikari_view_map(view, xdg_surface->surface);
}

/* [COMMENT] Function purpose: Listener for the wlr_surface map event; runs
first_map while the view is still unmanaged, then maps it. */
static void
map_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xdg_view *xdg_view = wl_container_of(listener, xdg_view, map);

  struct hikari_view *view = (struct hikari_view *)xdg_view;

  /* [COMMENT] Action purpose: Configure the view on its first map. Focus is
  resolved from the view configuration inside hikari_view_map() -- the removed
  focus out-parameter was never written and is intentionally gone. */
  if (hikari_view_is_unmanaged(view)) {
    first_map(xdg_view);
  }

  map(view, false);

  // [COMMENT] Action purpose: Reconcile hikari's full-maximize state with the
  // toplevel's currently requested fullscreen state whenever they disagree
  // after mapping -- covers both a pre-map request on a brand new view
  // (request_fullscreen_handler only becomes observable once the surface is
  // initialized) and a stale request left over from before an already
  // managed view was unmapped and is now being remapped. This must run after
  // map() / hikari_view_map() so the view is mapped by the time
  // apply_requested_fullscreen() drives the full-maximize toggle, which
  // otherwise no-ops on an unmapped view.
  if (xdg_view->xdg_toplevel->requested.fullscreen !=
      hikari_view_is_fully_maximized(view)) {
    apply_requested_fullscreen(xdg_view);
  }
}

static void
unmap(struct hikari_view *view)
{
#if !defined(NDEBUG)
  printf("XDG UNMAP %p\n", view);
#endif

  struct hikari_xdg_view *xdg_view = (struct hikari_xdg_view *)view;

  assert(xdg_view->surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

  hikari_view_unmap(view);

  wl_list_remove(&xdg_view->set_title.link);
  wl_list_remove(&xdg_view->new_popup.link);
  // [COMMENT] Action purpose: commit listener is removed in destroy_handler since it
// lives for the full surface lifetime (registered at new_toplevel).
}

static void
unmap_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xdg_view *xdg_view = wl_container_of(listener, xdg_view, unmap);

  unmap((struct hikari_view *)xdg_view);
}

static void
activate(struct hikari_view *view, bool active)
{
  struct hikari_xdg_view *xdg_view = (struct hikari_xdg_view *)view;

  // [COMMENT] Action purpose: Guard against calling configure (via set_activated)
  // before the initial commit has fully initialized the surface.
  if (xdg_view->surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && xdg_view->surface->initialized) {
    wlr_xdg_toplevel_set_activated(xdg_view->xdg_toplevel, active);

    hikari_view_damage_whole(view);
  }
}

static uint32_t
resize(struct hikari_view *view, int width, int height)
{
  struct hikari_xdg_view *xdg_view = (struct hikari_xdg_view *)view;

  // [COMMENT] Action purpose: Guard against calling configure (via set_size)
  // before the initial commit has fully initialized the surface. Return 0 to
  // defer the resize until safe.
  if (xdg_view->surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && xdg_view->surface->initialized) {
    return wlr_xdg_toplevel_set_size(xdg_view->xdg_toplevel, width, height);
  }

  return 0;
}

static void
quit(struct hikari_view *view)
{
  struct hikari_xdg_view *xdg_view = (struct hikari_xdg_view *)view;

  wlr_xdg_toplevel_send_close(xdg_view->xdg_toplevel);
}

static void
destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xdg_view *xdg_view =
      wl_container_of(listener, xdg_view, destroy);

  struct hikari_view *view = (struct hikari_view *)xdg_view;

  if (hikari_view_is_mapped(view)) {
    unmap(view);
  }

  wl_list_remove(&xdg_view->map.link);
  wl_list_remove(&xdg_view->unmap.link);
  wl_list_remove(&xdg_view->commit.link);
  wl_list_remove(&xdg_view->request_fullscreen.link);
  wl_list_remove(&xdg_view->destroy.link);

  hikari_view_fini(view);

  /* [COMMENT] Action purpose: Destroy the hikari-owned parent tree, which also
  destroys the border and indicator rects parented to it. wlroots has already
  destroyed surface_tree from its own xdg_surface destroy listener by this
  point, so only hikari's own nodes remain to clean up. Without this the tree
  and its rects leak for every window that is ever opened. */
  if (xdg_view->scene_tree != NULL) {
    wlr_scene_node_destroy(&xdg_view->scene_tree->node);
    xdg_view->scene_tree = NULL;
    xdg_view->surface_tree = NULL;
    view->scene_node = NULL;
  }

  hikari_free(xdg_view);
}

static void
focus(struct hikari_node *node)
{
  struct hikari_view *view = (struct hikari_view *)node;

  hikari_workspace_focus_view(view->sheet->workspace, view);
}

static void
for_each_surface(struct hikari_node *node,
    void (*func)(struct wlr_surface *, int, int, void *),
    void *data)
{
  struct hikari_xdg_view *xdg_view = (struct hikari_xdg_view *)node;

  wlr_xdg_surface_for_each_surface(xdg_view->surface, func, data);
}

static void
destroy_popup_handler(struct wl_listener *listener, void *data)
{
#if !defined(NDEBUG)
  printf("DESTROY POPUP\n");
#endif
  struct hikari_xdg_popup *popup = wl_container_of(listener, popup, destroy);

  hikari_view_child_fini(&popup->view_child);

  wl_list_remove(&popup->destroy.link);
  wl_list_remove(&popup->commit.link);
  wl_list_remove(&popup->unmap.link);
  wl_list_remove(&popup->map.link);
  wl_list_remove(&popup->new_popup.link);

  hikari_free(popup);
}

// [COMMENT] Function purpose: Handle wlroots 0.20 popup initial_commit lifecycle.
// When an xdg_popup performs its first commit, the compositor must reply
// with a configure so the popup can map. See tinywl xdg_popup_commit.
static void
popup_commit_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xdg_popup *popup =
      wl_container_of(listener, popup, commit);

  // [COMMENT] Action purpose: Only handle the initial commit; subsequent commits
// are managed by the scene graph automatically.
  if (popup->popup->base->initial_commit) {
    wlr_xdg_surface_schedule_configure(popup->popup->base);
  }
}

static void
xdg_popup_create(struct wlr_xdg_popup *wlr_popup, struct hikari_view *parent);

static void
new_popup_popup_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xdg_popup *xdg_popup =
      wl_container_of(listener, xdg_popup, new_popup);

  struct wlr_xdg_popup *wlr_popup = data;

  xdg_popup_create(wlr_popup, xdg_popup->view_child.parent);
}

static void
new_popup_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xdg_view *xdg_view =
      wl_container_of(listener, xdg_view, new_popup);

  struct wlr_xdg_popup *wlr_popup = data;

  xdg_popup_create(wlr_popup, &xdg_view->view);
}

static void
popup_map(struct wl_listener *listener, void *data)
{
#if !defined(NDEBUG)
  printf("POPUP MAP\n");
#endif

  struct hikari_xdg_popup *xdg_popup =
      wl_container_of(listener, xdg_popup, map);

  struct hikari_view *parent = xdg_popup->view_child.parent;

  hikari_view_damage_surface(parent, xdg_popup->view_child.surface, true);
}

static void
popup_unmap(struct wl_listener *listener, void *data)
{
#if !defined(NDEBUG)
  printf("POPUP UNMAP\n");
#endif

  struct hikari_xdg_popup *xdg_popup =
      wl_container_of(listener, xdg_popup, unmap);

  struct hikari_view *parent = xdg_popup->view_child.parent;

  hikari_view_damage_surface(parent, xdg_popup->view_child.surface, true);
}

static void
popup_unconstrain(struct hikari_xdg_popup *popup)
{
  struct hikari_view *view = popup->view_child.parent;
  struct wlr_xdg_popup *wlr_popup = popup->popup;

  struct hikari_output *output = view->output;

  struct wlr_box *geometry = hikari_view_geometry(view);

  struct wlr_box output_toplevel_sx_box = {
    .x = -geometry->x,
    .y = -geometry->y,
    .width = output->geometry.width,
    .height = output->geometry.height,
  };

  wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_toplevel_sx_box);
}

static void
xdg_popup_create(struct wlr_xdg_popup *wlr_popup, struct hikari_view *parent)
{
  struct hikari_xdg_popup *popup =
      hikari_malloc(sizeof(struct hikari_xdg_popup));

#if !defined(NDEBUG)
  printf("CREATE POPUP\n");
#endif

  popup->view_child.parent = parent;
  popup->popup = wlr_popup;

  wlr_popup->base->surface->data = parent;

  popup->destroy.notify = destroy_popup_handler;
  wl_signal_add(&wlr_popup->base->surface->events.destroy, &popup->destroy);

  popup->new_popup.notify = new_popup_popup_handler;
  wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);

  // [COMMENT] Action purpose: Register commit listener on popup surface to handle
// initial_commit — wlroots 0.20 requires the compositor to respond with a
// configure so the popup can map. See tinywl xdg_popup_commit.
  popup->commit.notify = popup_commit_handler;
  wl_signal_add(&wlr_popup->base->surface->events.commit, &popup->commit);

  popup->map.notify = popup_map;
  wl_signal_add(&wlr_popup->base->surface->events.map, &popup->map);

  popup->unmap.notify = popup_unmap;
  wl_signal_add(&wlr_popup->base->surface->events.unmap, &popup->unmap);

  hikari_view_child_init(
      (struct hikari_view_child *)popup, parent, wlr_popup->base->surface);

  popup_unconstrain(popup);
}

// [COMMENT] Function purpose: Apply the toplevel's currently requested
// fullscreen state, both acking the protocol request and driving hikari's
// own full-maximize state to match. Shared by request_fullscreen_handler
// (subsequent requests) and first_map (a request made before the initial
// commit, which is only observable once the surface is initialized).
static void
apply_requested_fullscreen(struct hikari_xdg_view *xdg_view)
{
  struct hikari_view *view = (struct hikari_view *)xdg_view;

  // [COMMENT] Action purpose: Guard against calling configure before the surface
// is initialized (initial_commit not yet handled). See tinywl request_fullscreen.
  if (xdg_view->surface->initialized) {
    bool fullscreen = xdg_view->xdg_toplevel->requested.fullscreen;

    // [COMMENT] Action purpose: Apply the client's requested fullscreen state via wlroots API.
    wlr_xdg_toplevel_set_fullscreen(xdg_view->xdg_toplevel, fullscreen);

    // [COMMENT] Action purpose: Acking the protocol request alone leaves the
    // view at its old geometry -- the client renders assuming it now fills
    // the output while hikari never resized it. Drive hikari's own
    // full-maximize state to match, using the same toggle the normal
    // fullscreen keybinding uses, guarded so this is a no-op when the view is
    // already in the requested state or a resize is already in flight.
    if (hikari_view_is_mapped(view) && !hikari_view_is_hidden(view) &&
        !hikari_view_is_dirty(view) &&
        fullscreen != hikari_view_is_fully_maximized(view)) {
      hikari_view_toggle_full_maximize(view);
    }
  }
}

static void
request_fullscreen_handler(struct wl_listener *listener, void *data)
{
  struct hikari_xdg_view *xdg_view =
      wl_container_of(listener, xdg_view, request_fullscreen);

  apply_requested_fullscreen(xdg_view);
}

static void
constraints(struct hikari_view *view,
    int *min_width,
    int *min_height,
    int *max_width,
    int *max_height)
{
  struct hikari_xdg_view *xdg_view = (struct hikari_xdg_view *)view;
  struct wlr_xdg_toplevel_state *state = &xdg_view->surface->toplevel->current;

  *min_width = state->min_width > 0 ? state->min_width : 0;
  *min_height = state->min_height > 0 ? state->min_height : 0;
  *max_width =
      state->max_width > 0 ? state->max_width : view->output->geometry.width;
  *max_height =
      state->max_height > 0 ? state->max_height : view->output->geometry.height;
}

// [COMMENT] Function purpose: Initialize an XDG view, linking listeners to surface events (map, unmap, destroy) and constructing scene tree nodes.
void
hikari_xdg_view_init(struct hikari_xdg_view *xdg_view,
    struct wlr_xdg_surface *xdg_surface,
    struct hikari_workspace *workspace)
{
  assert(xdg_surface->toplevel != NULL);

  bool child = xdg_surface->toplevel->parent != NULL;

  hikari_view_init(&xdg_view->view, child, workspace);

  if (xdg_surface->surface->mapped) {
    struct wlr_box new_geometry = xdg_surface->geometry;
    if (new_geometry.width > 0 && new_geometry.height > 0) {
      xdg_view->view.geometry = new_geometry;
    } else {
      xdg_view->view.geometry = (struct wlr_box){0, 0, 1, 1};
    }
  } else {
    xdg_view->view.geometry = (struct wlr_box){0, 0, 1, 1};
  }

#if !defined(NDEBUG)
  printf("NEW XDG %p\n", xdg_view);
#endif

  xdg_view->view.node.surface_at = surface_at;

  // [COMMENT] Action purpose: wlr_xdg_surface_ping was removed here — in wlroots 0.20,
// the XDG surface is not yet initialized (surface->initialized == false) at
// the new_toplevel signal. Calling ping triggers schedule_configure, which
// asserts initialized. The wlroots xdg_shell module handles pings internally
// after the initial commit. See wlr_xdg_surface.c line 168.

  xdg_view->surface = xdg_surface;
  xdg_view->xdg_toplevel = xdg_surface->toplevel;

  /* [COMMENT] Action purpose: Create a hikari-owned parent tree first. wlroots
  installs its own xdg_surface destroy listener inside
  wlr_scene_xdg_surface_create (see wlroots types/scene/xdg_shell.c) that
  destroys the tree it returns -- along with every child node. Parenting
  hikari's border and indicator rects into that tree would hand their lifetime
  to wlroots and leave hikari holding dangling pointers. Owning the parent
  ourselves keeps hikari's nodes under hikari's control, matching the structure
  hikari_xwayland_view_init already uses. */
  xdg_view->scene_tree = wlr_scene_tree_create(&hikari_server.scene->tree);
  if (xdg_view->scene_tree == NULL) {
    fprintf(stderr, "error: could not create scene tree for xdg view\n");
    hikari_view_fini(&xdg_view->view);
    hikari_free(xdg_view);
    return;
  }

  /* [COMMENT] Action purpose: Attach the wlroots-managed surface tree beneath
  the hikari-owned parent. wlroots keeps this subtree positioned against the
  surface's own geometry offset and tears it down on xdg_surface destroy. */
  xdg_view->surface_tree =
      wlr_scene_xdg_surface_create(xdg_view->scene_tree, xdg_surface);
  if (xdg_view->surface_tree == NULL) {
    fprintf(stderr, "error: could not create scene surface for xdg view\n");
    wlr_scene_node_destroy(&xdg_view->scene_tree->node);
    hikari_view_fini(&xdg_view->view);
    hikari_free(xdg_view);
    return;
  }

  xdg_view->view.scene_node = &xdg_view->scene_tree->node;
  xdg_view->scene_tree->node.data = xdg_view;
  // [COMMENT] Action purpose: Store the hikari-owned scene_tree (NOT the wlroots
  // surface_tree) in xdg_surface->data. server_decoration_handler resolves a
  // decoration back to its view via xdg_surface->data->node.data, and only
  // scene_tree carries that back-reference; pointing this at surface_tree would
  // hand that lookup a tree whose node.data was never set.
  xdg_surface->data = xdg_view->scene_tree;
  hikari_border_init(&xdg_view->view.border, xdg_view->scene_tree);
  hikari_indicator_frame_init(&xdg_view->view.indicator_frame, xdg_view->scene_tree);

  xdg_view->map.notify = map_handler;
  wl_signal_add(&xdg_surface->surface->events.map, &xdg_view->map);

  xdg_view->unmap.notify = unmap_handler;
  wl_signal_add(&xdg_surface->surface->events.unmap, &xdg_view->unmap);

  // [COMMENT] Action purpose: Register commit listener at new_toplevel time (not map
// time) so that the initial_commit is caught before map. This is the
// wlroots 0.20 lifecycle pattern — see tinywl server_new_xdg_toplevel.
  xdg_view->commit.notify = commit_handler;
  wl_signal_add(&xdg_surface->surface->events.commit, &xdg_view->commit);

  // [COMMENT] Action purpose: Register the fullscreen request listener at
  // new_toplevel time (not map time) so that a client requesting fullscreen
  // before its first map is not silently dropped.
  xdg_view->request_fullscreen.notify = request_fullscreen_handler;
  wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen,
      &xdg_view->request_fullscreen);

  xdg_view->destroy.notify = destroy_handler;
  wl_signal_add(&xdg_surface->events.destroy, &xdg_view->destroy);

  assert(xdg_view->surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

  xdg_view->view.node.focus = focus;
  xdg_view->view.node.for_each_surface = for_each_surface;
  xdg_view->view.activate = activate;
  xdg_view->view.resize = resize;
  xdg_view->view.quit = quit;
  xdg_view->view.constraints = constraints;
#ifdef HAVE_XWAYLAND
  xdg_view->view.move = NULL;
  xdg_view->view.move_resize = NULL;
#endif
}
