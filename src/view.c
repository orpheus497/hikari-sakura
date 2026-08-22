#include <hikari/view.h>

#include <assert.h>
#include <string.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>

#include <hikari/color.h>
#include <hikari/configuration.h>
#include <hikari/geometry.h>
#include <hikari/group.h>
#include <hikari/indicator.h>
#include <hikari/layout.h>
#include <hikari/mark.h>
#include <hikari/memory.h>
#include <hikari/operation.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/sheet.h>
#include <hikari/tile.h>
#include <hikari/view_config.h>
#include <hikari/workspace.h>
#include <hikari/xdg_view.h>
#include <hikari/xwayland_view.h>

#define VIEW(name, link)                                                       \
  static struct hikari_view *name##_view(void)                                 \
  {                                                                            \
    struct wl_list *views = hikari_server.visible_views.link;                  \
                                                                               \
    if (!wl_list_empty(views)) {                                               \
      struct hikari_view *view;                                                \
      view = wl_container_of(views, view, visible_server_views);               \
      return view;                                                             \
    }                                                                          \
                                                                               \
    return NULL;                                                               \
  }                                                                            \
                                                                               \
  static bool is_##name##_view(struct hikari_view *view)                       \
  {                                                                            \
    return view == name##_view();                                              \
  }

VIEW(first, next)
VIEW(last, prev)
#undef VIEW



static void
move_to_top(struct hikari_view *view)
{
  assert(view != NULL);
  assert(hikari_view_is_mapped(view));

  wl_list_remove(&view->sheet_views);
  wl_list_insert(&view->sheet->views, &view->sheet_views);

  wl_list_remove(&view->group_views);
  wl_list_insert(&view->group->views, &view->group_views);

  wl_list_remove(&view->output_views);
  wl_list_insert(&view->output->views, &view->output_views);
}

/* Function purpose: Stacking-order counterpart of move_to_top() -- moves the
view to the back of the sheet, group and output lists. Paired with
view_link_visible_at(..., false) by hikari_view_lower(), mirroring how
move_to_top() pairs with view_link_visible() in raise_view(). */
static void
move_to_bottom(struct hikari_view *view)
{
  assert(view != NULL);
  assert(hikari_view_is_mapped(view));

  wl_list_remove(&view->sheet_views);
  wl_list_insert(view->sheet->views.prev, &view->sheet_views);

  wl_list_remove(&view->group_views);
  wl_list_insert(view->group->views.prev, &view->group_views);

  wl_list_remove(&view->output_views);
  wl_list_insert(view->output->views.prev, &view->output_views);
}

/* Function purpose: Single writer for the "this view is visible" linkage. Links
the view into -- or re-links it to either end of -- every list that represents
visibility: hikari_server.visible_views, its group's visible_views, the group's
own server-visibility aggregate, and the workspace's views. `front` selects
raise (true) or lower (false) ordering.

Because each link is removed before being inserted, and every link is kept in a
valid state by hikari_view_init() and view_unlink_visible(), this is idempotent
with respect to membership: calling it on an already-visible view merely restacks
it. That is why one function serves "become visible", "raise" and "lower" without
separate code paths. Nothing else in this file may link these four lists -- see
view_unlink_visible() for the inverse, and DECISIONS_LOG Phase 55 for why the
previously-split entry path (increase_group_visiblity + place_visibly_above) and
hikari_view_lower()'s separate inline copy were collapsed into this one writer. */
static void
view_link_visible_at(struct hikari_view *view,
    struct hikari_workspace *workspace,
    bool front)
{
  assert(hikari_view_is_forced(view) ? hikari_view_is_hidden(view)
                                     : !hikari_view_is_hidden(view));

  struct wl_list *visible_views = &hikari_server.visible_views;
  struct wl_list *group_visible_views = &view->group->visible_views;
  struct wl_list *visible_groups = &hikari_server.visible_groups;
  struct wl_list *workspace_views = &workspace->views;

  /* [COMMENT] Action purpose: Each list's tail anchor is read after that list's
  own removal, never before -- removing a view that is currently last would
  otherwise leave the cached `prev` pointing at the view being re-inserted. */
  wl_list_remove(&view->visible_server_views);
  wl_list_insert(front ? visible_views : visible_views->prev,
      &view->visible_server_views);

  wl_list_remove(&view->visible_group_views);
  wl_list_insert(front ? group_visible_views : group_visible_views->prev,
      &view->visible_group_views);

  wl_list_remove(&view->group->visible_server_groups);
  wl_list_insert(front ? visible_groups : visible_groups->prev,
      &view->group->visible_server_groups);

  wl_list_remove(&view->workspace_views);
  wl_list_insert(front ? workspace_views : workspace_views->prev,
      &view->workspace_views);
}

static void
view_link_visible(struct hikari_view *view, struct hikari_workspace *workspace)
{
  view_link_visible_at(view, workspace, true);
}

static void
raise_view(struct hikari_view *view)
{
  assert(view != NULL);

  move_to_top(view);
  view_link_visible(view, view->sheet->workspace);

  /* [COMMENT] Action purpose: Restack the view in the scene graph too.
  move_to_top() and view_link_visible() only reorder hikari's own lists, which
  drive focus and cycling; until this call existed nothing ever reordered the
  scene node, so window stacking was fixed at map time and hikari_view_raise()
  had no visual effect at all -- clicking a partially covered window raised it
  for focus purposes while it stayed drawn underneath. Scoped to the view's
  parent tree, so a raise cannot lift a window out of its layer: while locked,
  public views live in the lock layer and raise among themselves there. */
  if (view->scene_node != NULL) {
    wlr_scene_node_raise_to_top(view->scene_node);
  }
}

static void
refresh_border_geometry(struct hikari_view *view)
{
  assert(view != NULL);
  hikari_border_refresh_geometry(&view->border, view->current_geometry);
  hikari_indicator_frame_refresh_geometry(&view->indicator_frame, view);
}

static inline void
move_view_constrained(
    struct hikari_view *view, struct wlr_box *geometry, int x, int y)
{
  if (!hikari_view_is_hidden(view)) {
    hikari_view_damage_whole(view);
    hikari_indicator_position(&hikari_server.indicator, view);
  }

  hikari_geometry_constrain_relative(
      geometry, &view->output->usable_area, x, y);
}

static void
move_view(struct hikari_view *view, struct wlr_box *geometry, int x, int y)
{
  if (view->maximized_state != NULL) {
    struct wlr_box *usable_area;

    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        return;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        usable_area = &view->output->usable_area;

        if (y != usable_area->y) {
          return;
        }

        move_view_constrained(view, geometry, x, y);
        view->geometry.x = x;
        break;

      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        usable_area = &view->output->usable_area;

        if (x != usable_area->x) {
          return;
        }

        move_view_constrained(view, geometry, x, y);
        view->geometry.y = y;
        break;
    }
  } else {
    move_view_constrained(view, geometry, x, y);
  }

#ifdef HAVE_XWAYLAND
  if (view->move != NULL) {
    view->move(view, geometry->x, geometry->y);
  }
#endif

  refresh_border_geometry(view);

  if (!hikari_view_is_hidden(view)) {
    hikari_view_damage_whole(view);
    hikari_indicator_position(&hikari_server.indicator, view);
  }
}

/* Function purpose: Unlink a view from its group's visibility bookkeeping only
-- its group visible_views membership and, once that group has no visible views
left, the group's server-visibility aggregate. Deliberately does NOT touch the
hidden flag or the workspace/server lists, because it serves two different
callers: view_unlink_visible() below, where it is one step of a full transition,
and remove_from_group(), which reassigns a view between groups while the view
remains visible throughout. */
static void
view_unlink_group_visible(struct hikari_view *view)
{
  struct hikari_group *group = view->group;

  wl_list_remove(&view->visible_group_views);
  wl_list_init(&view->visible_group_views);

  /* [COMMENT] Action purpose: Drop the group's server-visibility aggregate only
  once this group's last visible view has gone. Checked after the removal above,
  mirroring how view_link_visible() re-establishes the aggregate on the way in.
  Re-initialised so hikari_group_fini()'s unconditional removal stays safe. */
  if (wl_list_empty(&group->visible_views)) {
    wl_list_remove(&group->visible_server_groups);
    wl_list_init(&group->visible_server_groups);
  }
}

