/* [COMMENT] Script function and purpose: Abstract view interface definitions and view management operations for windows and surfaces in hikari. */

#if !defined(HIKARI_VIEW_H)
#define HIKARI_VIEW_H

#include <assert.h>

#include <stdbool.h>
#include <stdint.h>

#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/box.h>

#include <hikari/animation.h>
#include <hikari/border.h>
#include <hikari/foreign_toplevel.h>
#include <hikari/group.h>
#include <hikari/indicator_frame.h>
#include <hikari/maximized_state.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>

#include <hikari/node.h>
#include <hikari/operation.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/sheet.h>
#include <hikari/tile.h>
#include <hikari/workspace.h>

struct hikari_mark;

struct hikari_view;

/* Manages client-side vs server-side window decoration bindings for a view. */
struct hikari_view_decoration {
  struct wlr_server_decoration *wlr_decoration;
  struct hikari_view *view;
  struct wl_listener mode;
  struct wl_listener destroy;
};

/* Base representation of a managed window view, encapsulating surface state, geometry, sheet, group, mark, and tile associations. */
struct hikari_view {

  struct hikari_node node;
  struct hikari_sheet *sheet;
  struct hikari_group *group;
  struct hikari_mark *mark;
  struct hikari_output *output;
  struct wlr_surface *surface;
  struct wlr_scene_node *scene_node;

  bool use_csd;
  bool child;
  uint16_t flags;
  char *title;
  char *id;

  /* [COMMENT] Class purpose: This view's entry in the foreign-toplevel list,
  which is what a dock or panel enumerates. Owned: created when the view maps,
  destroyed when it unmaps, so an unmapped window correctly disappears from a
  taskbar. NULL whenever the view is not mapped. `id` doubles as the app_id
  published through it. */
  struct wlr_ext_foreign_toplevel_handle_v1 *foreign_toplevel;

  /* [COMMENT] Class purpose: This view's zwlr_foreign_toplevel_management_v1
  handle and the listeners for the requests that arrive through it -- the half
  that lets an external window switcher FOCUS, close or minimise this window
  rather than merely list it. Same lifetime as the handle above: created on map,
  destroyed on unmap. See src/foreign_toplevel.c. */
  struct hikari_foreign_toplevel foreign_toplevel_management;

  /* [COMMENT] Class purpose: The spill crop currently applied to this view's
  surface tree, and whether one is applied at all.

  Cached rather than recomputed-and-reapplied, because the crop is refreshed
  from refresh_border_geometry(), which runs on EVERY pointer motion event of a
  drag. wlr_scene_subsurface_tree_set_clip() walks the whole subsurface tree, so
  reapplying an unchanged crop at pointer rate costs a tree walk per event on
  exactly the windows that can least afford it -- a browser has many
  subsurfaces, and a window larger than its output damages a correspondingly
  larger region each time. Valid only while `spill_clipped` is true. */
  struct wlr_box spill_clip;
  bool spill_clipped;

  struct hikari_border border;
  struct hikari_indicator_frame indicator_frame;
  struct hikari_tile *tile;

  /* [COMMENT] Class purpose: In-flight position interpolation for this view's
  scene node. Inert -- and every path behaves exactly as it did before it
  existed -- unless `ui { animation { enabled = true } }` is set. See
  include/hikari/animation.h. */
  struct hikari_animation animation;

  struct wlr_box geometry;
  struct hikari_maximized_state *maximized_state;

  /* [COMMENT] Class purpose: The whole-output box a fullscreen view occupies,
  valid only while the fullscreen flag is set. Held by value rather than behind
  a pointer like maximized_state: there is exactly one per view, it is cheap,
  and it has no lifetime to get wrong on any of the teardown paths BLUEPRINT
  section 15 enumerates. */
  struct wlr_box fullscreen_geometry;

  struct wl_list output_views;
  struct wl_list workspace_views;
  struct wl_list sheet_views;
  struct wl_list group_views;
  struct wl_list visible_group_views;
  struct wl_list visible_server_views;
  struct wl_list children;

