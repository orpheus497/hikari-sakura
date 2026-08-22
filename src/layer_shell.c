#ifdef HAVE_LAYERSHELL
#include <hikari/layer_shell.h>

#ifndef NDEBUG
#include <stdio.h>
#endif

#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/server.h>

static void
map(struct hikari_layer *layer);

static void
map_handler(struct wl_listener *listener, void *data);

static void
unmap(struct hikari_layer *layer);

static void
unmap_handler(struct wl_listener *listener, void *data);

static void
commit_handler(struct wl_listener *listener, void *data);

static void
destroy_handler(struct wl_listener *listener, void *data);

static void
new_popup_handler(struct wl_listener *listener, void *data);

static void
for_each_surface(struct hikari_node *node,
    void (*func)(struct wlr_surface *, int, int, void *),
    void *data);

static struct wlr_surface *
surface_at(
    struct hikari_node *node, double ox, double oy, double *sx, double *sy);

static void
focus(struct hikari_node *node);

static void
arrange_layers(struct hikari_output *output);

static bool
init_layer_popup(struct hikari_layer_popup *layer_popup,
    struct hikari_layer *parent,
    struct wlr_xdg_popup *popup);

static bool
init_popup_popup(struct hikari_layer_popup *layer_popup,
    struct hikari_layer_popup *parent,
    struct wlr_xdg_popup *popup);

static void
fini_popup(struct hikari_layer_popup *layer_popup);

static void
commit_popup_handler(struct wl_listener *listener, void *data);

static void
destroy_popup_handler(struct wl_listener *listener, void *data);

static void
map_popup_handler(struct wl_listener *listener, void *data);

static void
unmap_popup_handler(struct wl_listener *listener, void *data);

static void
destroy_popup_handler(struct wl_listener *listener, void *data);

static void
new_popup_popup_handler(struct wl_listener *listener, void *data);

static struct hikari_layer *
get_layer(struct hikari_layer_popup *popup);

/* [COMMENT] Function purpose: Arrange all layer surfaces for a given output
using wlr_scene_layer_surface_v1_configure(), which handles geometry
computation, scene node positioning, wlr_layer_surface_v1_configure() dispatch,
and exclusive zone tracking in a single correct call. Updates
output->usable_area for views. Must only be called AFTER the surface's
initial_commit (initialized == true). */
static void
arrange_layers(struct hikari_output *output)
{
  assert(output != NULL);

  struct wlr_box full_area = { .x = 0,
    .y = 0,
    .width = output->geometry.width,
    .height = output->geometry.height };

  /* [COMMENT] Action purpose: usable_area starts as the full output area and
  is progressively shrunk by exclusive-zone surfaces as we iterate. The result
  is stored in output->usable_area for use by the view layout engine. */
  struct wlr_box usable_area = full_area;

  /* [COMMENT] Action purpose: Reserve the native top bar's strip first. This
  pass re-derives usable_area from the full output box, so without repeating the
  reservation here every layer-shell arrangement would silently hand the bar's
  rows back to views. Matches output_geometry()'s baseline. */
  hikari_bar_reserve(&output->bar, &usable_area);

  /* [COMMENT] Action purpose: Process layers in protocol-defined order.
  BACKGROUND and BOTTOM layers are processed first so their exclusive zones
  shrink usable_area before TOP and OVERLAY surfaces are configured. */
  static const enum zwlr_layer_shell_v1_layer layer_order[] = {
    ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
    ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
  };

  for (size_t i = 0; i < sizeof(layer_order) / sizeof(layer_order[0]); i++) {
    struct hikari_layer *layer;
    wl_list_for_each (layer, &output->layers[layer_order[i]], layer_surfaces) {
      if (layer->scene_layer_surface == NULL) {
        continue;
      }

      struct wlr_box old_geometry = layer->geometry;

      /* [COMMENT] Action purpose: wlr_scene_layer_surface_v1_configure() reads
      from layer_surface->current (the committed state, set by wlroots after the
      initial commit). It computes geometry from anchor/margin/desired_size,
      positions the scene node in output-local coordinates, calls
      wlr_layer_surface_v1_configure() to send the configure event to the
      client, and updates usable_area if the surface has a positive
      exclusive_zone. */
      wlr_scene_layer_surface_v1_configure(
          layer->scene_layer_surface, &full_area, &usable_area);

      /* [COMMENT] Action purpose: After the scene configure call, derive the
      output-local geometry from the scene node's layout-global position.
      wlr_scene_node_coords() returns layout-global coordinates; subtract the
      output's layout origin to get output-local. */
      int nx = 0, ny = 0;
      wlr_scene_node_coords(&layer->scene_layer_surface->tree->node, &nx, &ny);
      layer->geometry.x = nx - output->geometry.x;
      layer->geometry.y = ny - output->geometry.y;

      /* [COMMENT] Action purpose: actual_width/height are populated in the
      layer surface state after the client acks the configure. On the very first
      configure they are still zero; fall back to desired size for the initial
      geometry tracking so popup_unconstrain has valid dimensions. */
      struct wlr_layer_surface_v1_state *state =
          &layer->scene_layer_surface->layer_surface->current;
      layer->geometry.width = state->actual_width > 0
                                  ? (int)state->actual_width
                                  : (int)state->desired_width;
      layer->geometry.height = state->actual_height > 0
                                   ? (int)state->actual_height
                                   : (int)state->desired_height;

      /* [COMMENT] Action purpose: Damage the old and new geometry rectangles
      when a mapped surface has moved or resized so the scene repaint fires. */
      bool geo_changed =
          memcmp(&old_geometry, &layer->geometry, sizeof(struct wlr_box)) != 0;
      if (geo_changed && layer->mapped && output->enabled) {
        hikari_output_add_damage(output, &old_geometry);
        hikari_output_add_damage(output, &layer->geometry);
      }
    }
  }

  output->usable_area = usable_area;
}