/* Function purpose: Single writer for leaving the visible state -- the exact
inverse of view_link_visible(). Unlinks the view from all four visibility lists,
drops the group's server-visibility aggregate once that group has no visible
views left, and sets the hidden flag. Every link is re-initialised immediately
after removal so a subsequent removal is a harmless no-op rather than a NULL
dereference (libwayland's wl_list_remove leaves both pointers NULL).

Nothing else in this file may unlink these lists. The previous arrangement --
where the hidden flag could be set without performing the unlink -- is what
allowed a view to be freed while still linked into three lists, and its group to
be freed while still linked into hikari_server.visible_groups. Because this
function sets the flag itself, the flag can no longer diverge from the linkage.
See DECISIONS_LOG Phase 55. */
static void
view_unlink_visible(struct hikari_view *view)
{
  view_unlink_group_visible(view);

  wl_list_remove(&view->workspace_views);
  wl_list_init(&view->workspace_views);

  wl_list_remove(&view->visible_server_views);
  wl_list_init(&view->visible_server_views);

  hikari_view_set_hidden(view);
}

static void
detach_from_group(struct hikari_view *view)
{
  struct hikari_group *group = view->group;

  wl_list_remove(&view->group_views);
  wl_list_init(&view->group_views);

  if (wl_list_empty(&group->views)) {
    /* [COMMENT] Action purpose: Make the group-lifetime invariant explicit and
    checked. A group with no views can have no visible views, so by the time it
    is freed its visible_views list must already be empty -- otherwise the freed
    group is still referenced from hikari_server.visible_groups and from the
    visible_group_views link of whatever view is still listed, which is walked
    on every focus change. This invariant was previously unwritten and
    maintained by entirely separate functions. See DECISIONS_LOG Phase 55. */
    assert(wl_list_empty(&group->visible_views));

    hikari_group_fini(group);
    hikari_free(group);
  }
}

static void
remove_from_group(struct hikari_view *view)
{
  assert(!hikari_view_is_hidden(view));

  /* [COMMENT] Action purpose: Drop this view from the old group's visibility
  bookkeeping before its membership is dropped -- detach_from_group() may free
  the group, and doing so while this view is still listed in
  group->visible_views leaves freed memory reachable from
  hikari_server.visible_groups. Only the group-scoped unlink is used: the view
  stays visible and keeps its workspace/server list membership and its hidden
  flag, because the caller (hikari_view_group) is reassigning it to another
  group, not hiding it. */
  if (!hikari_view_is_hidden(view)) {
    view_unlink_group_visible(view);
  }

  detach_from_group(view);
}

static void
cancel_tile(struct hikari_view *view)
{
  if (hikari_view_is_tiling(view)) {
    struct hikari_tile *tile = view->pending_operation.tile;

    hikari_tile_detach(tile);
    hikari_free(tile);
    view->pending_operation.tile = NULL;
  }
}

static inline void
guarded_resize(struct hikari_view *view,
    struct hikari_operation *op,
    void (*f)(struct hikari_view *, struct hikari_operation *))
{
  op->serial = view->resize(view, op->geometry.width, op->geometry.height);

  if (op->serial == 0) {
    f(view, op);
  } else {
    hikari_view_set_dirty(view);
  }
}

static inline void
resize(struct hikari_view *view,
    struct hikari_operation *op,
    void (*f)(struct hikari_view *, struct hikari_operation *))
{
#ifdef HAVE_XWAYLAND
  if (view->move_resize != NULL) {
    op->serial = 0;
    view->move_resize(view,
        op->geometry.x,
        op->geometry.y,
        op->geometry.width,
        op->geometry.height);

    hikari_view_set_dirty(view);
  } else {
    guarded_resize(view, op, f);
  }
#else
  guarded_resize(view, op, f);
#endif
}

static void
commit_pending_geometry(
    struct hikari_view *view, struct wlr_box *pending_geometry)
{
  hikari_view_refresh_geometry(view, pending_geometry);

  hikari_indicator_position(&hikari_server.indicator, view);
  hikari_view_damage_whole(view);
}

static void
commit_pending_operation(
    struct hikari_view *view, struct hikari_operation *operation)
{
  if (!hikari_view_is_hidden(view)) {
    if (!hikari_view_is_forced(view)) {
      raise_view(view);
    } else {
      move_to_top(view);
    }

    commit_pending_geometry(view, &operation->geometry);

    if (operation->center) {
      hikari_view_center_cursor(view);
      hikari_server_cursor_focus();
    }
  } else {
    hikari_view_refresh_geometry(view, &operation->geometry);

    if (hikari_sheet_is_visible(view->sheet)) {
      if (!hikari_view_is_forced(view)) {
        hikari_view_show(view);
      } else {
        raise_view(view);
      }

      if (operation->center) {
        hikari_view_center_cursor(view);
        hikari_server_cursor_focus();
      }
    } else {
      if (!hikari_view_is_forced(view)) {
        move_to_top(view);
      } else {
        raise_view(view);
      }
    }
  }
}

// [COMMENT] Function purpose: Handle resetting view state operations.
static void
commit_reset(struct hikari_view *view, struct hikari_operation *operation)
{
  // [COMMENT] Action purpose: Skip indicator repositioning for hidden views.
  if (!hikari_view_is_hidden(view)) {
    hikari_indicator_position(&hikari_server.indicator, view);
  }

  if (hikari_view_is_tiled(view)) {
    assert(!hikari_tile_is_attached(view->tile));
    hikari_free(view->tile);
    view->tile = NULL;
  }

  if (hikari_view_is_maximized(view)) {
    hikari_maximized_state_destroy(view->maximized_state);
    view->maximized_state = NULL;

    if (!view->use_csd) {
      if (view == hikari_server.workspace->focus_view) {
        view->border.state = HIKARI_BORDER_ACTIVE;
      } else {
        view->border.state = HIKARI_BORDER_INACTIVE;
      }
    }
  }

  commit_pending_operation(view, operation);
}

static void
queue_reset(struct hikari_view *view, bool center)
{
  struct wlr_box *view_geometry = hikari_view_geometry(view);
  struct hikari_operation *op = &view->pending_operation;
  struct hikari_tile *tile = view->tile;
  struct wlr_box *geometry = &view->geometry;

  cancel_tile(view);

  if (hikari_view_is_tiled(view) && hikari_tile_is_attached(tile)) {
    hikari_tile_detach(tile);
  }

  op->type = HIKARI_OPERATION_TYPE_RESET;
  op->center = center;
  memcpy(&op->geometry, geometry, sizeof(struct wlr_box));

  if (view_geometry->width == geometry->width &&
      view_geometry->height == geometry->height) {
    hikari_view_set_dirty(view);
    hikari_view_commit_pending_operation(view, view_geometry);
  } else {
    resize(view, op, commit_reset);
  }

  assert(
      hikari_view_is_tiled(view) ? !hikari_tile_is_attached(view->tile) : true);
}

static void
clear_focus(struct hikari_view *view)
{
  if (hikari_view_is_focus_view(view)) {
    if (hikari_view_has_focus(view)) {
      assert(!hikari_server_in_lock_mode());

      if (!hikari_server_in_normal_mode()) {
        hikari_server_enter_normal_mode(NULL);
      }

      hikari_workspace_focus_view(hikari_server.workspace, NULL);
    } else {
      view->sheet->workspace->focus_view = NULL;
    }
  }
}

