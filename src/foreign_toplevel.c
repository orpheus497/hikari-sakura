/* [COMMENT] Script function and purpose: Implementation of
zwlr_foreign_toplevel_management_v1 -- one handle per mapped view, plus the
handlers for the requests an external window switcher, dock or task manager
sends through it.

Everything wlroots-dependent lives behind HAVE_FOREIGN_TOPLEVEL_MANAGEMENT so
the whole protocol can be compiled out from the Makefile without threading a
conditional through every call site in view.c; the stubs at the bottom of this
file keep those call sites unconditional. */

#include <hikari/foreign_toplevel.h>

#ifdef HAVE_FOREIGN_TOPLEVEL_MANAGEMENT

#include <assert.h>

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/util/log.h>

#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/sheet.h>
#include <hikari/view.h>
#include <hikari/workspace.h>

/* [COMMENT] Function purpose: The gate every client-driven request passes
through, and the single most important thing in this file.

hikari is modal, and hikari_workspace_focus_view() opens with
`assert(hikari_server_in_normal_mode())`. A foreign-toplevel request is driven
by an external client, so it arrives whenever that client chooses to send it --
including while the user is mid-drag in move or resize mode, in mark-select, or
on a locked screen. A request that reached the focus machinery from any other
mode would abort a debug build outright and corrupt hikari's visibility linkage
under NDEBUG. None of this applies to ext-foreign-toplevel-list-v1, which is
read-only, which is why the hazard appears only now.

Lock mode needs no separate test because lock mode IS a mode: while the screen
is locked hikari_server_in_normal_mode() is false, so nothing outside the
compositor can focus, close, minimise or maximise a window. Close is gated too
even though it only forwards a request to the client, so that a locked screen
cannot be used to close windows.

An unmapped view has no handle, so a request cannot normally reach one at all;
the mapped test is belt-and-braces against a request queued in the same event
loop iteration as the unmap. */
static bool
can_act(struct hikari_foreign_toplevel *foreign_toplevel)
{
  struct hikari_view *view = foreign_toplevel->view;

  return hikari_server_in_normal_mode() && hikari_view_is_mapped(view) &&
         !hikari_view_is_forced(view);
}

/* [COMMENT] Function purpose: Bring a view onto the screen and give it focus,
whatever sheet or output it lives on.

This deliberately does NOT call hikari_workspace_focus_view(). hikari's
workspaces are per-output and output focus follows the cursor, so focusing a
view that belongs to another output through that function would leave
hikari_server.workspace naming the old one. The sequence below -- switch the
sheet if the target is not the displayed one, show or raise, centre the cursor,
then let cursor focus resolve the rest -- is the one hikari already uses for
marks in hikari_server_switch_to_mark()/show_marked_view(), which is the same
problem: reach a view that may be anywhere. */
static void
activate_view(struct hikari_view *view)
{
  struct hikari_workspace *workspace = view->sheet->workspace;

  if (workspace->sheet != view->sheet) {
    hikari_workspace_switch_sheet(workspace, view->sheet);
  }

  /* [COMMENT] Action purpose: Re-test rather than assume. Switching sheets
  shows every non-invisible view of the incoming sheet, so the target may
  already be visible by now -- and hikari_view_show() asserts the view is
  hidden. A view flagged invisible stays hidden through the switch and is shown
  explicitly here, which is the behaviour a switcher wants: the user picked it. */
  if (hikari_view_is_hidden(view)) {
    hikari_view_show(view);
  } else {
    hikari_view_raise(view);
  }

  hikari_view_center_cursor(view);
  hikari_server_cursor_focus();
}

static void
request_activate_handler(struct wl_listener *listener, void *data)
{
  struct hikari_foreign_toplevel *foreign_toplevel =
      wl_container_of(listener, foreign_toplevel, request_activate);

  if (!can_act(foreign_toplevel)) {
    return;
  }

  activate_view(foreign_toplevel->view);
}

static void
request_close_handler(struct wl_listener *listener, void *data)
{
  struct hikari_foreign_toplevel *foreign_toplevel =
      wl_container_of(listener, foreign_toplevel, request_close);

  if (!can_act(foreign_toplevel)) {
    return;
  }

  hikari_view_quit(foreign_toplevel->view);
}