/* [COMMENT] Function purpose: Initialise a hikari_layer for a new
wlr_layer_surface -- resolve the output, attach the surface to the scene
graph with per-layer stacking order, and register lifecycle listeners.
NOTE: arrange_layers() is NOT called here; it must only be called after
the client performs its initial_commit (surface->initialized == true).
wlr_layer_surface_v1_configure() asserts surface->initialized, so calling
arrange_layers() before the first commit is a hard assert crash in wlroots 0.20.
*/
void
hikari_layer_init(
    struct hikari_layer *layer, struct wlr_layer_surface_v1 *wlr_layer_surface)
{
#ifndef NDEBUG
  printf("LAYER INIT %p\n", layer);
#endif

  struct hikari_output *output = wlr_layer_surface->output != NULL
                                     ? wlr_layer_surface->output->data
                                     : hikari_server.workspace->output;

  layer->node.surface_at = surface_at;
  layer->node.focus = focus;
  layer->node.for_each_surface = for_each_surface;
  layer->output = output;
  layer->layer = wlr_layer_surface->pending.layer;
  layer->surface = wlr_layer_surface;
  layer->mapped = false;
  layer->configured = false;
  layer->geometry = (struct wlr_box){ 0 };
  layer->desired_width = 0;
  layer->desired_height = 0;
  layer->anchor = 0;
  layer->margin.top = 0;
  layer->margin.right = 0;
  layer->margin.bottom = 0;
  layer->margin.left = 0;

  wlr_layer_surface->output = output->wlr_output;

  /* [COMMENT] Action purpose: Attach the layer surface to the scene graph.
  wlr_scene_layer_surface_v1_create() creates a sub-tree and subsurface tree
  for this layer surface; all rendering, subsurface parenting, and popup
  attachment is managed by wlroots from this point. */
  layer->scene_layer_surface = wlr_scene_layer_surface_v1_create(
      &hikari_server.scene->tree, wlr_layer_surface);

  /* [COMMENT] Action purpose: Scene tree allocation only fails on OOM. Reject
  the client surface and bail out before listeners are registered; the destroy
  signal then has no hikari-side state to clean up. */
  if (layer->scene_layer_surface == NULL) {
    fprintf(stderr, "error: could not create scene node for layer surface\n");
    hikari_free(layer);
    wlr_layer_surface_v1_destroy(wlr_layer_surface);
    return;
  }

  /* [COMMENT] Action purpose: Establish stacking order per layer: overlay/top
  render above views, bottom/background below views. The node starts disabled;
  map() enables it once the surface has content. */
  struct wlr_scene_node *scene_node = &layer->scene_layer_surface->tree->node;
  if (layer->layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY ||
      layer->layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
    wlr_scene_node_raise_to_top(scene_node);
  } else {
    wlr_scene_node_lower_to_bottom(scene_node);
  }
  wlr_scene_node_set_enabled(scene_node, false);