void
hikari_view_init(
    struct hikari_view *view, bool child, struct hikari_workspace *workspace)
{
#if !defined(NDEBUG)
  printf("VIEW INIT %p\n", view);
#endif
  view->flags = 0;
  hikari_view_set_hidden(view);
  memset(&view->border, 0, sizeof(struct hikari_border));
  view->border.state = HIKARI_BORDER_INACTIVE;
  view->sheet = NULL;
  view->mark = NULL;
  view->surface = NULL;
  view->maximized_state = NULL;
  /* [COMMENT] Action purpose: Seed the output from the workspace the view is
  being created on, rather than leaving it NULL until hikari_view_configure()
  runs. Both first_map() paths (xdg and xwayland) call
  hikari_view_refresh_geometry() BEFORE hikari_view_configure(), and that
  function positions the scene node against view->output->geometry. Leaving
  this NULL made every window creation dereference a NULL output. Seeding it
  here also means the first refresh positions the node correctly instead of
  being skipped; hikari_view_configure() may still reassign it afterwards when
  view config rules select a different output. */
  view->output = workspace != NULL ? workspace->output : NULL;
  view->group = NULL;
  view->title = NULL;
  view->id = NULL;
  view->tile = NULL;
  view->decoration.wlr_decoration = NULL;
  /* [COMMENT] Action purpose: Explicitly null the scene node. The containing
  hikari_xdg_view / hikari_xwayland_view structs are allocated with
  hikari_malloc, which does not zero memory, and show/hide/geometry-refresh
  guard on `scene_node != NULL`. Leaving it indeterminate lets uninitialised
  garbage pass those guards and be dereferenced as a scene node. */
  view->scene_node = NULL;
  view->use_csd = false;
  view->child = child;
  view->current_geometry = &view->geometry;
  view->current_unmaximized_geometry = &view->geometry;

  hikari_view_unset_dirty(view);
  view->pending_operation.tile = NULL;

  /* [COMMENT] Action purpose: Initialise every list link this view can ever be
  linked through, not only children. The containing hikari_xdg_view /
  hikari_xwayland_view structs are allocated with hikari_malloc, which does not
  zero, so any link left untouched here holds indeterminate garbage until
  something first inserts it -- while hikari_view_fini() and the teardown paths
  call wl_list_remove() on these links unconditionally. Initialising all seven
  makes every later removal a structural no-op on an unlinked view, instead of
  depending on an unrelated NULL-pointer guard elsewhere happening to skip it.
  See DECISIONS_LOG Phase 55. */
  wl_list_init(&view->output_views);
  wl_list_init(&view->workspace_views);
  wl_list_init(&view->sheet_views);
  wl_list_init(&view->group_views);
  wl_list_init(&view->visible_group_views);
  wl_list_init(&view->visible_server_views);
  wl_list_init(&view->children);
}

void
hikari_view_fini(struct hikari_view *view)
{
  assert(hikari_view_is_hidden(view));
  assert(!hikari_view_is_mapped(view));
  assert(!hikari_view_is_forced(view));

#if !defined(NDEBUG)
  printf("DESTROY VIEW %p\n", view);
#endif

  if (view->decoration.wlr_decoration != NULL) {
    wl_list_remove(&view->decoration.mode.link);
    wl_list_remove(&view->decoration.destroy.link);
  }

  hikari_free(view->title);
  hikari_free(view->id);

  if (view->group != NULL) {
    detach_from_group(view);
  }

  if (view->sheet != NULL) {
    wl_list_remove(&view->sheet_views);
    wl_list_remove(&view->output_views);
  }

  if (view->maximized_state != NULL) {
    hikari_maximized_state_destroy(view->maximized_state);
  }

  if (view->mark != NULL) {
    hikari_mark_clear(view->mark);
  }

  if (hikari_view_is_tiled(view)) {
    struct hikari_tile *tile = view->tile;
    hikari_tile_detach(tile);
    hikari_free(tile);
    view->tile = NULL;
  }

  cancel_tile(view);
}

void
hikari_view_set_title(struct hikari_view *view, const char *title)
{
  hikari_free(view->title);

  if (title != NULL) {
    view->title = hikari_malloc(strlen(title) + 1);
    strcpy(view->title, title);

    if (hikari_server.workspace->focus_view == view) {
      assert(!hikari_view_is_hidden(view));
      struct hikari_output *output = view->output;
      hikari_indicator_update_title(&hikari_server.indicator, output, title);
    }
  } else {
    view->title = NULL;
  }
}

static void
set_app_id(struct hikari_view *view, const char *id)
{
  assert(view->id == NULL);
  assert(id != NULL);

  view->id = hikari_malloc(strlen(id) + 1);

  strcpy(view->id, id);
}

struct hikari_damage_data {
  struct wlr_box *geometry;
  struct wlr_surface *surface;

  struct hikari_view *view;
  struct hikari_output *output;

  bool whole;
};

// [COMMENT] Function purpose: wlr_surface_for_each_surface callback that
// damages a single surface (main surface or subsurface) within a view.
static void
damage_whole_surface(struct wlr_surface *surface, int sx, int sy, void *data)
{
  struct hikari_damage_data *damage_data = data;
  struct hikari_output *output = damage_data->output;
  struct hikari_view *view = damage_data->view;

  // [COMMENT] Action purpose: SSD views damage the server border box for their main
  // surface. CSD views carry no server border, so their main surface is damaged
  // granularly by its own buffer extents -- client-drawn decorations and shadows
  // live inside that buffer, exactly like subsurfaces handled by the else branch.
  if (!view->use_csd && view->surface == surface) {
    hikari_view_damage_border(view);
  } else {
    struct wlr_box geometry;
    memcpy(&geometry, damage_data->geometry, sizeof(struct wlr_box));

    geometry.x += sx;
    geometry.y += sy;
    geometry.width = surface->current.width;
    geometry.height = surface->current.height;

    hikari_output_add_damage(output, &geometry);
  }
}

// [COMMENT] Function purpose: Damage the entire view region granularly by iterating
// all (sub)surfaces and computing a damage box per surface (border box for the SSD
// main surface, buffer extents for CSD and subsurfaces) instead of over-damaging
// the whole output.
void
hikari_view_damage_whole(struct hikari_view *view)
{
  assert(view != NULL);

  // [COMMENT] Action purpose: view->output is NULL until hikari_view_configure.
  if (view->output == NULL) {
    return;
  }

  struct hikari_damage_data damage_data;

  damage_data.geometry = hikari_view_geometry(view);
  damage_data.output = view->output;
  damage_data.view = view;
  damage_data.surface = NULL;
  damage_data.whole = true;

  hikari_node_for_each_surface(
      (struct hikari_node *)view, damage_whole_surface, &damage_data);
}

static struct wlr_box *
refresh_unmaximized_geometry(struct hikari_view *view)
{
  assert(view != NULL);

  if (hikari_view_is_tiled(view)) {
    return &view->tile->view_geometry;
  } else {
    return &view->geometry;
  }
}

static struct wlr_box *
refresh_geometry(struct hikari_view *view)
{
  assert(view != NULL);

  if (view->maximized_state != NULL) {
    return &view->maximized_state->geometry;
  } else {
    return refresh_unmaximized_geometry(view);
  }
}

static inline int
constrain_size(int min, int max, int value)
{
  if (value > max) {
    return max;
  } else if (value < min) {
    return min;
  } else {
    return value;
  }
}

static void
commit_resize(struct hikari_view *view, struct hikari_operation *operation)
{
  commit_pending_operation(view, operation);
}

static void
queue_resize(struct hikari_view *view,
    struct wlr_box *geometry,
    int requested_x,
    int requested_y,
    int requested_width,
    int requested_height)
{
  // [COMMENT] Action purpose: Guard against a missing output. view->output is
  // NULL between hikari_view_init and hikari_view_configure (see the matching
  // guard and explanation in hikari_view_refresh_geometry); a resize queued
  // in that window would otherwise dereference output->usable_area below on a
  // NULL output. There is nothing to constrain the resize against yet, so
  // simply defer it.
  if (view->output == NULL) {
    return;
  }

  struct hikari_operation *op = &view->pending_operation;

  int min_width;
  int min_height;
  int max_width;
  int max_height;

  int new_width;
  int new_height;

  if (view->maximized_state != NULL) {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        return;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        view->constraints(
            view, &min_width, &min_height, &max_width, &max_height);
        new_width = constrain_size(min_width, max_width, requested_width);
        new_height = geometry->height;
        view->geometry.width = new_width;
        break;

      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        view->constraints(
            view, &min_width, &min_height, &max_width, &max_height);
        new_width = geometry->width;
        new_height = constrain_size(min_height, max_height, requested_height);
        view->geometry.height = new_height;
        break;
    }
  } else {
    view->constraints(view, &min_width, &min_height, &max_width, &max_height);
    new_width = constrain_size(min_width, max_width, requested_width);
    new_height = constrain_size(min_height, max_height, requested_height);
  }

  struct hikari_output *output = view->output;

  op->type = HIKARI_OPERATION_TYPE_RESIZE;
  op->geometry.width = new_width;
  op->geometry.height = new_height;
  op->center = false;

  hikari_geometry_constrain_relative(
      &op->geometry, &output->usable_area, requested_x, requested_y);

  resize(view, op, commit_resize);
}