/* [COMMENT] Function purpose: Minimise maps onto hikari's `hidden` flag, which
is what hiding a view already means here -- it leaves the workspace but keeps
its sheet, group and mark.

Both directions are guarded on the current state because hikari_view_hide()
asserts the view is not hidden and hikari_view_show() asserts that it is, while
a client is free to send set_minimized twice. Unminimising also has to reach
across sheets, for the same reason activation does. */
static void
request_minimize_handler(struct wl_listener *listener, void *data)
{
  struct hikari_foreign_toplevel *foreign_toplevel =
      wl_container_of(listener, foreign_toplevel, request_minimize);
  struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;

  if (!can_act(foreign_toplevel)) {
    return;
  }

  struct hikari_view *view = foreign_toplevel->view;

  if (event->minimized) {
    if (hikari_view_is_hidden(view)) {
      return;
    }

    hikari_view_hide(view);
    hikari_server_cursor_focus();
  } else {
    if (!hikari_view_is_hidden(view)) {
      return;
    }

    struct hikari_workspace *workspace = view->sheet->workspace;

    if (workspace->sheet != view->sheet) {
      hikari_workspace_switch_sheet(workspace, view->sheet);
    }

    if (hikari_view_is_hidden(view)) {
      hikari_view_show(view);
    }

    hikari_server_cursor_focus();
  }
}

/* [COMMENT] Function purpose: Drive hikari's full-maximize state from a
protocol request.

Maximize means the USABLE area -- the top bar and any layer-shell exclusive zone
stay where they are. It is a different operation from fullscreen below, and the
two are deliberately no longer routed together.

hikari_view_toggle_full_maximize() asserts the view is not hidden and is a
toggle rather than a setter, hence both guards. It returns early on its own when
the view has an operation in flight. */
static void
set_full_maximize(
    struct hikari_foreign_toplevel *foreign_toplevel, bool maximized)
{
  struct hikari_view *view = foreign_toplevel->view;

  if (hikari_view_is_hidden(view) ||
      maximized == hikari_view_is_fully_maximized(view)) {
    return;
  }

  hikari_view_toggle_full_maximize(view);
}

/* [COMMENT] Function purpose: Drive hikari's fullscreen state from a protocol
request -- the WHOLE output, over the top bar.

This used to call set_full_maximize(), because hikari had no fullscreen state to
set: there was only HIKARI_MAXIMIZATION_*, so set_fullscreen and set_maximized
were literally the same operation and a client saw back a state it had not asked
for. That is why TODOS carried "external switchers should expose maximise only"
as a known consequence.

Phase 90 gave the compositor a real fullscreen state, so the two requests are now
distinct. hikari_view_request_fullscreen() is a setter rather than a toggle and
owns every remaining precondition -- mapped, not hidden, no operation in flight,
and already-in-the-requested-state -- so there is nothing to re-derive here. */
static void
set_fullscreen(
    struct hikari_foreign_toplevel *foreign_toplevel, bool fullscreen)
{
  hikari_view_request_fullscreen(foreign_toplevel->view, fullscreen);
}

static void
request_maximize_handler(struct wl_listener *listener, void *data)
{
  struct hikari_foreign_toplevel *foreign_toplevel =
      wl_container_of(listener, foreign_toplevel, request_maximize);
  struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;

  if (!can_act(foreign_toplevel)) {
    return;
  }

  set_full_maximize(foreign_toplevel, event->maximized);
}

static void
request_fullscreen_handler(struct wl_listener *listener, void *data)
{
  struct hikari_foreign_toplevel *foreign_toplevel =
      wl_container_of(listener, foreign_toplevel, request_fullscreen);
  struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;

  if (!can_act(foreign_toplevel)) {
    return;
  }

  set_fullscreen(foreign_toplevel, event->fullscreen);
}

// [COMMENT] Function purpose: Drop every listener and forget the handle. Runs
// exactly once per handle, from whichever side tears it down first.
static void
detach(struct hikari_foreign_toplevel *foreign_toplevel)
{
  wl_list_remove(&foreign_toplevel->request_maximize.link);
  wl_list_remove(&foreign_toplevel->request_minimize.link);
  wl_list_remove(&foreign_toplevel->request_activate.link);
  wl_list_remove(&foreign_toplevel->request_fullscreen.link);
  wl_list_remove(&foreign_toplevel->request_close.link);
  wl_list_remove(&foreign_toplevel->handle_destroy.link);

  foreign_toplevel->handle = NULL;
  foreign_toplevel->published_output = NULL;
}

/* [COMMENT] Function purpose: Ownership of the handle is shared with wlroots --
hikari destroys it on unmap, but wlroots destroys every outstanding handle when
the manager goes away at display teardown. Listening for the handle's own
destroy means neither a double-destroy nor a stale pointer is reachable
whichever side goes first, which is the same shape as the scene-tree ownership
handled in Phase 78 rather than a bet on wlroots' teardown order. */
static void
handle_destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_foreign_toplevel *foreign_toplevel =
      wl_container_of(listener, foreign_toplevel, handle_destroy);

  detach(foreign_toplevel);
}