  layer->commit.notify = commit_handler;
  wl_signal_add(&wlr_layer_surface->surface->events.commit, &layer->commit);

  /* [COMMENT] Action purpose: Listen on the LAYER SURFACE's destroy signal, not
  the underlying wl_surface's. These are distinct objects torn down at distinct
  times: a client destroys the layer-surface role object first and the
  wl_surface afterwards. wlroots registers its own listener on
  layer_surface->events.destroy (wlroots-0.20.0/types/scene/layer_shell_v1.c)
  which destroys the scene tree AND frees the wlr_scene_layer_surface_v1 struct
  itself. Listening on the wl_surface instead meant hikari kept running with a
  freed scene_layer_surface and a freed layer->surface -- arrange_layers() would
  walk output->layers[] and configure through the dangling pointer, and
  hikari_layer_fini() would later destroy an already-freed scene node. That
  heap corruption is what crashed the compositor on every layer-shell teardown.
  Sharing the signal with wlroots keeps both teardowns in one defined
  ordering. */
  layer->destroy.notify = destroy_handler;
  wl_signal_add(&wlr_layer_surface->events.destroy, &layer->destroy);

  layer->map.notify = map_handler;
  wl_signal_add(&wlr_layer_surface->surface->events.map, &layer->map);

  wl_list_insert(&output->layers[layer->layer], &layer->layer_surfaces);

  /* [COMMENT] Action purpose: DO NOT call arrange_layers() here.
  wlr_layer_surface_v1_configure() (called inside arrange_layers via
  wlr_scene_layer_surface_v1_configure) asserts surface->initialized, which
  is only set true on the first wl_surface.commit from the client. Calling
  arrange before that first commit fires a hard assert in wlroots 0.20.
  commit_handler will call arrange_layers() on the initial_commit. */
}

/* [COMMENT] Function purpose: Tear down a layer surface -- detach its scene
node, unlink it from the output's layer list, and remove all listeners.
Called once the surface is unmapped (destroy path). */
void
hikari_layer_fini(struct hikari_layer *layer)
{
  assert(!layer->mapped);

  /* [COMMENT] Action purpose: Drop the reference WITHOUT destroying the tree.
  hikari_layer_fini now runs off layer_surface->events.destroy, the same signal
  wlroots uses to destroy this scene tree and free the enclosing
  wlr_scene_layer_surface_v1. Calling wlr_scene_node_destroy here would either
  double-destroy the tree or operate on already-freed memory depending on
  listener order. Nulling the pointer is all hikari needs to do; wlroots owns
  the teardown. */
  layer->scene_layer_surface = NULL;

  wl_list_remove(&layer->layer_surfaces);

  wl_list_remove(&layer->commit.link);
  wl_list_remove(&layer->destroy.link);
  wl_list_remove(&layer->map.link);
}

static void
popup_unconstrain(struct hikari_layer_popup *layer_popup)
{
  struct hikari_layer *layer = get_layer(layer_popup);
  /* Action purpose: get_layer returns NULL only when the depth guard fires
  (a compositor-side cycle bug); bail out gracefully rather than dereferencing
  a NULL output pointer. */
  if (layer == NULL) {
    return;
  }
  struct hikari_output *output = layer->output;

  /* [COMMENT] Action purpose: Provide the popup with a constraint box in
  surface-local coordinates (origin = negative of the layer's output-local
  position, size = full output). This gives the popup maximum freedom to
  position itself without going off-screen. */
  struct wlr_box box = { .x = -layer->geometry.x,
    .y = -layer->geometry.y,
    .width = output->geometry.width,
    .height = output->geometry.height };

  wlr_xdg_popup_unconstrain_from_box(layer_popup->popup, &box);
}

// Function purpose: Wire up the listeners shared by every layer-shell popup
// (parented directly to a layer surface or nested under another popup).
// Shared by init_layer_popup and init_popup_popup so both parent kinds set up
// identically. Unconstraining is driven by commit_popup_handler on the popup's
// initial commit, not from here.
static bool
init_popup(
    struct hikari_layer_popup *layer_popup, struct wlr_xdg_popup *wlr_popup)
{
  layer_popup->popup = wlr_popup;

  /* [COMMENT] Action purpose: Zero the cached geometry before anything can read
  it. hikari_try_malloc does not zero, and damage_popup() reads this field to
  damage the popup's previous position before writing its new one. */
  layer_popup->geometry = (struct wlr_box){ 0 };