void
hikari_view_resize(struct hikari_view *view, int dwidth, int dheight)
{
  assert(view != NULL);
  assert(view->resize != NULL);
  assert(view->constraints != NULL);

  if (hikari_view_is_dirty(view)) {
    return;
  }

  struct wlr_box *geometry = hikari_view_geometry(view);

  int requested_width = geometry->width + dwidth;
  int requested_height = geometry->height + dheight;

  queue_resize(view,
      geometry,
      geometry->x,
      geometry->y,
      requested_width,
      requested_height);
}

void
hikari_view_resize_absolute(struct hikari_view *view, int width, int height)
{
  assert(view != NULL);
  assert(view->resize != NULL);
  assert(view->constraints != NULL);

  if (hikari_view_is_dirty(view)) {
    return;
  }

  struct wlr_box *geometry = hikari_view_geometry(view);
  queue_resize(view, geometry, geometry->x, geometry->y, width, height);
}

void
hikari_view_move_resize(
    struct hikari_view *view, int x, int y, int width, int height)
{
  assert(view != NULL);
  assert(view->resize != NULL);
  assert(view->constraints != NULL);

  if (hikari_view_is_dirty(view)) {
    return;
  }

  struct wlr_box *geometry = hikari_view_geometry(view);

  int requested_x = geometry->x + x;
  int requested_y = geometry->y + y;
  int requested_width = geometry->width + width;
  int requested_height = geometry->height + height;

  queue_resize(view,
      geometry,
      requested_x,
      requested_y,
      requested_width,
      requested_height);
}

void
hikari_view_move(struct hikari_view *view, int x, int y)
{
  assert(view != NULL);

  // [COMMENT] Action purpose: Same view->output nullability precondition as
  // queue_resize (view->output is NULL between hikari_view_init and
  // hikari_view_configure) — move_view dereferences view->output->usable_area
  // via move_view_constrained, so defer if there is no output yet.
  if (view->output == NULL) {
    return;
  }

  struct wlr_box *geometry = hikari_view_geometry(view);

  move_view(view, geometry, geometry->x + x, geometry->y + y);
}

void
hikari_view_move_absolute(struct hikari_view *view, int x, int y)
{
  assert(view != NULL);

  if (view->output == NULL) {
    return;
  }

  struct wlr_box *geometry = hikari_view_geometry(view);

  move_view(view, geometry, x, y);
}

#define MOVE(pos)                                                              \
  void hikari_view_move_##pos(struct hikari_view *view)                        \
  {                                                                            \
    assert(view != NULL);                                                      \
                                                                               \
    if (view->output == NULL) {                                                \
      return;                                                                  \
    }                                                                          \
                                                                               \
    struct hikari_output *output = view->output;                               \
    struct wlr_box *usable_area = &output->usable_area;                        \
    struct wlr_box *border_geometry = hikari_view_border_geometry(view);       \
    struct wlr_box *geometry = hikari_view_geometry(view);                     \
                                                                               \
    int x;                                                                     \
    int y;                                                                     \
    hikari_geometry_position_##pos(border_geometry, usable_area, &x, &y);      \
                                                                               \
    move_view(view, geometry, x, y);                                           \
  }

MOVE(bottom_left)
MOVE(bottom_middle)
MOVE(bottom_right)
MOVE(center_left)
MOVE(center)
MOVE(center_right)
MOVE(top_left)
MOVE(top_middle)
MOVE(top_right)
#undef MOVE

// Function purpose: Track a subsurface newly attached to a mapped view's
// surface so it participates in hikari's granular damage tracking and
// teardown loop.
static void
new_subsurface_handler(struct wl_listener *listener, void *data)
{
  struct hikari_view *view = wl_container_of(listener, view, new_subsurface);

  struct wlr_subsurface *wlr_subsurface = data;

  // [COMMENT] Action purpose: Graceful-degradation allocation. A subsurface
  // added under memory pressure still renders via wlr_scene (which manages
  // subsurface scene nodes automatically); skipping hikari's own tracking
  // wrapper only loses granular damage-tracking for it, not visibility. See
  // DECISIONS_LOG Finding 4.
  struct hikari_view_subsurface *view_subsurface =
      hikari_try_malloc(sizeof(struct hikari_view_subsurface));

  if (view_subsurface == NULL) {
    return;
  }

  hikari_view_subsurface_init(view_subsurface, view, wlr_subsurface);
}

// Function purpose: Map a view for the first time -- adopt its surface,
// track its existing subsurfaces, resolve its group/mark from view config,
// link it into its sheet/group/output, and make it visible (or forced-hidden
// under lock mode).
void
hikari_view_map(struct hikari_view *view, struct wlr_surface *surface)
{
  assert(hikari_view_is_hidden(view));
  assert(!hikari_view_is_unmanaged(view));
  assert(!hikari_view_is_mapped(view));

  struct hikari_sheet *sheet = view->sheet;
  struct hikari_output *output = view->output;
  struct hikari_group *group;
  bool focus;

  struct hikari_view_config *view_config =
      hikari_configuration_resolve_view_config(hikari_configuration, view->id);

  view->surface = surface;

  view->new_subsurface.notify = new_subsurface_handler;
  wl_signal_add(&surface->events.new_subsurface, &view->new_subsurface);

  // [COMMENT] Action purpose: Graceful-degradation allocation -- see
  // new_subsurface_handler above and DECISIONS_LOG Finding 4.
  struct wlr_subsurface *wlr_subsurface;
  wl_list_for_each (
      wlr_subsurface, &surface->current.subsurfaces_below, current.link) {
    struct hikari_view_subsurface *subsurface =
        hikari_try_malloc(sizeof(struct hikari_view_subsurface));
    if (subsurface == NULL) {
      continue;
    }
    hikari_view_subsurface_init(subsurface, view, wlr_subsurface);
  }
  wl_list_for_each (
      wlr_subsurface, &surface->current.subsurfaces_above, current.link) {
    struct hikari_view_subsurface *subsurface =
        hikari_try_malloc(sizeof(struct hikari_view_subsurface));
    if (subsurface == NULL) {
      continue;
    }
    hikari_view_subsurface_init(subsurface, view, wlr_subsurface);
  }

  if (view_config != NULL) {
    struct hikari_mark *mark;
    struct hikari_view_properties *properties =
        hikari_view_config_resolve_properties(view_config, view->child);

    assert(properties != NULL);

    group = hikari_view_properties_resolve_group(properties, view->id);
    mark = properties->mark;

    if (mark != NULL && mark->view == NULL) {
      hikari_mark_set(mark, view);
    }

    focus = properties->focus;
  } else {
    group = hikari_server_find_or_create_group(view->id);
    focus = false;
  }

  view->group = group;

  wl_list_insert(&sheet->views, &view->sheet_views);
  wl_list_insert(&group->views, &view->group_views);
  wl_list_insert(&output->views, &view->output_views);

  /* [COMMENT] Action purpose: Decide the view's layer before anything shows or
  raises it, and do so unconditionally rather than only in the lock-mode case.

  A view's scene tree outlives an unmap -- it is destroyed in the shell's
  destroy_handler, not here -- so a view can carry a stale parent across a
  map/unmap cycle. A public view that unmapped while the screen was locked
  would otherwise still be parented to the lock layer when it remapped after
  the unlock, and stay invisible forever because that layer is disabled.
  Deriving the parent here on every map makes the layer a property of the
  view's current state rather than of whatever happened to be true when it was
  first constructed. */
  if (view->scene_node != NULL) {
    struct wlr_scene_tree *layer =
        (hikari_server_in_lock_mode() && hikari_view_is_public(view))
            ? hikari_server.layers.lock
            : hikari_server.layers.views;

    wlr_scene_node_reparent(view->scene_node, layer);
  }

  if (!hikari_server_in_lock_mode() || hikari_view_is_public(view)) {
    hikari_view_show(view);

    if (focus) {
      hikari_view_center_cursor(view);
    }

    hikari_server_cursor_focus();
  } else {
    /* [COMMENT] Action purpose: A non-public view mapping while the screen is
    locked. It is linked into the visible lists and flagged hidden/forced, which
    is what the rest of view.c expects, but its invisibility comes from the
    scene graph: the tree it was parented to at construction is the view layer,
    which override_visibility() has disabled, and wlr_scene disables every child
    of a disabled node.

    This used to claim the scene node was disabled here. It was not -- nothing
    in this path ever touched it, and raise_view() only reorders hikari's own
    lists -- so a window mapping while locked drew itself straight onto the lock
    screen. See DECISIONS_LOG Phase 70 F2. */
    hikari_view_set_forced(view);
    raise_view(view);
  }
}