void
hikari_foreign_toplevel_manager_setup(struct hikari_server *server)
{
  /* [COMMENT] Action purpose: Non-fatal, exactly like the ext-list global next
  to it. Losing the ability to act on windows from outside costs a window
  switcher, not the session. */
  server->foreign_toplevel_manager =
      wlr_foreign_toplevel_manager_v1_create(server->display);

  if (server->foreign_toplevel_manager == NULL) {
    wlr_log(WLR_ERROR,
        "could not create the foreign-toplevel manager; external window "
        "switchers and task managers will not be able to focus, close or "
        "minimise windows");
  }
}

void
hikari_foreign_toplevel_init(
    struct hikari_foreign_toplevel *foreign_toplevel, struct hikari_view *view)
{
  assert(foreign_toplevel != NULL);
  assert(view != NULL);

  foreign_toplevel->handle = NULL;
  foreign_toplevel->view = view;
  foreign_toplevel->published_output = NULL;
}

void
hikari_foreign_toplevel_create(struct hikari_foreign_toplevel *foreign_toplevel)
{
  assert(foreign_toplevel != NULL);

  if (foreign_toplevel->handle != NULL ||
      hikari_server.foreign_toplevel_manager == NULL) {
    return;
  }

  struct wlr_foreign_toplevel_handle_v1 *handle =
      wlr_foreign_toplevel_handle_v1_create(
          hikari_server.foreign_toplevel_manager);

  if (handle == NULL) {
    wlr_log(WLR_ERROR,
        "could not create a foreign-toplevel management handle; this window "
        "will not be actionable from a window switcher");
    return;
  }

  handle->data = foreign_toplevel->view;
  foreign_toplevel->handle = handle;

  foreign_toplevel->request_maximize.notify = request_maximize_handler;
  wl_signal_add(
      &handle->events.request_maximize, &foreign_toplevel->request_maximize);

  foreign_toplevel->request_minimize.notify = request_minimize_handler;
  wl_signal_add(
      &handle->events.request_minimize, &foreign_toplevel->request_minimize);

  foreign_toplevel->request_activate.notify = request_activate_handler;
  wl_signal_add(
      &handle->events.request_activate, &foreign_toplevel->request_activate);

  foreign_toplevel->request_fullscreen.notify = request_fullscreen_handler;
  wl_signal_add(&handle->events.request_fullscreen,
      &foreign_toplevel->request_fullscreen);

  foreign_toplevel->request_close.notify = request_close_handler;
  wl_signal_add(
      &handle->events.request_close, &foreign_toplevel->request_close);

  foreign_toplevel->handle_destroy.notify = handle_destroy_handler;
  wl_signal_add(&handle->events.destroy, &foreign_toplevel->handle_destroy);

  /* [COMMENT] Action purpose: set_rectangle is deliberately not listened to. It
  is a minimise-animation hint naming where the client's taskbar entry sits, and
  hikari draws no minimise animation. */

  hikari_foreign_toplevel_publish_title(foreign_toplevel);
  hikari_foreign_toplevel_publish_state(foreign_toplevel);
  hikari_foreign_toplevel_publish_output(foreign_toplevel);
}

void
hikari_foreign_toplevel_destroy(
    struct hikari_foreign_toplevel *foreign_toplevel)
{
  assert(foreign_toplevel != NULL);

  struct wlr_foreign_toplevel_handle_v1 *handle = foreign_toplevel->handle;

  if (handle == NULL) {
    return;
  }

  wlr_foreign_toplevel_handle_v1_destroy(handle);

  /* [COMMENT] Action purpose: If wlroots emitted the handle's destroy signal,
  handle_destroy_handler has already detached and nulled the pointer. If it did
  not, detach here. Tolerating both makes this correct without depending on
  which, and detach() cannot run twice because it is reached only through a
  non-NULL handle. */
  if (foreign_toplevel->handle != NULL) {
    detach(foreign_toplevel);
  }
}

/* [COMMENT] Function purpose: Publish title and app_id together.

Empty strings rather than NULL, matching the ext-list decision recorded in
BLUEPRINT section 16: a window with no app_id should appear as a window with a
blank app_id, not vanish. `view->id` already holds the app_id -- it is set by
hikari_view_configure(), which runs before hikari_view_map(), so the first state
published at creation already carries it. */
void
hikari_foreign_toplevel_publish_title(
    struct hikari_foreign_toplevel *foreign_toplevel)
{
  assert(foreign_toplevel != NULL);

  if (foreign_toplevel->handle == NULL) {
    return;
  }

  struct hikari_view *view = foreign_toplevel->view;

  wlr_foreign_toplevel_handle_v1_set_title(
      foreign_toplevel->handle, view->title != NULL ? view->title : "");
  wlr_foreign_toplevel_handle_v1_set_app_id(
      foreign_toplevel->handle, view->id != NULL ? view->id : "");
}