  /* [COMMENT] Action purpose: Give the popup its own scene tree, parented to
  the owning layer surface's tree (or to the parent popup's tree when nested).

  This is REQUIRED and was previously missing, exactly as in xdg_view.c's
  popup path. wlr_scene_layer_surface_v1_create() covers the layer surface and
  its SUBSURFACES only; nothing in wlroots walks an xdg_surface's POPUPS (see
  types/scene/xdg_shell.c). Without this every layer-shell popup -- a panel's
  context menu, a bar's dropdown, waybar sub-menus -- had no scene node and
  never rendered. layer_popup->parent is already populated by the callers
  below, so it is safe to resolve here. */
  struct wlr_scene_tree *parent_tree = NULL;

  switch (layer_popup->parent.type) {
    case HIKARI_LAYER_NODE_TYPE_LAYER: {
      struct hikari_layer *parent_layer = layer_popup->parent.node.layer;
      if (parent_layer->scene_layer_surface != NULL) {
        parent_tree = parent_layer->scene_layer_surface->tree;
      }
      break;
    }

    case HIKARI_LAYER_NODE_TYPE_POPUP:
      parent_tree = layer_popup->parent.node.popup->scene_tree;
      break;
  }

  if (parent_tree == NULL) {
    wlr_log(WLR_ERROR,
        "init_popup: layer popup parent has no scene tree, popup will not be "
        "shown");
    return false;
  }

  layer_popup->scene_tree =
      wlr_scene_xdg_surface_create(parent_tree, wlr_popup->base);

  if (layer_popup->scene_tree == NULL) {
    wlr_log(WLR_ERROR, "init_popup: could not create layer popup scene tree");
    return false;
  }

  layer_popup->commit.notify = commit_popup_handler;
  wl_signal_add(&wlr_popup->base->surface->events.commit, &layer_popup->commit);

  layer_popup->map.notify = map_popup_handler;
  wl_signal_add(&wlr_popup->base->surface->events.map, &layer_popup->map);

  layer_popup->unmap.notify = unmap_popup_handler;
  wl_signal_add(&wlr_popup->base->surface->events.unmap, &layer_popup->unmap);

  layer_popup->destroy.notify = destroy_popup_handler;
  wl_signal_add(&wlr_popup->events.destroy, &layer_popup->destroy);

  layer_popup->new_popup.notify = new_popup_popup_handler;
  wl_signal_add(&wlr_popup->base->events.new_popup, &layer_popup->new_popup);

  /* [COMMENT] Action purpose: Unconstraining deliberately does NOT happen here.
  It is deferred to commit_popup_handler's initial_commit branch, because the
  popup surface is not yet initialized at new_popup time and wlroots asserts on
  that. See the full explanation there. */

  return true;
}

static struct hikari_layer *
get_layer(struct hikari_layer_popup *layer_popup)
{
  struct hikari_layer_popup *current = layer_popup;

  /* Action purpose: Guard against an infinite loop if popup parent links ever
  form a cycle (a compositor bug, not a client bug). Depth > 64 is impossible
  in any real UI stack; hitting the limit indicates corrupted parent pointers.
*/
  int depth = 0;
  const int MAX_POPUP_DEPTH = 64;

  for (;;) {
    if (++depth > MAX_POPUP_DEPTH) {
      wlr_log(WLR_ERROR,
          "get_layer: popup parent chain exceeded depth limit -- aborting "
          "walk");
      return NULL;
    }
    switch (current->parent.type) {
      case HIKARI_LAYER_NODE_TYPE_LAYER:
        return current->parent.node.layer;

      case HIKARI_LAYER_NODE_TYPE_POPUP:
        current = current->parent.node.popup;
        break;
    }
  }
}

static void
damage(struct hikari_layer *layer, bool whole)
{
  struct wlr_surface *surface = layer->surface->surface;

  if (whole) {
    struct wlr_box geometry = { .x = layer->geometry.x,
      .y = layer->geometry.y,
      .width = surface->current.width,
      .height = surface->current.height };

    hikari_output_add_damage(layer->output, &geometry);
  } else {
    hikari_output_add_effective_surface_damage(
        layer->output, surface, layer->geometry.x, layer->geometry.y);
  }
}