// Function purpose: Tear down a view on unmap -- finalise every child
// (subsurface or popup) via its own fini pointer, detach from group/tile/
// mark, and unlink from the sheet/output lists, leaving the view struct
// itself intact for a possible remap.
void
hikari_view_unmap(struct hikari_view *view)
{
  assert(!hikari_view_is_unmanaged(view));
  assert(hikari_view_is_mapped(view));

  wl_list_remove(&view->new_subsurface.link);

  // [COMMENT] Action purpose: Dispatch through each child's own fini pointer
  // instead of assuming every entry is a hikari_view_subsurface. hikari_xdg_popup
  // is also linked into view->children via the shared hikari_view_child prefix
  // (xdg_popup_create -> hikari_view_child_init), and its layout diverges past
  // that prefix -- blindly casting freed the wrong fields and left wlroots
  // holding live listeners into freed memory. See DECISIONS_LOG Phase 42/44.
  struct hikari_view_child *child, *child_temp;
  wl_list_for_each_safe (child, child_temp, &view->children, link) {
    child->fini(child);
  }

  /* [COMMENT] Action purpose: Leave the visible state through the single unlink
  writer, whichever state the view is in. A forced view (lock mode) is linked
  into the visibility lists while flagged hidden, so it must still be unlinked;
  it never holds focus, which is why it does not go through hikari_view_hide().

  This previously branched on the hidden flag and, when a forced view was not
  flagged hidden, set the flag WITHOUT performing the unlink -- leaving the view
  linked into workspace->views, hikari_server.visible_views and
  group->visible_views while execution fell through to detach_from_group()
  below, which frees the group. That freed a group still reachable from
  hikari_server.visible_groups and left three lists pointing at a view struct
  freed moments later by destroy_handler. view_unlink_visible() sets the hidden
  flag itself, so the flag can no longer diverge from the linkage. See
  DECISIONS_LOG Phase 55. */
  if (hikari_view_is_forced(view)) {
    view_unlink_visible(view);
    hikari_view_unset_forced(view);
  } else if (!hikari_view_is_hidden(view)) {
    hikari_view_hide(view);
    hikari_server_cursor_focus();
  }

  assert(hikari_view_is_hidden(view));
  assert(!hikari_view_is_forced(view));

  view->surface = NULL;

  struct hikari_mark *mark = view->mark;
  if (mark != NULL) {
    hikari_mark_clear(mark);
  }

  detach_from_group(view);
  view->group = NULL;

  cancel_tile(view);

  if (hikari_view_is_tiled(view)) {
    struct wlr_box geometry;
    struct hikari_tile *tile = view->tile;

    memcpy(&geometry, hikari_view_geometry(view), sizeof(struct wlr_box));

    if (hikari_tile_is_attached(tile)) {
      hikari_tile_detach(tile);
    }

    hikari_free(tile);
    view->tile = NULL;

    hikari_view_refresh_geometry(view, &geometry);
  }

  wl_list_remove(&view->sheet_views);
  wl_list_init(&view->sheet_views);

  wl_list_remove(&view->output_views);
  wl_list_init(&view->output_views);

  hikari_view_unset_dirty(view);

  assert(!hikari_view_is_tiling(view));
  assert(!hikari_view_is_tiled(view));
}

// [COMMENT] Function purpose: Show a hidden view and enable its scene node.
void
hikari_view_show(struct hikari_view *view)
{
  assert(view != NULL);
  assert(hikari_view_is_hidden(view));
  assert(!hikari_view_is_forced(view));

#if !defined(NDEBUG)
  printf("SHOW %p\n", view);
#endif
  hikari_view_unset_hidden(view);

  // [COMMENT] Action purpose: Guard against missing scene node before enabling it.
  if (view->scene_node != NULL) {
    wlr_scene_node_set_enabled(view->scene_node, true);
  }

  /* [COMMENT] Action purpose: raise_view() performs the visibility linkage
  through view_link_visible(), the single writer. The separate
  increase_group_visiblity() call this replaced only re-established the group's
  server-visibility aggregate, which view_link_visible() now does unconditionally
  as part of the same transition -- keeping the flag, the scene node and all
  four lists updated in one place. See DECISIONS_LOG Phase 55. */
  raise_view(view);

  hikari_view_damage_whole(view);

  assert(is_first_view(view));
}

// [COMMENT] Function purpose: Hide a visible view and disable its scene node.
void
hikari_view_hide(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));
  assert(!hikari_view_is_forced(view));

#if !defined(NDEBUG)
  printf("HIDE %p\n", view);
#endif

  /* [COMMENT] Action purpose: clear_focus() must run before the unlink -- it
  resolves the successor focus by consulting the very lists view_unlink_visible()
  is about to remove this view from. */
  clear_focus(view);
  view_unlink_visible(view);

  // [COMMENT] Action purpose: Guard against missing scene node before disabling it.
  if (view->scene_node != NULL) {
    wlr_scene_node_set_enabled(view->scene_node, false);
  }

  // [COMMENT] Action purpose: Hide the indicator frame overlay when the view is hidden.
  hikari_indicator_frame_hide(&view->indicator_frame);

  hikari_view_damage_whole(view);
}

void
hikari_view_raise(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  if (is_first_view(view)) {
    return;
  }

  raise_view(view);
  hikari_view_damage_whole(view);
}

void
hikari_view_lower(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  if (is_last_view(view)) {
    return;
  }

  /* [COMMENT] Action purpose: Lower is the exact mirror of raise_view() --
  stacking lists to the back via move_to_bottom(), visibility lists to the back
  via the same single writer used everywhere else. This previously inlined its
  own remove/insert against all seven lists, a third hand-maintained copy of the
  linkage that shared no code with move_to_top() or view_link_visible() and had
  to be kept in agreement with them by hand. See DECISIONS_LOG Phase 55. */
  move_to_bottom(view);
  view_link_visible_at(view, view->sheet->workspace, false);

  /* [COMMENT] Action purpose: The scene-graph half of the mirror. Counterpart
  to the raise in raise_view(); without it lowering reordered hikari's lists
  while the window stayed drawn exactly where it was. Scoped to the view's
  parent tree, so this can only reorder among views, never sink one below the
  wallpaper or out of the lock layer. */
  if (view->scene_node != NULL) {
    wlr_scene_node_lower_to_bottom(view->scene_node);
  }

  hikari_view_damage_whole(view);
}

static void
commit_tile(struct hikari_view *view, struct hikari_operation *operation)
{
  if (view->maximized_state) {
    hikari_maximized_state_destroy(view->maximized_state);
    view->maximized_state = NULL;
    if (!view->use_csd) {
      view->border.state = HIKARI_BORDER_INACTIVE;
    }
  }

  assert(hikari_tile_is_attached(operation->tile));

  if (hikari_view_is_tiled(view)) {
    struct hikari_tile *tile = view->tile;

    assert(hikari_tile_is_attached(tile));

    wl_list_remove(&tile->layout_tiles);
    hikari_free(tile);
    view->tile = NULL;
  }

  assert(!hikari_view_is_tiled(view));
  assert(operation->tile != NULL);
  view->tile = operation->tile;
  operation->tile = NULL;

  if (!hikari_view_is_hidden(view)) {
    commit_pending_geometry(view, &operation->geometry);
    if (operation->center) {
      hikari_view_center_cursor(view);
    }
    hikari_server_cursor_focus();
  } else {
    hikari_view_refresh_geometry(view, &operation->geometry);
  }
}