  struct hikari_operation pending_operation;

  struct wlr_box *current_geometry;
  struct wlr_box *current_unmaximized_geometry;

  struct hikari_view_decoration decoration;

  uint32_t (*resize)(struct hikari_view *, int, int);
#ifdef HAVE_XWAYLAND
  void (*move)(struct hikari_view *, int, int);
  void (*move_resize)(struct hikari_view *, int, int, int, int);
#endif
  void (*activate)(struct hikari_view *, bool);
  void (*quit)(struct hikari_view *);
  void (*constraints)(struct hikari_view *, int *, int *, int *, int *);

  struct wl_listener new_subsurface;
};

struct hikari_view_child {
  struct wl_list link;

  struct wlr_surface *surface;
  struct hikari_view *parent;

  struct wl_listener commit;
  struct wl_listener new_subsurface;

  /* [COMMENT] Action purpose: Concrete-type teardown, set by whichever
  initializer (hikari_view_subsurface_init, xdg_popup_create) embeds this
  struct as its first member. hikari_view_unmap() walks every entry in
  view->children generically through this pointer instead of assuming every
  entry is a hikari_view_subsurface -- hikari_xdg_popup is also linked into
  the same list via this shared prefix, and its layout diverges after this
  point, so a hardcoded cast there was reading/freeing the wrong fields. */
  void (*fini)(struct hikari_view_child *);
};

void
hikari_view_child_init(struct hikari_view_child *view_child,
    struct hikari_view *parent,
    struct wlr_surface *surface,
    void (*fini)(struct hikari_view_child *));

void
hikari_view_child_fini(struct hikari_view_child *view_child);

struct hikari_view_subsurface {
  struct hikari_view_child view_child;

  struct wlr_subsurface *subsurface;

  struct wl_listener destroy;
};

void
hikari_view_subsurface_init(struct hikari_view_subsurface *view_subsurface,
    struct hikari_view *parent,
    struct wlr_subsurface *subsurface);

void
hikari_view_subsurface_fini(struct hikari_view_subsurface *view_subsurface);