/* [COMMENT] Function purpose: Emit damage for a (possibly nested) layer
popup -- accumulate the popup chain offsets plus the owning layer's position
into output-space coordinates, then damage the popup surface. */
static void
damage_popup(struct hikari_layer_popup *layer_popup, bool whole)
{
  struct wlr_xdg_popup *popup = layer_popup->popup;
  struct wlr_surface *surface = popup->base->surface;

  /* [COMMENT] Action purpose: Compute the popup origin relative to the parent
  surface's window geometry. wlroots 0.20 moved the flat wlr_xdg_popup.geometry
  field into the popup state struct (popup->current.geometry). */
  int ox = popup->current.geometry.x - popup->base->geometry.x;
  int oy = popup->current.geometry.y - popup->base->geometry.y;

  struct hikari_layer *layer;
  struct hikari_layer_popup *current = layer_popup;

  /* Action purpose: Guard against an infinite loop if popup parent links ever
  form a cycle. Mirrors the guard in get_layer. */
  int depth = 0;
  const int MAX_POPUP_DEPTH = 64;

  for (;;) {
    if (++depth > MAX_POPUP_DEPTH) {
      wlr_log(WLR_ERROR,
          "damage_popup: popup parent chain exceeded depth limit -- aborting "
          "walk");
      return;
    }
    switch (current->parent.type) {
      case HIKARI_LAYER_NODE_TYPE_LAYER:
        layer = current->parent.node.layer;
        ox += layer->geometry.x;
        oy += layer->geometry.y;
        goto done;

      case HIKARI_LAYER_NODE_TYPE_POPUP:
        current = current->parent.node.popup;
        /* [COMMENT] Action purpose: Accumulate nested popup offsets via the
        0.20 popup-state geometry field. */
        ox += current->popup->current.geometry.x -
              current->popup->base->geometry.x;
        oy += current->popup->current.geometry.y -
              current->popup->base->geometry.y;
        break;
    }
  }

done:

  assert(layer != NULL);

  struct hikari_output *output = layer->output;

  // [COMMENT] Action purpose: Skip popup damage when output is disabled.
  if (!output->enabled) {
    return;
  }

  if (whole) {
    struct wlr_box geometry = { .x = ox,
      .y = oy,
      .width = surface->current.width,
      .height = surface->current.height };

    hikari_output_add_damage(output, &geometry);
  } else {
    if (layer_popup->geometry.width > 0 && layer_popup->geometry.height > 0) {
      hikari_output_add_damage(output, &layer_popup->geometry);
    }
    hikari_output_add_effective_surface_damage(layer->output, surface, ox, oy);
  }

  layer_popup->geometry = (struct wlr_box){ .x = ox,
    .y = oy,
    .width = surface->current.width,
    .height = surface->current.height };
}

/* [COMMENT] Function purpose: Handle wl_surface commit events for a layer
surface. On the initial_commit the compositor must respond with a configure
via arrange_layers(); on subsequent commits while unmapped, re-arrange in
case the client changed its desired size/anchor; while mapped, re-arrange
and damage if geometry changed. */
static void
commit_handler(struct wl_listener *listener, void *data)
{
  struct hikari_layer *layer = wl_container_of(listener, layer, commit);
  struct hikari_output *output = layer->output;

  /* [COMMENT] Action purpose: Handle the wlroots 0.20 initial_commit
  lifecycle for layer shell surfaces. The first commit sets
  surface->initialized = true inside wlroots before the commit signal fires,
  so arrange_layers() (which calls wlr_scene_layer_surface_v1_configure ->
  wlr_layer_surface_v1_configure, which asserts initialized) is safe here.
  The compositor MUST respond with a configure event so the client can
  proceed to attach a buffer and map. Without this response, layer clients
  hang indefinitely waiting for the configure reply. */
  if (!layer->configured) {
    layer->configured = true;
    arrange_layers(output);
    return;
  }

  if (!layer->mapped) {
    /* [COMMENT] Action purpose: Client committed again before mapping.
    Avoid infinite configure loops by only re-arranging if the desired size
    or anchor actually changed. */
    struct wlr_layer_surface_v1_state *state = &layer->surface->current;
    if (layer->desired_width != state->desired_width ||
        layer->desired_height != state->desired_height ||
        layer->anchor != state->anchor ||
        layer->margin.top != state->margin.top ||
        layer->margin.right != state->margin.right ||
        layer->margin.bottom != state->margin.bottom ||
        layer->margin.left != state->margin.left) {
      layer->desired_width = state->desired_width;
      layer->desired_height = state->desired_height;
      layer->anchor = state->anchor;
      layer->margin.top = state->margin.top;
      layer->margin.right = state->margin.right;
      layer->margin.bottom = state->margin.bottom;
      layer->margin.left = state->margin.left;
      arrange_layers(output);
    }
    return;
  }

  /* [COMMENT] Action purpose: Surface is mapped and committed a new state.
  Check whether the layer changed (client called set_layer) and move it to
  the correct list if so, adjusting scene z-order. Then re-arrange all layers
  so exclusive zones and positions are recalculated. */
  enum zwlr_layer_shell_v1_layer current_layer = layer->surface->current.layer;
  bool changed_layer = layer->layer != current_layer;

  if (changed_layer) {
    wl_list_remove(&layer->layer_surfaces);
    wl_list_insert(&output->layers[current_layer], &layer->layer_surfaces);
    layer->layer = current_layer;

    struct wlr_scene_node *scene_node = &layer->scene_layer_surface->tree->node;
    if (current_layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY ||
        current_layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
      wlr_scene_node_raise_to_top(scene_node);
    } else {
      wlr_scene_node_lower_to_bottom(scene_node);
    }
  }

  arrange_layers(output);
  hikari_server_cursor_focus();
}