static void
queue_tile(struct hikari_view *view,
    struct hikari_layout *layout,
    struct hikari_tile *tile,
    bool center)
{
  assert(!hikari_view_is_dirty(view));

  struct hikari_operation *op = &view->pending_operation;

  struct wlr_box *current_geometry = hikari_view_geometry(view);

  op->type = HIKARI_OPERATION_TYPE_TILE;
  op->tile = tile;
  op->geometry = tile->view_geometry;
  op->center = center;

  if (current_geometry->width == op->geometry.width &&
      current_geometry->height == op->geometry.height) {
    hikari_view_set_dirty(view);

    hikari_view_commit_pending_operation(view, current_geometry);
  } else {
    resize(view, op, commit_tile);
  }
}

void
hikari_view_tile(
    struct hikari_view *view, struct wlr_box *geometry, bool center)
{
  assert(!hikari_view_is_dirty(view));
  assert(hikari_view_is_tileable(view));

  struct hikari_layout *layout = view->sheet->workspace->sheet->layout;

  struct hikari_tile *tile = hikari_malloc(sizeof(struct hikari_tile));
  assert(tile != NULL);
  hikari_tile_init(tile, view, layout, geometry, geometry);

  queue_tile(view, layout, tile, center);

  wl_list_insert(layout->tiles.prev, &tile->layout_tiles);
}

static void
commit_full_maximize(
    struct hikari_view *view, struct hikari_operation *operation)
{
  if (!view->maximized_state) {
    view->maximized_state =
        hikari_malloc(sizeof(struct hikari_maximized_state));
  }

  view->maximized_state->maximization = HIKARI_MAXIMIZATION_FULLY_MAXIMIZED;
  view->maximized_state->geometry = operation->geometry;

  if (!view->use_csd) {
    view->border.state = HIKARI_BORDER_NONE;
  }

  commit_pending_operation(view, operation);
}

static void
queue_full_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  struct hikari_operation *op = &view->pending_operation;
  struct hikari_output *output = view->output;

  op->type = HIKARI_OPERATION_TYPE_FULL_MAXIMIZE;
  op->geometry = output->usable_area;
  op->center = true;

  resize(view, op, commit_full_maximize);
}

static void
commit_unmaximize(struct hikari_view *view, struct hikari_operation *operation)
{
  hikari_view_damage_whole(view);

  hikari_free(view->maximized_state);
  view->maximized_state = NULL;

  if (!view->use_csd) {
    view->border.state = HIKARI_BORDER_ACTIVE;
  }

  commit_pending_operation(view, operation);
}

static void
queue_unmaximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  struct hikari_operation *op = &view->pending_operation;

  op->type = HIKARI_OPERATION_TYPE_UNMAXIMIZE;
  op->center = true;

  if (view->tile != NULL) {
    op->geometry = view->tile->view_geometry;
  } else {
    op->geometry = view->geometry;
  }

  resize(view, op, commit_unmaximize);
}

void
hikari_view_toggle_full_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  if (hikari_view_is_dirty(view)) {
    return;
  }

  if (hikari_view_is_fully_maximized(view)) {
    queue_unmaximize(view);
  } else {
    queue_full_maximize(view);
  }
}

void
hikari_view_toggle_public(struct hikari_view *view)
{
  if (hikari_view_is_public(view)) {
    hikari_view_unset_public(view);
  } else {
    hikari_view_set_public(view);
  }
}

static void
commit_horizontal_maximize(
    struct hikari_view *view, struct hikari_operation *operation)
{
  if (!view->maximized_state) {
    view->maximized_state =
        hikari_malloc(sizeof(struct hikari_maximized_state));
  } else {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        commit_full_maximize(view, operation);
        return;

      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        if (!view->use_csd) {
          view->border.state = HIKARI_BORDER_INACTIVE;
        }
        break;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        assert(false);
        break;
    }
  }

  view->maximized_state->maximization =
      HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED;
  view->maximized_state->geometry = operation->geometry;

  commit_pending_operation(view, operation);
}

static void
queue_horizontal_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  struct hikari_operation *op = &view->pending_operation;
  struct hikari_output *output = view->output;

  struct wlr_box *geometry = view->current_unmaximized_geometry;

  op->type = HIKARI_OPERATION_TYPE_HORIZONTAL_MAXIMIZE;
  op->geometry.x = output->usable_area.x;
  op->geometry.y = geometry->y;
  op->geometry.height = geometry->height;
  op->geometry.width = output->usable_area.width;
  op->center = true;

  resize(view, op, commit_horizontal_maximize);
}

static void
commit_vertical_maximize(
    struct hikari_view *view, struct hikari_operation *operation)
{
  if (!view->maximized_state) {
    view->maximized_state =
        hikari_malloc(sizeof(struct hikari_maximized_state));
  } else {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        commit_full_maximize(view, operation);
        return;

      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        if (!view->use_csd) {
          view->border.state = HIKARI_BORDER_INACTIVE;
        }
        break;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        assert(false);
        break;
    }
  }

  view->maximized_state->maximization =
      HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED;
  view->maximized_state->geometry = operation->geometry;

  commit_pending_operation(view, operation);
}

static void
queue_vertical_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  struct hikari_operation *op = &view->pending_operation;
  struct hikari_output *output = view->output;

  struct wlr_box *geometry = view->current_unmaximized_geometry;

  op->type = HIKARI_OPERATION_TYPE_VERTICAL_MAXIMIZE;
  op->geometry.x = geometry->x;
  op->geometry.y = output->usable_area.y;
  op->geometry.height = output->usable_area.height;
  op->geometry.width = geometry->width;
  op->center = true;

  resize(view, op, commit_vertical_maximize);
}

void
hikari_view_toggle_vertical_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  if (hikari_view_is_dirty(view)) {
    return;
  }

  if (view->maximized_state != NULL) {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        queue_horizontal_maximize(view);
        break;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        queue_unmaximize(view);
        break;

      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        queue_full_maximize(view);
        break;
    }
  } else {
    queue_vertical_maximize(view);
  }
}

void
hikari_view_toggle_horizontal_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  if (view->maximized_state != NULL) {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        queue_vertical_maximize(view);
        break;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        queue_full_maximize(view);
        break;

      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        queue_unmaximize(view);
        break;
    }
  } else {
    queue_horizontal_maximize(view);
  }
}

void
hikari_view_toggle_floating(struct hikari_view *view)
{
  if (!hikari_view_is_floating(view)) {
    if (hikari_view_is_tiled(view)) {
      hikari_view_reset_geometry(view);
    }
    hikari_view_set_floating(view);
  } else {
    hikari_view_unset_floating(view);
  }
}

void
hikari_view_reset_geometry(struct hikari_view *view)
{
  queue_reset(view, true);
}

void
hikari_view_evacuate(struct hikari_view *view, struct hikari_sheet *sheet)
{
#ifndef NDEBUG
  printf("EVACUATE VIEW %p\n", view);
#endif

  clear_focus(view);

  view->output = sheet->workspace->output;
  view->sheet = sheet;

  /* Action purpose: Unconditionally move the view's list links to the new
  sheet and output before evaluating visibility. If the view is hidden, its
  sheet_views and output_views links are NOT removed during hide(), meaning
  they would otherwise be left dangling in the old (potentially destroyed)
  output's lists. */
  wl_list_remove(&view->sheet_views);
  wl_list_insert(&sheet->views, &view->sheet_views);

  wl_list_remove(&view->output_views);
  wl_list_insert(&view->output->views, &view->output_views);

  if (!hikari_view_is_hidden(view)) {
    if (hikari_view_is_forced(view)) {
      move_to_top(view);
    } else {
      raise_view(view);
    }

    if (hikari_sheet_is_visible(sheet)) {
      hikari_view_damage_whole(view);
      hikari_indicator_position(&hikari_server.indicator, view);
    } else {
      if (hikari_view_is_forced(view)) {
        move_to_top(view);
      } else {
        hikari_view_hide(view);
      }
    }
  } else {
    if (hikari_view_is_forced(view)) {
      raise_view(view);
    } else {
      move_to_top(view);
    }
  }

  if (hikari_view_is_tiled(view) || hikari_view_is_maximized(view)) {
    queue_reset(view, false);
  }
}

