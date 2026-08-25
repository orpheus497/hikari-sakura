// [COMMENT] Script function and purpose: The deferred re-tiling queue. See
// include/hikari/reflow.h for why re-tiling cannot be done synchronously.

#include <hikari/reflow.h>

#include <assert.h>
#include <stdbool.h>

#include <wayland-server-core.h>

#include <hikari/configuration.h>
#include <hikari/layout.h>
#include <hikari/layout_policy.h>
#include <hikari/server.h>
#include <hikari/sheet.h>
#include <hikari/split.h>
#include <hikari/view.h>
#include <hikari/workspace.h>

/* [COMMENT] Action purpose: Self-referential static initialisation, so there is
no hikari_reflow_init() for a caller to forget and no ordering constraint
against server startup. A wl_list is empty exactly when both pointers name the
head itself, and the address of a static object is an address constant, so this
is a valid initialiser. */
static struct wl_list pending_sheets = { .prev = &pending_sheets,
  .next = &pending_sheets };

/* [COMMENT] Class purpose: The armed idle source, or NULL. libwayland destroys
an idle source as part of dispatching it, so this pointer is dangling the moment
drain() is entered -- drain() clears it first thing, before doing any work. */
static struct wl_event_source *idle_source = NULL;

// [COMMENT] Function purpose: True while the sheet holds a view that a reflow
// would try to tile but cannot, because it has a geometry operation in flight.
static bool
sheet_is_settling(struct hikari_sheet *sheet)
{
  struct hikari_view *view;

  wl_list_for_each (view, &sheet->views, sheet_views) {
    /* [COMMENT] Action purpose: Mirror hikari_view_is_tileable() minus its
    dirty test. A dirty view that would never have been tiled anyway -- floating
    or invisible -- must not hold the whole sheet back, or a single floating
    window being dragged would suspend automatic tiling for as long as the drag
    lasts. */
    if (hikari_view_is_dirty(view) && !hikari_view_is_floating(view) &&
        !hikari_view_is_invisible(view)) {
      return true;
    }
  }

  return false;
}

static void
dequeue(struct hikari_sheet *sheet)
{
  wl_list_remove(&sheet->reflow_pending);
  wl_list_init(&sheet->reflow_pending);
}

// [COMMENT] Function purpose: Perform the re-tile. Only ever called on a
// visible, settled sheet.
static void
reflow(struct hikari_sheet *sheet)
{
  struct hikari_layout_policy *policy = &hikari_configuration->layout_policy;
  struct hikari_layout *layout = sheet->layout;

  /* [COMMENT] Action purpose: An existing layout is EXTENDED, not rebuilt.
  hikari_layout_restack_{append,prepend}() fold every untiled sheet view into
  the layout while leaving the order of the already-tiled ones alone, which is
  precisely the semantics the manual bindings of the same name offer -- and it
  is what stops an automatic reflow from shuffling the user's arrangement every
  time a dialog opens. They also unhide views on the way in, which is the
  agreed behaviour for a hidden view rejoining a layout. */
  if (layout != NULL) {
    switch (policy->insert) {
      case HIKARI_LAYOUT_INSERT_PREPEND:
        hikari_layout_restack_prepend(layout);
        break;

      case HIKARI_LAYOUT_INSERT_APPEND:
        hikari_layout_restack_append(layout);
        break;
    }

    return;
  }

  /* [COMMENT] Action purpose: No layout yet. Adopt whichever the policy names,
  and do nothing at all when it names none -- a sheet the user has not given a
  layout stays stacking, which is hikari's whole premise. */
  struct hikari_split *split =
      hikari_layout_policy_split(policy, hikari_configuration, sheet);

  if (split != NULL) {
    hikari_sheet_apply_split(sheet, split);
  }
}

static void
drain(void *data)
{
  /* [COMMENT] Action purpose: Cleared FIRST. libwayland removes and frees an
  idle source as part of dispatching it, so `idle_source` is dangling on entry;
  anything below that re-arms (reflow() commits geometry, which settles views,
  which calls back into hikari_reflow_settle()) must see NULL rather than the
  stale pointer. */
  idle_source = NULL;

  struct hikari_sheet *sheet, *sheet_temp;

  wl_list_for_each_safe (
      sheet, sheet_temp, &pending_sheets, reflow_pending) {
    /* [COMMENT] Action purpose: A sheet nobody is looking at is dropped rather
    than laid out. reflow() unhides views, and unhiding the views of a sheet the
    user has switched away from would draw them over the sheet that is actually
    displayed. The request is not lost: display_sheet() schedules a fresh one
    when the sheet comes back.

    The same drop covers lock mode, and there it is not merely cosmetic. Lock
    mode FORCES every view -- linked into the visible lists while flagged hidden
    -- and hikari_view_show() asserts !hikari_view_is_forced(view), so unhiding
    one from here would abort the compositor on a locked screen, or corrupt the
    visibility linkage under -DNDEBUG. This is the hazard Phase 89 gated with
    can_act() and Phase 90 found again in src/ipc.c; a re-tile scheduled while
    locked is dropped rather than deferred, because the arrangement the user
    left behind is already laid out and a window that mapped during the lock is
    one manual tiling action away. */
    if (hikari_server_in_lock_mode() || !hikari_sheet_is_visible(sheet)) {
      dequeue(sheet);
      continue;
    }

    /* [COMMENT] Action purpose: Left QUEUED, deliberately, and not re-armed
    here -- re-arming an idle source from inside an idle handler is a busy loop
    that starves the event loop while a client takes its time acking a configure.
    hikari_reflow_settle() re-arms when that ack actually lands. */
    if (sheet_is_settling(sheet)) {
      continue;
    }

    dequeue(sheet);
    reflow(sheet);
  }
}

static void
arm(void)
{
  if (idle_source != NULL || hikari_server.event_loop == NULL) {
    return;
  }

  idle_source = wl_event_loop_add_idle(hikari_server.event_loop, drain, NULL);
}

void
hikari_reflow_schedule(struct hikari_sheet *sheet)
{
  assert(sheet != NULL);

  /* [COMMENT] Action purpose: The policy gate lives here rather than at each
  call site, so map, unmap and sheet display can all call unconditionally and
  none of them can be forgotten when the policy grows a member. */
  if (hikari_configuration == NULL ||
      !hikari_configuration->layout_policy.automatic) {
    return;
  }

  // [COMMENT] Action purpose: An unqueued sheet's link points at itself; a
  // queued one points into pending_sheets. So this is the queued test, and it
  // is what makes repeated scheduling free.
  if (!wl_list_empty(&sheet->reflow_pending)) {
    return;
  }

  wl_list_insert(&pending_sheets, &sheet->reflow_pending);
  arm();
}

void
hikari_reflow_settle(void)
{
  if (wl_list_empty(&pending_sheets)) {
    return;
  }

  arm();
}

void
hikari_reflow_cancel(struct hikari_sheet *sheet)
{
  assert(sheet != NULL);

  if (!wl_list_empty(&sheet->reflow_pending)) {
    dequeue(sheet);
  }
}

void
hikari_reflow_fini(void)
{
  if (idle_source != NULL) {
    wl_event_source_remove(idle_source);
    idle_source = NULL;
  }

  /* [COMMENT] Action purpose: Unlink every remaining sheet. The queue links
  through storage owned by the workspaces, which are torn down around this
  point, so leaving entries linked leaves the static head pointing into freed
  memory. */
  struct hikari_sheet *sheet, *sheet_temp;
  wl_list_for_each_safe (
      sheet, sheet_temp, &pending_sheets, reflow_pending) {
    dequeue(sheet);
  }
}