static void
destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_layer *layer = wl_container_of(listener, layer, destroy);

#ifndef NDEBUG
  printf("LAYER DESTROY %p\n", layer);
#endif

  /* [COMMENT] Action purpose: Drop the scene reference FIRST. wlroots registers
  its own listener on this same destroy signal from inside
  wlr_scene_layer_surface_v1_create(), which runs before this one (wl_signal
  dispatches in registration order, and hikari registers afterwards in
  hikari_layer_init). By the time we get here the scene tree is destroyed and
  the wlr_scene_layer_surface_v1 struct has been freed, so this pointer is
  already dangling. Nulling it up front makes every `scene_layer_surface !=
  NULL` guard below -- in unmap(), damage(), and arrange_layers() -- actually
  protective instead of waving a freed pointer through. */
  layer->scene_layer_surface = NULL;

  if (layer->mapped) {
    unmap(layer);
  }

  assert(!layer->mapped);

  hikari_layer_fini(layer);
  hikari_free(layer);
}

/* [COMMENT] Function purpose: Map a layer surface -- register popup/unmap
listeners, enable the scene node now that content exists, and damage. */
static void
map(struct hikari_layer *layer)
{
#ifndef NDEBUG
  printf("LAYER MAP %p\n", layer);
#endif

  assert(!layer->mapped);

  struct wlr_layer_surface_v1 *wlr_layer_surface = layer->surface;

  layer->new_popup.notify = new_popup_handler;
  wl_signal_add(&wlr_layer_surface->events.new_popup, &layer->new_popup);

  layer->unmap.notify = unmap_handler;
  wl_signal_add(&wlr_layer_surface->surface->events.unmap, &layer->unmap);

  wl_list_remove(&layer->map.link);

  layer->mapped = true;

  /* [COMMENT] Action purpose: Make the scene node visible now that the surface
  has mapped content. */
  if (layer->scene_layer_surface != NULL) {
    wlr_scene_node_set_enabled(&layer->scene_layer_surface->tree->node, true);
  }

  damage(layer, true);

  hikari_server_cursor_focus();
}

static void
map_handler(struct wl_listener *listener, void *data)
{
  struct hikari_layer *layer = wl_container_of(listener, layer, map);

  map(layer);
}