void
hikari_view_pin_to_sheet(struct hikari_view *view, struct hikari_sheet *sheet)
{
  assert(view != NULL);
  assert(sheet != NULL);
  assert(sheet->workspace->output == view->output);

  if (view->sheet == sheet) {
    assert(!hikari_view_is_hidden(view));

    if (view->sheet->workspace->sheet != sheet && sheet->nr != 0) {
      hikari_view_hide(view);
      hikari_server_cursor_focus();

      move_to_top(view);
    } else {
      hikari_view_raise(view);
      hikari_indicator_position(&hikari_server.indicator, view);
    }
  } else {
    if (hikari_sheet_is_visible(sheet)) {
      view_link_visible(view, sheet->workspace);

      hikari_view_damage_whole(view);
      hikari_indicator_position(&hikari_server.indicator, view);
    } else {
      hikari_view_hide(view);
      hikari_server_cursor_focus();
    }

    // [COMMENT] Action purpose: Move the list link with the pointer. hide()
    // leaves sheet_views linked, so without this the view reports the new sheet
    // while still sitting in the old sheet's list.
    wl_list_remove(&view->sheet_views);
    wl_list_insert(&sheet->views, &view->sheet_views);

    view->sheet = sheet;

    if (hikari_view_is_tiled(view)) {
      queue_reset(view, true);
    } else {
      move_to_top(view);
    }
  }
}

void
hikari_view_center_cursor(struct hikari_view *view)
{
  assert(view != NULL);

  struct hikari_output *output = view->output;
  struct wlr_box *view_geometry = hikari_view_geometry(view);

  struct wlr_box geometry;
  hikari_geometry_constrain_size(
      view_geometry, &output->usable_area, &geometry);

  hikari_cursor_center(&hikari_server.cursor, output, &geometry);
}

void
hikari_view_top_left_cursor(struct hikari_view *view)
{
  assert(view != NULL);

  struct wlr_box *geometry = hikari_view_geometry(view);
  struct hikari_output *output = view->output;

  int x = output->geometry.x + geometry->x;
  int y = output->geometry.y + geometry->y;

  hikari_cursor_warp(&hikari_server.cursor, x, y);
}

void
hikari_view_bottom_right_cursor(struct hikari_view *view)
{
  assert(view != NULL);

  struct wlr_box *geometry = hikari_view_geometry(view);
  struct hikari_output *output = view->output;

  int x = output->geometry.x + geometry->x + geometry->width;
  int y = output->geometry.y + geometry->y + geometry->height;

  hikari_cursor_warp(&hikari_server.cursor, x, y);
}

void
hikari_view_toggle_invisible(struct hikari_view *view)
{
  if (hikari_view_is_invisible(view)) {
    hikari_view_unset_invisible(view);
  } else {
    if (hikari_view_is_tiled(view)) {
      hikari_view_reset_geometry(view);
    }
    hikari_view_set_invisible(view);
  }
}

void
hikari_view_group(struct hikari_view *view, struct hikari_group *group)
{
  assert(view != NULL);
  assert(group != NULL);
  assert(view->group != NULL);

  if (view->group == group) {
    return;
  }

  remove_from_group(view);
  view->group = group;

  /* [COMMENT] Action purpose: raise_view() links the view into the new group's
  visible_views and re-establishes that group's server-visibility aggregate via
  view_link_visible(). The separate increase_group_visiblity() call this
  replaced did only the aggregate half. The view is still flagged visible here
  -- remove_from_group() detaches group bookkeeping without hiding it -- so the
  linkage precondition holds. */
  raise_view(view);

  hikari_view_damage_whole(view);
}

void
hikari_view_exchange(struct hikari_view *from, struct hikari_view *to)
{
  assert(from != NULL);
  assert(to != NULL);

  if (hikari_view_is_dirty(from) || hikari_view_is_dirty(to)) {
    return;
  }

  assert(from->tile != NULL);
  assert(to->tile != NULL);
  assert(to->tile->view->sheet == from->tile->view->sheet);

  struct hikari_layout *layout = from->sheet->workspace->sheet->layout;

  struct wlr_box *from_geometry = &from->tile->tile_geometry;
  struct wlr_box *to_geometry = &to->tile->tile_geometry;

  struct hikari_tile *from_tile = hikari_malloc(sizeof(struct hikari_tile));
  struct hikari_tile *to_tile = hikari_malloc(sizeof(struct hikari_tile));
  assert(from_tile != NULL);
  assert(to_tile != NULL);

  hikari_tile_init(from_tile, from, layout, to_geometry, to_geometry);
  hikari_tile_init(to_tile, to, layout, from_geometry, from_geometry);

  wl_list_insert(&from->tile->layout_tiles, &to_tile->layout_tiles);
  wl_list_insert(&to->tile->layout_tiles, &from_tile->layout_tiles);

  queue_tile(from, layout, from_tile, true);
  queue_tile(to, layout, to_tile, false);
}

static void
destroy_subsurface_handler(struct wl_listener *listener, void *data)
{
  struct hikari_view_subsurface *view_subsurface =
      wl_container_of(listener, view_subsurface, destroy);

  hikari_view_subsurface_fini(view_subsurface);

  hikari_free(view_subsurface);
}

// [COMMENT] Function purpose: hikari_view_child.fini implementation for
// hikari_view_subsurface, dispatched generically from hikari_view_unmap()'s
// teardown loop over view->children.
static void
subsurface_child_fini(struct hikari_view_child *view_child)
{
  struct hikari_view_subsurface *view_subsurface =
      (struct hikari_view_subsurface *)view_child;

  hikari_view_subsurface_fini(view_subsurface);

  hikari_free(view_subsurface);
}

void
hikari_view_subsurface_init(struct hikari_view_subsurface *view_subsurface,
    struct hikari_view *parent,
    struct wlr_subsurface *subsurface)
{
  view_subsurface->subsurface = subsurface;

  view_subsurface->destroy.notify = destroy_subsurface_handler;
  wl_signal_add(
      &subsurface->surface->events.destroy, &view_subsurface->destroy);

  hikari_view_child_init((struct hikari_view_child *)view_subsurface,
      parent,
      subsurface->surface,
      subsurface_child_fini);
}

void
hikari_view_child_fini(struct hikari_view_child *view_child)
{
  wl_list_remove(&view_child->link);
  wl_list_remove(&view_child->commit.link);
  wl_list_remove(&view_child->new_subsurface.link);
}

void
hikari_view_subsurface_fini(struct hikari_view_subsurface *view_subsurface)
{
  hikari_view_child_fini(&view_subsurface->view_child);
  wl_list_remove(&view_subsurface->destroy.link);
}

static void
damage_surface(struct wlr_surface *surface, int sx, int sy, void *data)
{
  struct hikari_damage_data *damage_data = data;
  struct hikari_output *output = damage_data->output;

  if (damage_data->whole) {
    damage_whole_surface(surface, sx, sy, data);
  } else {
    struct wlr_box *geometry = damage_data->geometry;

    hikari_output_add_effective_surface_damage(
        output, surface, geometry->x + sx, geometry->y + sy);
  }
}

static void
damage_single_surface(struct wlr_surface *surface, int sx, int sy, void *data)
{
  struct hikari_damage_data *damage_data = data;

  if (damage_data->surface == surface) {
    damage_surface(surface, sx, sy, data);
  }
}

static void
commit_child_handler(struct wl_listener *listener, void *data)
{
  struct hikari_view_child *view_child =
      wl_container_of(listener, view_child, commit);

  struct hikari_view *parent = view_child->parent;

  if (!hikari_view_is_hidden(parent)) {
    struct wlr_surface *surface = view_child->surface;

    hikari_view_damage_surface(parent, surface, false);
  }
}

// Function purpose: Allocate and initialise a hikari_view_subsurface for a
// subsurface discovered under an already-tracked child (a subsurface's own
// subsurface, or a popup's subsurface) -- the shared entry point used by
// new_subsurface_child_handler and hikari_view_child_init's initial walk.
static void
view_subsurface_create(
    struct wlr_subsurface *wlr_subsurface, struct hikari_view *parent)
{
  // [COMMENT] Action purpose: Graceful-degradation allocation -- see
  // new_subsurface_handler and DECISIONS_LOG Finding 4. Nested subsurfaces
  // (a subsurface's or popup's own subsurfaces) go through this shared path.
  struct hikari_view_subsurface *view_subsurface =
      hikari_try_malloc(sizeof(struct hikari_view_subsurface));