/* [COMMENT] Function purpose: Republish the states that are derivable from the
view itself, from whatever the view's current state is rather than from a
transition.

Pushing the whole set from a handful of single-writer call sites (show, hide,
operation commit) is what keeps this from drifting: hikari changes maximization
through several commit paths that all converge on
hikari_view_commit_pending_operation(), and tracking each transition separately
would need every one of them to remember. wlroots coalesces the resulting events
into one `done` on an idle source, so a redundant call costs nothing.

Activation is NOT published here: hikari_view_has_focus() dereferences
hikari_server.workspace, which is NULL during output teardown, so the activated
state comes from hikari_view_activate()'s explicit bool instead. */
void
hikari_foreign_toplevel_publish_state(
    struct hikari_foreign_toplevel *foreign_toplevel)
{
  assert(foreign_toplevel != NULL);

  if (foreign_toplevel->handle == NULL) {
    return;
  }

  struct hikari_view *view = foreign_toplevel->view;

  wlr_foreign_toplevel_handle_v1_set_minimized(
      foreign_toplevel->handle, hikari_view_is_hidden(view));
  wlr_foreign_toplevel_handle_v1_set_maximized(
      foreign_toplevel->handle, hikari_view_is_fully_maximized(view));

  /* [COMMENT] Action purpose: Published from the fullscreen state itself, not
  from the maximization state.

  This used to report full-maximize AS fullscreen, because the two were the same
  operation. A switcher therefore saw a window it had merely maximized come back
  flagged fullscreen as well. They are independent now, and note they are not
  mutually exclusive: fullscreen SHADOWS whatever maximization is underneath it
  (refresh_geometry(), src/view.c), so a maximized window that a client
  fullscreens correctly reports both -- and reports only maximized again once the
  fullscreen is released. */
  wlr_foreign_toplevel_handle_v1_set_fullscreen(
      foreign_toplevel->handle, hikari_view_is_fullscreen(view));
}

void
hikari_foreign_toplevel_publish_activated(
    struct hikari_foreign_toplevel *foreign_toplevel, bool activated)
{
  assert(foreign_toplevel != NULL);

  if (foreign_toplevel->handle == NULL) {
    return;
  }

  wlr_foreign_toplevel_handle_v1_set_activated(
      foreign_toplevel->handle, activated);
}

/* [COMMENT] Function purpose: Keep the handle's output association in step with
the view's, emitting leave-then-enter on a change.

Guarded on the output actually differing because wlroots keeps a list of entered
outputs per handle and a second enter for the same one would duplicate the
entry. The call sites are creation, hikari_view_evacuate() and
hikari_view_migrate() -- the only places a mapped view changes output. Evacuate
runs from hikari_output_fini(), which wlroots calls while the wlr_output is
still alive, so the leave is always sent to a live output. */
void
hikari_foreign_toplevel_publish_output(
    struct hikari_foreign_toplevel *foreign_toplevel)
{
  assert(foreign_toplevel != NULL);

  if (foreign_toplevel->handle == NULL) {
    return;
  }

  struct hikari_output *output = foreign_toplevel->view->output;
  struct wlr_output *wlr_output =
      output != NULL ? output->wlr_output : NULL;

  if (wlr_output == foreign_toplevel->published_output) {
    return;
  }

  if (foreign_toplevel->published_output != NULL) {
    wlr_foreign_toplevel_handle_v1_output_leave(
        foreign_toplevel->handle, foreign_toplevel->published_output);
  }

  if (wlr_output != NULL) {
    wlr_foreign_toplevel_handle_v1_output_enter(
        foreign_toplevel->handle, wlr_output);
  }

  foreign_toplevel->published_output = wlr_output;
}

#else

/* [COMMENT] Action purpose: WITH_FOREIGN_TOPLEVEL_MANAGEMENT=NO. The call sites
in view.c and server.c stay unconditional; only the behaviour goes away. */

void
hikari_foreign_toplevel_manager_setup(struct hikari_server *server)
{}

void
hikari_foreign_toplevel_init(
    struct hikari_foreign_toplevel *foreign_toplevel, struct hikari_view *view)
{
  foreign_toplevel->handle = NULL;
  foreign_toplevel->view = view;
  foreign_toplevel->published_output = NULL;
}

void
hikari_foreign_toplevel_create(struct hikari_foreign_toplevel *foreign_toplevel)
{}

void
hikari_foreign_toplevel_destroy(
    struct hikari_foreign_toplevel *foreign_toplevel)
{}

void
hikari_foreign_toplevel_publish_title(
    struct hikari_foreign_toplevel *foreign_toplevel)
{}

void
hikari_foreign_toplevel_publish_state(
    struct hikari_foreign_toplevel *foreign_toplevel)
{}

void
hikari_foreign_toplevel_publish_activated(
    struct hikari_foreign_toplevel *foreign_toplevel, bool activated)
{}

void
hikari_foreign_toplevel_publish_output(
    struct hikari_foreign_toplevel *foreign_toplevel)
{}

#endif