/* [COMMENT] Function purpose: Unmap a layer surface -- disable the scene
node, release keyboard focus, recompute exclusive zones, and damage. */
static void
unmap(struct hikari_layer *layer)
{
#ifndef NDEBUG
  printf("LAYER UNMAP %p\n", layer);
#endif

  struct hikari_workspace *workspace = layer->output->workspace;

  assert(layer->mapped);

  wl_list_remove(&layer->layer_surfaces);
  wl_list_init(&layer->layer_surfaces);

  wl_list_remove(&layer->new_popup.link);
  wl_list_remove(&layer->unmap.link);

  /* [COMMENT] Action purpose: Re-arm the map listener so a surface that unmaps
  and maps again is picked up. Guarded because unmap() is also reached from the
  destroy path, where the layer surface (and its wl_surface) are being torn
  down and must not be signal-subscribed. The else branch re-initialises the
  link so hikari_layer_fini's unconditional wl_list_remove stays balanced in
  both cases. */
  if (layer->surface != NULL && layer->surface->surface != NULL) {
    layer->map.notify = map_handler;
    wl_signal_add(&layer->surface->surface->events.map, &layer->map);
  } else {
    wl_list_init(&layer->map.link);
  }

  layer->mapped = false;

  /* [COMMENT] Action purpose: Hide the scene node while unmapped so no stale
  buffer remains on screen. */
  if (layer->scene_layer_surface != NULL) {
    wlr_scene_node_set_enabled(&layer->scene_layer_surface->tree->node, false);
  }

  damage(layer, true);

  /* [COMMENT] Action purpose: Recalculate exclusive zones and usable area now
  that this surface is gone so views and remaining layers re-pack correctly. */
  arrange_layers(layer->output);

  if (layer == workspace->focus_layer) {
    struct wlr_seat *seat = hikari_server.seat;

    workspace->focus_layer = NULL;
    wlr_seat_keyboard_clear_focus(seat);
  }

  hikari_server_cursor_focus();
}

static void
unmap_handler(struct wl_listener *listener, void *data)
{
  struct hikari_layer *layer = wl_container_of(listener, layer, unmap);

  unmap(layer);
}

static void
destroy_popup_handler(struct wl_listener *listener, void *data)
{
  struct hikari_layer_popup *layer_popup =
      wl_container_of(listener, layer_popup, destroy);

#ifndef NDEBUG
  printf("DESTROY LAYER POPUP %p\n", layer_popup);
#endif

  fini_popup(layer_popup);

  hikari_free(layer_popup);
}

static void
map_popup_handler(struct wl_listener *listener, void *data)
{
  struct hikari_layer_popup *layer_popup =
      wl_container_of(listener, layer_popup, map);

#ifndef NDEBUG
  printf("MAP LAYER POPUP %p\n", layer_popup);
#endif

  damage_popup(layer_popup, true);
}

static void
unmap_popup_handler(struct wl_listener *listener, void *data)
{
  struct hikari_layer_popup *layer_popup =
      wl_container_of(listener, layer_popup, unmap);

#ifndef NDEBUG
  printf("UNMAP LAYER POPUP %p\n", layer_popup);
#endif

  damage_popup(layer_popup, true);
}

static void
commit_popup_handler(struct wl_listener *listener, void *data)
{
  struct hikari_layer_popup *layer_popup =
      wl_container_of(listener, layer_popup, commit);

  /* [COMMENT] Action purpose: Handle wlroots 0.20 initial_commit lifecycle for
  layer shell popups (which are XDG popups). The compositor must respond with a
  configure on the first commit so the popup can map.

  Unconstraining happens here rather than in init_popup for the same reason as
  the xdg_view.c popup path: wlr_xdg_popup_unconstrain_from_box() ends with
  wlr_xdg_surface_schedule_configure() (wlr_xdg_popup.c:534), which asserts
  surface->initialized (wlr_xdg_surface.c:168), and wlroots emits new_popup
  before the popup surface has ever been committed. Doing it at creation time
  aborted the compositor on every layer-shell popup. Unconstraining also
  schedules the configure, so no separate call is needed. See DECISIONS_LOG
  Phase 61. */
  if (layer_popup->popup->base->initial_commit) {
    popup_unconstrain(layer_popup);
    return;
  }

  damage_popup(layer_popup, false);
}

// Function purpose: Handle a popup nested under another already-open
// layer-shell popup (e.g. a submenu), tracking it the same way as a
// top-level layer popup.
static void
new_popup_popup_handler(struct wl_listener *listener, void *data)
{
  struct hikari_layer_popup *layer_popup =
      wl_container_of(listener, layer_popup, new_popup);

#ifndef NDEBUG
  printf("NEW LAYER POPUP POPUP %p\n", layer_popup);
#endif

  struct wlr_xdg_popup *wlr_popup = data;

  // [COMMENT] Action purpose: Graceful-degradation allocation, matching
  // xdg_view.c's xdg_popup_create -- this struct is hikari's own tracking
  // (unconstrain/damage), not required for the popup to render. See
  // DECISIONS_LOG Finding 4.
  struct hikari_layer_popup *layer_popup_popup =
      hikari_try_malloc(sizeof(struct hikari_layer_popup));

  if (layer_popup_popup == NULL) {
    return;
  }

  /* [COMMENT] Action purpose: Free the tracking struct when scene-tree setup
  fails; init_popup registers no listeners before that point, so a plain free
  is the complete cleanup. */
  if (!init_popup_popup(layer_popup_popup, layer_popup, wlr_popup)) {
    hikari_free(layer_popup_popup);
  }
}