  if (view_subsurface == NULL) {
    return;
  }

  hikari_view_subsurface_init(view_subsurface, parent, wlr_subsurface);
}

static void
new_subsurface_child_handler(struct wl_listener *listener, void *data)
{
  struct hikari_view_child *view_child =
      wl_container_of(listener, view_child, new_subsurface);

  struct wlr_subsurface *wlr_subsurface = data;

  view_subsurface_create(wlr_subsurface, view_child->parent);
}

// Function purpose: Shared initialisation for every hikari_view_child kind
// (subsurface or popup) -- wire up its commit/new_subsurface listeners, link
// it into the parent view's children list, and recursively track any
// subsurfaces it already has.
void
hikari_view_child_init(struct hikari_view_child *view_child,
    struct hikari_view *parent,
    struct wlr_surface *surface,
    void (*fini)(struct hikari_view_child *))
{
  view_child->parent = parent;
  view_child->surface = surface;
  view_child->fini = fini;

  view_child->new_subsurface.notify = new_subsurface_child_handler;
  wl_signal_add(&surface->events.new_subsurface, &view_child->new_subsurface);

  view_child->commit.notify = commit_child_handler;
  wl_signal_add(&surface->events.commit, &view_child->commit);

  wl_list_insert(&parent->children, &view_child->link);

  struct wlr_subsurface *subsurface;
  wl_list_for_each (
      subsurface, &surface->current.subsurfaces_below, current.link) {
    view_subsurface_create(subsurface, parent);
  }
  wl_list_for_each (
      subsurface, &surface->current.subsurfaces_above, current.link) {
    view_subsurface_create(subsurface, parent);
  }
}

// [COMMENT] Function purpose: Damage a single surface of the view granularly --
// either its full buffer extents (whole) or its committed damage at its exact
// view-relative position -- unified for CSD and SSD views; no whole-output
// fallback.
void
hikari_view_damage_surface(
    struct hikari_view *view, struct wlr_surface *surface, bool whole)
{
  assert(view != NULL);

  struct hikari_damage_data damage_data;

  damage_data.geometry = hikari_view_geometry(view);
  damage_data.output = view->output;
  damage_data.surface = surface;
  damage_data.whole = whole;
  damage_data.view = view;

  hikari_node_for_each_surface(
      (struct hikari_node *)view, damage_single_surface, &damage_data);
}

// [COMMENT] Function purpose: Refresh view geometry and position its scene node.
void
hikari_view_refresh_geometry(struct hikari_view *view, struct wlr_box *geometry)
{
  struct wlr_box *new_geometry = refresh_geometry(view);

  memcpy(new_geometry, geometry, sizeof(struct wlr_box));

  view->current_geometry = new_geometry;
  view->current_unmaximized_geometry = refresh_unmaximized_geometry(view);
  
  // [COMMENT] Action purpose: Guard against BOTH a missing scene node and a
  // missing output before updating position. view->output is NULL between
  // hikari_view_init and hikari_view_configure, but scene_node is already set
  // by then (hikari_xdg_view_init assigns it at new_toplevel time). first_map
  // calls this function in that window, so guarding only on scene_node
  // dereferences a NULL output on every single window creation. The position
  // is recomputed once the view is configured onto an output.
  if (view->scene_node != NULL && view->output != NULL) {
    wlr_scene_node_set_position(view->scene_node,
        new_geometry->x + view->output->geometry.x,
        new_geometry->y + view->output->geometry.y);
  }

  refresh_border_geometry(view);
}

static void
commit_operation(struct hikari_operation *operation, struct hikari_view *view)
{
  switch (operation->type) {
    case HIKARI_OPERATION_TYPE_RESIZE:
      commit_resize(view, operation);
      break;

    case HIKARI_OPERATION_TYPE_RESET:
      commit_reset(view, operation);
      break;

    case HIKARI_OPERATION_TYPE_UNMAXIMIZE:
      commit_unmaximize(view, operation);
      break;

    case HIKARI_OPERATION_TYPE_FULL_MAXIMIZE:
      commit_full_maximize(view, operation);
      break;

    case HIKARI_OPERATION_TYPE_VERTICAL_MAXIMIZE:
      commit_vertical_maximize(view, operation);
      break;

    case HIKARI_OPERATION_TYPE_HORIZONTAL_MAXIMIZE:
      commit_horizontal_maximize(view, operation);
      break;

    case HIKARI_OPERATION_TYPE_TILE:
      commit_tile(view, operation);
      break;
  }
}

void
hikari_view_commit_pending_operation(
    struct hikari_view *view, struct wlr_box *geometry)
{
  assert(view != NULL);
  assert(hikari_view_is_dirty(view));

  view->pending_operation.geometry.width = geometry->width;
  view->pending_operation.geometry.height = geometry->height;

  if (!hikari_view_is_hidden(view)) {
    hikari_indicator_position(&hikari_server.indicator, view);
  }
  hikari_view_damage_whole(view);

  commit_operation(&view->pending_operation, view);
  hikari_view_unset_dirty(view);
}

void
hikari_view_activate(struct hikari_view *view, bool active)
{
  assert(view != NULL);

  if (view->activate) {
    if (view->border.state != HIKARI_BORDER_NONE) {
      view->border.state =
          active ? HIKARI_BORDER_ACTIVE : HIKARI_BORDER_INACTIVE;
    }
    view->activate(view, active);
  }
}

static void
migrate_view(struct hikari_view *view, struct hikari_sheet *sheet, bool center)
{
  assert(hikari_view_is_hidden(view));

  view->output = sheet->workspace->output;
  view->sheet = sheet;


  move_to_top(view);

  queue_reset(view, center);
}

void
hikari_view_migrate(struct hikari_view *view,
    struct hikari_sheet *sheet,
    int x,
    int y,
    bool center)
{
  struct hikari_output *output = sheet->workspace->output;
  struct wlr_box *view_geometry = hikari_view_geometry(view);

  hikari_indicator_position(&hikari_server.indicator, view);
  hikari_view_damage_whole(view);

  // only remove view from lists and do not make it lose focus by calling
  // `hikari_view_hide`.
  view_unlink_visible(view);

  hikari_geometry_constrain_relative(
      &view->geometry, &output->usable_area, x, y);
  hikari_geometry_constrain_relative(view_geometry, &output->usable_area, x, y);

  migrate_view(view, sheet, center);

#ifdef HAVE_XWAYLAND
  if (view->move != NULL) {
    view->move(view, view_geometry->x, view_geometry->y);
  }
#endif

  if (hikari_view_is_hidden(view)) {
    hikari_view_show(view);
  }
}

void
hikari_view_configure(struct hikari_view *view,
    const char *app_id,
    struct hikari_view_config *view_config)
{
  assert(view->id == NULL);

  struct hikari_sheet *sheet;
  struct hikari_output *output;
  struct wlr_box *geometry = &view->geometry;
  int x, y;
  bool invisible, floating, publicview;

  set_app_id(view, app_id);

  if (view_config != NULL) {
    struct hikari_view_properties *properties =
        hikari_view_config_resolve_properties(view_config, view->child);

    sheet = hikari_view_properties_resolve_sheet(properties);
    output = sheet->workspace->output;

    invisible = properties->invisible;
    floating = properties->floating;
    publicview = properties->publicview;

    hikari_view_properties_resolve_position(properties, view, &x, &y);
  } else {
    sheet = hikari_server.workspace->sheet;
    output = sheet->workspace->output;

    invisible = false;
    floating = false;
    publicview = false;

    x = hikari_server.cursor.wlr_cursor->x - output->geometry.x;
    y = hikari_server.cursor.wlr_cursor->y - output->geometry.y;
  }

  view->sheet = sheet;

  view->output = output;

  wl_list_init(&view->workspace_views);
  wl_list_init(&view->visible_server_views);

  if (invisible) {
    hikari_view_set_invisible(view);
  }

  if (floating) {
    hikari_view_set_floating(view);
  }

  if (publicview) {
    hikari_view_set_public(view);
  }

  hikari_geometry_constrain_absolute(geometry, &output->usable_area, x, y);
  hikari_view_refresh_geometry(view, geometry);
}