#define FLAG(name, shift)                                                      \
  static const uint16_t hikari_view_##name##_flag = (uint16_t)(1U << (shift)); \
                                                                               \
  static inline bool hikari_view_is_##name(struct hikari_view *view)           \
  {                                                                            \
    assert(view != NULL);                                                      \
    return (view->flags & hikari_view_##name##_flag);                          \
  }                                                                            \
                                                                               \
  static inline void hikari_view_set_##name(struct hikari_view *view)          \
  {                                                                            \
    assert(view != NULL);                                                      \
    view->flags |= hikari_view_##name##_flag;                                  \
  }                                                                            \
                                                                               \
  static inline void hikari_view_unset_##name(struct hikari_view *view)        \
  {                                                                            \
    assert(view != NULL);                                                      \
    view->flags &= ~hikari_view_##name##_flag;                                 \
  }

FLAG(hidden, 0UL)
FLAG(invisible, 1UL)
FLAG(floating, 2UL)
FLAG(public, 3UL)
FLAG(forced, 4UL)

/* [COMMENT] Action purpose: Genuine fullscreen, set only from a client's own
protocol request. A flag rather than a fourth `enum hikari_maximization` member
on purpose: that enum is switched on exhaustively in eight places, so a new
member would force every one of them to change, whereas a flag touches only the
paths that opt in. `flags` is uint16_t and bits 0-4 were the only ones in use.

The flag SHADOWS the maximization state rather than replacing it --
refresh_geometry() tests it above maximized_state, and maximized_state is left
untouched while fullscreen. That is what makes leaving fullscreen restore a
maximized, tiled or floating window with no bookkeeping: the shadow lifts and
whatever was underneath is still there. See BLUEPRINT section 17. */
FLAG(fullscreen, 5UL)
#undef FLAG

void
hikari_view_init(
    struct hikari_view *view, bool child, struct hikari_workspace *workspace);

void
hikari_view_fini(struct hikari_view *view);

void
hikari_view_set_title(struct hikari_view *view, const char *title);

#define VIEW_ACTION(name) void hikari_view_##name(struct hikari_view *view);

VIEW_ACTION(show)
VIEW_ACTION(hide)
VIEW_ACTION(raise)
VIEW_ACTION(lower)
VIEW_ACTION(toggle_full_maximize)
VIEW_ACTION(toggle_vertical_maximize)
VIEW_ACTION(toggle_horizontal_maximize)
VIEW_ACTION(toggle_floating)
VIEW_ACTION(damage_whole)
VIEW_ACTION(top_left_cursor)
VIEW_ACTION(bottom_right_cursor)
VIEW_ACTION(center_cursor)
VIEW_ACTION(toggle_invisible)
VIEW_ACTION(toggle_public)
VIEW_ACTION(reset_geometry)
#undef VIEW_ACTION

/* [COMMENT] Function purpose: THE entry point for genuine fullscreen. Every
protocol path -- xdg-shell request_fullscreen, XWayland request_fullscreen, and
zwlr_foreign_toplevel_management set_fullscreen -- goes through this and nothing
else.

Takes a desired STATE rather than toggling, because that is what a client sends.
Guarding a toggle by first comparing against the current state is precisely how
the xdg-shell path came to compare against the *maximization* state as a proxy
and silently no-op on every maximized window.

Named `request_` and not `set_`: FLAG(fullscreen, ...) above already generates
`hikari_view_set_fullscreen(view)` as the raw bit setter, and the two must not
be confused. This one performs the whole transition -- geometry, borders, the
top bar -- while that one flips a bit. The name also says the honest thing: this
answers a request, and may decline it (unmapped, hidden, or mid-resize).

Not a VIEW_ACTION: those are void(view) so they can be bound to keys, and this
deliberately cannot be. hikari binds no key to fullscreen -- applications
already own F11. */
void
hikari_view_request_fullscreen(struct hikari_view *view, bool fullscreen);

void
hikari_view_map(struct hikari_view *view, struct wlr_surface *surface);

void
hikari_view_unmap(struct hikari_view *view);

void
hikari_view_commit_pending_operation(
    struct hikari_view *view, struct wlr_box *geometry);

void
hikari_view_evacuate(struct hikari_view *view, struct hikari_sheet *sheet);

void
hikari_view_pin_to_sheet(struct hikari_view *view, struct hikari_sheet *sheet);

void
hikari_view_group(struct hikari_view *view, struct hikari_group *group);

void
hikari_view_move_resize(
    struct hikari_view *view, int x, int y, int width, int height);

void
hikari_view_move(struct hikari_view *view, int x, int y);

void
hikari_view_move_bottom_left(struct hikari_view *view);

void
hikari_view_move_bottom_middle(struct hikari_view *view);

void
hikari_view_move_bottom_right(struct hikari_view *view);

void
hikari_view_move_center_left(struct hikari_view *view);

void
hikari_view_move_center(struct hikari_view *view);

void
hikari_view_move_center_right(struct hikari_view *view);

void
hikari_view_move_top_left(struct hikari_view *view);

void
hikari_view_move_top_middle(struct hikari_view *view);

void
hikari_view_move_top_right(struct hikari_view *view);

void
hikari_view_move_absolute(struct hikari_view *view, int x, int y);

void
hikari_view_resize_absolute(struct hikari_view *view, int x, int y);

void
hikari_view_resize(struct hikari_view *view, int dx, int dy);

void
hikari_view_assign_sheet(struct hikari_view *view, struct hikari_sheet *sheet);

void
hikari_view_tile(
    struct hikari_view *view, struct wlr_box *geometry, bool center);

void
hikari_view_exchange(struct hikari_view *from, struct hikari_view *to);

void
hikari_view_damage_surface(
    struct hikari_view *view, struct wlr_surface *surface, bool whole);

void
hikari_view_refresh_geometry(
    struct hikari_view *view, struct wlr_box *geometry);

/* [COMMENT] Function purpose: Re-evaluate whether this view may overhang its
screen, without changing its geometry.

Needed because the answer depends on the server's MODE as well as the view's
position: under the default `ui { spill = drag }` a window may overhang while it
is being dragged and is cropped once it is let go. Ending a drag changes no
geometry, so nothing on the geometry path would notice. */
void
hikari_view_refresh_spill_clip(struct hikari_view *view);

void
hikari_view_activate(struct hikari_view *view, bool active);

void
hikari_view_migrate(struct hikari_view *view,
    struct hikari_sheet *sheet,
    int x,
    int y,
    bool center);

void
hikari_view_configure(struct hikari_view *view,
    const char *app_id,
    struct hikari_view_config *view_config);

static inline bool
hikari_view_is_dirty(struct hikari_view *view)
{
  assert(view != NULL);
  return view->pending_operation.dirty;
}

static inline void
hikari_view_set_dirty(struct hikari_view *view)
{
  assert(view != NULL);
  view->pending_operation.dirty = true;
}

static inline void
hikari_view_unset_dirty(struct hikari_view *view)
{
  assert(view != NULL);
  view->pending_operation.dirty = false;
}

static inline bool
hikari_view_was_updated(struct hikari_view *view, uint32_t serial)
{
  assert(view != NULL);
  return hikari_view_is_dirty(view) && serial >= view->pending_operation.serial;
}

static inline void
hikari_view_quit(struct hikari_view *view)
{
  assert(view != NULL);
  if (view->quit) {
    view->quit(view);
  }
}

static inline bool
hikari_view_is_fully_maximized(struct hikari_view *view)
{
  assert(view != NULL);
  return view->maximized_state != NULL &&
         view->maximized_state->maximization ==
             HIKARI_MAXIMIZATION_FULLY_MAXIMIZED;
}

static inline void
hikari_view_for_each_surface(
    struct hikari_view *view, wlr_surface_iterator_func_t func, void *data)
{
  assert(view != NULL);
  assert(view->surface != NULL);

  wlr_surface_for_each_surface(view->surface, func, data);
}

static inline struct wlr_box *
hikari_view_geometry(struct hikari_view *view)
{
  assert(view != NULL);
  return view->current_geometry;
}

static inline struct wlr_box *
hikari_view_border_geometry(struct hikari_view *view)
{
  assert(view != NULL);
  return &view->border.geometry;
}

static inline bool
hikari_view_is_focus_view(struct hikari_view *view)
{
  return view->sheet->workspace->focus_view == view;
}

static inline bool
hikari_view_has_focus(struct hikari_view *view)
{
  assert(view != NULL);
  return hikari_server.workspace->focus_view == view;
}

static inline bool
hikari_view_wants_border(struct hikari_view *view)
{
  assert(view != NULL);
  return view->border.state != HIKARI_BORDER_NONE;
}

static inline bool
hikari_view_is_tiled(struct hikari_view *view)
{
  assert(view != NULL);
  return view->tile != NULL;
}

static inline bool
hikari_view_is_tileable(struct hikari_view *view)
{
  return !hikari_view_is_floating(view) && !hikari_view_is_invisible(view) &&
         !hikari_view_is_dirty(view);
}

static inline bool
hikari_view_is_mapped(struct hikari_view *view)
{
  assert(view != NULL);
  return view->surface != NULL;
}

static inline bool
hikari_view_is_maximized(struct hikari_view *view)
{
  return view->maximized_state != NULL;
}

static inline bool
hikari_view_is_unmanaged(struct hikari_view *view)
{
  return view->sheet == NULL;
}

static inline bool
hikari_view_is_tiling(struct hikari_view *view)
{
  return hikari_view_is_dirty(view) &&
         view->pending_operation.type == HIKARI_OPERATION_TYPE_TILE;
}

static inline void
hikari_view_damage_border(struct hikari_view *view)
{
  if (view->output == NULL) {
    return;
  }

  struct wlr_box *geometry = hikari_view_border_geometry(view);

  hikari_output_add_damage(view->output, geometry);
}

#endif