// Function purpose: Handle a new popup requested directly by a layer
// surface (e.g. a panel's context menu), allocating and initialising its
// tracking struct.
static void
new_popup_handler(struct wl_listener *listener, void *data)
{
  struct hikari_layer *layer = wl_container_of(listener, layer, new_popup);

#ifndef NDEBUG
  printf("NEW LAYER POPUP\n");
#endif

  // [COMMENT] Action purpose: Graceful-degradation allocation -- see
  // new_popup_popup_handler above and DECISIONS_LOG Finding 4.
  struct hikari_layer_popup *layer_popup =
      hikari_try_malloc(sizeof(struct hikari_layer_popup));

  if (layer_popup == NULL) {
    return;
  }

  struct wlr_xdg_popup *wlr_popup = data;

  /* [COMMENT] Action purpose: Free the tracking struct when scene-tree setup
  fails; init_popup registers no listeners before that point, so a plain free
  is the complete cleanup. */
  if (!init_layer_popup(layer_popup, layer, wlr_popup)) {
    hikari_free(layer_popup);
  }
}

static void
focus(struct hikari_node *node)
{
  assert(node != NULL);

  struct hikari_layer *layer = (struct hikari_layer *)node;
  struct wlr_layer_surface_v1_state *state = &layer->surface->current;

  if (state->keyboard_interactive) {
    struct hikari_workspace *workspace = hikari_server.workspace;
    struct hikari_view *focus_view = workspace->focus_view;
    struct hikari_layer *focus_layer = workspace->focus_layer;
    struct wlr_seat *seat = hikari_server.seat;
    struct wlr_keyboard *wlr_keyboard = wlr_seat_get_keyboard(seat);

    if (focus_view != NULL) {
      hikari_workspace_focus_view(workspace, NULL);
    }

    if (focus_layer != NULL) {
      wlr_seat_keyboard_clear_focus(seat);
    }

    if (wlr_keyboard != NULL) {
      wlr_seat_keyboard_notify_enter(seat,
          layer->surface->surface,
          wlr_keyboard->keycodes,
          wlr_keyboard->num_keycodes,
          &wlr_keyboard->modifiers);
    }

    workspace->focus_layer = layer;
  }
}

static void
for_each_surface(struct hikari_node *node,
    void (*func)(struct wlr_surface *, int, int, void *),
    void *data)
{
  struct hikari_layer *layer = (struct hikari_layer *)node;

  wlr_layer_surface_v1_for_each_surface(layer->surface, func, data);
}

static struct wlr_surface *
surface_at(
    struct hikari_node *node, double ox, double oy, double *sx, double *sy)
{
  struct hikari_layer *layer = (struct hikari_layer *)node;

  double x = ox - layer->geometry.x;
  double y = oy - layer->geometry.y;

  struct wlr_surface *surface =
      wlr_layer_surface_v1_surface_at(layer->surface, x, y, sx, sy);

  return surface;
}

static bool
init_layer_popup(struct hikari_layer_popup *layer_popup,
    struct hikari_layer *parent,
    struct wlr_xdg_popup *wlr_popup)
{
  layer_popup->parent.type = HIKARI_LAYER_NODE_TYPE_LAYER;
  layer_popup->parent.node.layer = parent;

  return init_popup(layer_popup, wlr_popup);
}

static bool
init_popup_popup(struct hikari_layer_popup *layer_popup,
    struct hikari_layer_popup *parent,
    struct wlr_xdg_popup *wlr_popup)
{
  layer_popup->parent.type = HIKARI_LAYER_NODE_TYPE_POPUP;
  layer_popup->parent.node.popup = parent;

  return init_popup(layer_popup, wlr_popup);
}

static void
fini_popup(struct hikari_layer_popup *layer_popup)
{
  wl_list_remove(&layer_popup->commit.link);
  wl_list_remove(&layer_popup->destroy.link);
  wl_list_remove(&layer_popup->map.link);
  wl_list_remove(&layer_popup->unmap.link);
  wl_list_remove(&layer_popup->new_popup.link);
}

#endif
