#include <hikari/lock_mode.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// [COMMENT] Action purpose: Portability shim — on FreeBSD, explicit_bzero is
// declared in <strings.h>. On Linux (used only for IDE analysis, not builds),
// it requires _DEFAULT_SOURCE. Provide a fallback declaration so clangd works.
#if !defined(__FreeBSD__) && !defined(HAVE_EXPLICIT_BZERO)
extern void explicit_bzero(void *, size_t);
#endif

#include <wlr/types/wlr_seat.h>
#include <errno.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>

#include <hikari/cursor.h>
#include <hikari/keyboard.h>
#include <hikari/lock_indicator.h>
#include <hikari/output.h>

#include <hikari/server.h>
#include <hikari/utf8.h>
#include <hikari/view.h>

#define BUFFER_SIZE 1024

// [COMMENT] Action purpose: Resolve the unlock helper through a compile-time
// absolute path rather than the inherited PATH. hikari-unlocker is installed
// setuid (mode 4555); resolving it via `/bin/sh -c "hikari-unlocker"` would let
// a caller-controlled PATH substitute an impostor helper that reports
// authentication success without verifying the password. HIKARI_PREFIX is
// supplied by the Makefile and matches the install destination.
#define HIKARI_STR_INNER(s) #s
#define HIKARI_STR(s) HIKARI_STR_INNER(s)
#define HIKARI_UNLOCKER_PATH HIKARI_STR(HIKARI_PREFIX) "/bin/hikari-unlocker"

static char input_buffer[BUFFER_SIZE];
static int cursor = 0;
static int locker_pipe[2][2] = { { -1, -1 }, { -1, -1 } };
static pid_t locker_pid = -1;

static struct hikari_lock_mode *
get_mode(void)
{
  struct hikari_lock_mode *mode = &hikari_server.lock_mode;

  assert(mode == (struct hikari_lock_mode *)hikari_server.mode);

  return mode;
}

static void
clear_buffer(void)
{
  cursor = 0;
  explicit_bzero(input_buffer, BUFFER_SIZE);
}

static void
clear_password(void)
{
  struct hikari_lock_mode *mode = get_mode();

  clear_buffer();
  hikari_lock_indicator_clear(mode->lock_indicator);
}

// [COMMENT] Function purpose: Fork and exec hikari-unlocker, wiring its stdin
// and stdout to the password and result pipes.
static bool
start_unlocker(void)
{
  // [COMMENT] Action purpose: Create two unidirectional pipes for IPC with
  // hikari-unlocker: pipe[0] carries the password (parent writes, child reads),
  // pipe[1] carries the authentication result (child writes, parent reads).
  if (pipe(locker_pipe[0]) == -1) {
    return false;
  }
  if (pipe(locker_pipe[1]) == -1) {
    close(locker_pipe[0][0]);
    close(locker_pipe[0][1]);
    locker_pipe[0][0] = -1;
    locker_pipe[0][1] = -1;
    return false;
  }

  // [COMMENT] Action purpose: Fork a child process to run hikari-unlocker,
  // which performs PAM authentication in a separate address space so the
  // compositor event loop is never blocked by pam_authenticate().
  locker_pid = fork();

  // [COMMENT] Action purpose: On fork failure, close all pipe descriptors and
  // preserve the locked session without attempting password submission.
  if (locker_pid == -1) {
    close(locker_pipe[0][0]);
    close(locker_pipe[0][1]);
    close(locker_pipe[1][0]);
    close(locker_pipe[1][1]);
    locker_pipe[0][0] = -1;
    locker_pipe[0][1] = -1;
    locker_pipe[1][0] = -1;
    locker_pipe[1][1] = -1;
    locker_pid = -1;
    return false;
  }

  // [COMMENT] Action purpose: In the child process, rewire stdin/stdout to the
  // pipe endpoints and exec hikari-unlocker for PAM authentication.
  if (locker_pid == 0) {
    close(locker_pipe[0][1]);
    close(locker_pipe[1][0]);
    // [COMMENT] Action purpose: dup2 both endpoints onto stdin/stdout, checking
    // for failure -- an unchecked dup2 could silently leave the child reading
    // or writing the wrong descriptor. The original endpoint is only closed
    // when it differs from its target: dup2 is a no-op when src == dst, so
    // closing an endpoint that already equals 0 or 1 would sever the freshly
    // established stdin/stdout.
    if (dup2(locker_pipe[0][0], STDIN_FILENO) == -1 ||
        dup2(locker_pipe[1][1], STDOUT_FILENO) == -1) {
      fprintf(stderr, "error: could not redirect hikari-unlocker pipes\n");
      _exit(EXIT_FAILURE);
    }

    if (locker_pipe[0][0] != STDIN_FILENO) {
      close(locker_pipe[0][0]);
    }

    if (locker_pipe[1][1] != STDOUT_FILENO) {
      close(locker_pipe[1][1]);
    }

    // [COMMENT] Action purpose: Execute the helper directly at its absolute
    // install path. Avoiding the /bin/sh -c indirection removes both the PATH
    // lookup and the shell's own metacharacter/environment handling from the
    // authentication path.
    execl(HIKARI_UNLOCKER_PATH, "hikari-unlocker", NULL);
    // [COMMENT] Action purpose: Reached only when execl fails. The child must
    // not report success: emit a diagnostic (stderr survives the stdin/stdout
    // rewiring) and exit non-zero so the failure is diagnosable. _exit skips
    // inherited atexit handlers and stdio flushing in the forked compositor
    // address space; the parent observes the pipe hangup as a terminal locker
    // failure and reaps the child.
    fprintf(stderr, "error: could not execute hikari-unlocker\n");
    _exit(EXIT_FAILURE);
  // [COMMENT] Action purpose: In the parent process, close the child-side pipe
  // endpoints that are no longer needed to avoid descriptor leaks.
  } else {
    close(locker_pipe[0][0]);
    close(locker_pipe[1][1]);
  }

  return true;
}

static void
put_char(uint32_t codepoint)
{
  size_t length = utf8_chsize(codepoint);

  if (cursor + length < BUFFER_SIZE) {
    struct hikari_lock_mode *mode = get_mode();

    hikari_lock_indicator_set_type(mode->lock_indicator);
    utf8_encode(&input_buffer[cursor], length, codepoint);
    cursor += length;
  }
}

static void
delete_char(void)
{
  struct hikari_lock_mode *mode = get_mode();

  if (cursor == 0) {
    return;
  }

  hikari_lock_indicator_set_type(mode->lock_indicator);

  input_buffer[--cursor] = '\0';

  if (cursor == 0) {
    hikari_lock_indicator_clear(mode->lock_indicator);
  }
}

/* [COMMENT] Function purpose: Attempt one non-blocking reap of the unlocker
child. Returns true when the child has been collected (or was already gone),
false when it is still running and a retry is needed. */
static bool
try_reap_locker(void)
{
  if (locker_pid <= 0) {
    return true;
  }

  int status;
  pid_t result;
  do {
    result = waitpid(locker_pid, &status, WNOHANG);
  } while (result == -1 && errno == EINTR);

  // [COMMENT] Action purpose: ECHILD means the child is already gone (e.g. reaped
  // elsewhere); treat it as success so the pid state is cleared either way.
  if (result > 0 || (result == -1 && errno == ECHILD)) {
    locker_pid = -1;
    return true;
  }

  return false;
}

/* [COMMENT] Function purpose: Timer callback that retries the non-blocking reap
until the unlocker child is collected, then disarms itself. */
static int
reap_locker_handler(void *data)
{
  struct hikari_lock_mode *mode = data;

  if (try_reap_locker()) {
    if (mode->locker_reap_timer != NULL) {
      wl_event_source_remove(mode->locker_reap_timer);
      mode->locker_reap_timer = NULL;
    }
    return 0;
  }

  // [COMMENT] Action purpose: Child still running -- re-arm rather than spin.
  wl_event_source_timer_update(mode->locker_reap_timer, 50);
  return 0;
}

/* [COMMENT] Function purpose: Reap the unlocker child without ever blocking the
event loop. Tries once inline; if the child has not exited yet, arms a short
retry timer and returns immediately so the compositor keeps dispatching. */
static void
reap_locker_deferred(struct hikari_lock_mode *mode)
{
  if (try_reap_locker()) {
    return;
  }

  if (mode->locker_reap_timer == NULL) {
    mode->locker_reap_timer = wl_event_loop_add_timer(
        hikari_server.event_loop, reap_locker_handler, mode);
  }

  if (mode->locker_reap_timer != NULL) {
    wl_event_source_timer_update(mode->locker_reap_timer, 50);
  }
}

// [COMMENT] Function purpose: Handle the authentication result from hikari-unlocker
// asynchronously via the Wayland event loop. This callback fires when data is
// available on the locker result pipe, preventing the compositor from blocking
// during PAM password verification.
static int
locker_result_handler(int fd, uint32_t mask, void *data)
{
  struct hikari_lock_mode *mode = data;
  bool success = false;
  bool got_result = false;

  // [COMMENT] Action purpose: Read the authentication result boolean from the
  // unlocker pipe when data is available, distinguishing a complete result from
  // read failure or EOF.
  if (mask & WL_EVENT_READABLE) {
    ssize_t n;
    do {
      n = read(fd, &success, sizeof(bool));
    } while (n == -1 && errno == EINTR);

    if (n == (ssize_t)sizeof(bool)) {
      // [COMMENT] Action purpose: A complete authentication result was read
      // from the unlocker pipe. Mark as received so hangup does not override.
      got_result = true;
    } else {
      // [COMMENT] Action purpose: Treat read failure, EOF, or incomplete read
      // as authentication failure -- hikari-unlocker may have crashed.
      success = false;
    }
  }

  // [COMMENT] Action purpose: Handle pipe hangup when no readable result was
  // obtained -- the unlocker exited or crashed without writing a result, so
  // this is a terminal failure requiring child cleanup.
  if ((mask & WL_EVENT_HANGUP) && !got_result) {
    success = false;
  }

  // [COMMENT] Action purpose: Determine whether this result is terminal (the
  // child has exited or will exit) or retryable (wrong password, child stays
  // alive for the next attempt). Terminal conditions: success, read failure,
  // or ANY hangup. WL_EVENT_READABLE and WL_EVENT_HANGUP can be delivered in
  // the same callback when the child writes a false result and then exits; in
  // that case got_result is true and success is false, so without the explicit
  // hangup term this would be misclassified as retryable, leaving locker_pid
  // unreaped and the pipe fds open on a dead child. The next password attempt
  // would then write into a broken pipe. Retryable: a complete false result
  // read while the helper is still alive.
  bool terminal = success || !got_result || (mask & WL_EVENT_HANGUP);

  // [COMMENT] Action purpose: Remove the fd event source now that we have a
  // result. The source must be cleaned up before closing the pipe fds.
  if (mode->locker_event_source != NULL) {
    wl_event_source_remove(mode->locker_event_source);
    mode->locker_event_source = NULL;
  }

  if (terminal) {
    // [COMMENT] Action purpose: Reap the unlocker child WITHOUT blocking. This
    // runs inside a Wayland event-loop callback: hikari-unlocker writes its
    // result and only then finishes PAM cleanup and exits, so a blocking
    // waitpid() here stalls the entire compositor for the duration of that
    // teardown. reap_locker_deferred tries once and falls back to a short retry
    // timer, keeping the event loop dispatching either way.
    reap_locker_deferred(mode);

    close(locker_pipe[1][0]);
    close(locker_pipe[0][1]);
    locker_pipe[1][0] = -1;
    locker_pipe[0][1] = -1;
  }

  if (success) {
    hikari_server_enter_normal_mode(NULL);
  } else {
    hikari_lock_indicator_set_deny(mode->lock_indicator);
  }

  return 0;
}

// [COMMENT] Function purpose: Send the password to hikari-unlocker and register
// a non-blocking event source for the authentication result.
static void
submit_password(void)
{
  struct hikari_lock_mode *mode = get_mode();

  // [COMMENT] Action purpose: If the unlocker child is not running (failed start
  // or died), attempt to restart it before denying. This handles the case where
  // the unlocker crashed between lock_mode_enter and the first password submit.
  if (locker_pid <= 0) {
    if (!start_unlocker()) {
      hikari_lock_indicator_set_deny(mode->lock_indicator);
      return;
    }
  }

  size_t password_length = strnlen(input_buffer, BUFFER_SIZE - 1) + 1;

  hikari_lock_indicator_set_verify(mode->lock_indicator);
  // [COMMENT] Action purpose: Write password to unlocker pipe. If write fails
  // (broken pipe, full buffer), the password is silently lost — the unlocker
  // will not receive it and will not write a result, so locker_result_handler
  // will eventually fire with WL_EVENT_HANGUP and show the deny indicator.
  // [COMMENT] Action purpose: Write the full password to the unlocker pipe,
  // handling both EINTR interrupts and partial writes. The buffer position
  // and remaining length are advanced after each successful partial write.
  const char *buf = input_buffer;
  size_t remaining = password_length;
  while (remaining > 0) {
    ssize_t nw = write(locker_pipe[0][1], buf, remaining);
    if (nw == -1) {
      if (errno == EINTR) {
        continue;
      }
      fprintf(stderr, "lock_mode: failed to write password to unlocker pipe\n");
      break;
    }
    buf += nw;
    remaining -= (size_t)nw;
  }
  clear_buffer();

  // [COMMENT] Action purpose: Register the locker result pipe with the Wayland
  // event loop for non-blocking read. The locker_result_handler will fire when
  // hikari-unlocker writes the success/failure boolean back.
  if (mode->locker_event_source != NULL) {
    wl_event_source_remove(mode->locker_event_source);
  }
  mode->locker_event_source = wl_event_loop_add_fd(
      hikari_server.event_loop,
      locker_pipe[1][0],
      WL_EVENT_READABLE | WL_EVENT_HANGUP,
      locker_result_handler,
      mode);
}

static void
disable_outputs(void)
{
  struct hikari_lock_mode *mode = get_mode();

  wl_event_source_timer_update(mode->disable_outputs, 0);

  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    hikari_output_disable(output);

    hikari_output_damage_whole(output);
  }

  mode->outputs_disabled = true;

  clear_password();
}

static void
enable_outputs(void)
{
  struct hikari_lock_mode *mode = get_mode();

  if (!mode->outputs_disabled) {
    return;
  }

  assert(cursor == 0);

  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    hikari_output_enable(output);
  }

  mode->outputs_disabled = false;
}

// [COMMENT] Function purpose: Handles keyboard events for lock mode authentication.
static void
key_handler(
    struct hikari_keyboard *keyboard, struct wlr_keyboard_key_event *event)
{
  struct hikari_lock_mode *mode = get_mode();

  if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    const xkb_keysym_t *syms;
    uint32_t keycode = event->keycode + 8;
    uint32_t codepoint;

    int nsyms = xkb_state_key_get_syms(
        keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    enable_outputs();

    for (int i = 0; i < nsyms; i++) {
      switch (syms[i]) {
        case XKB_KEY_Caps_Lock:
        case XKB_KEY_Shift_L:
        case XKB_KEY_Shift_R:
        case XKB_KEY_Control_L:
        case XKB_KEY_Control_R:
        case XKB_KEY_Meta_L:
        case XKB_KEY_Meta_R:
        case XKB_KEY_Alt_L:
        case XKB_KEY_Alt_R:
        case XKB_KEY_Super_L:
        case XKB_KEY_Super_R:
          break;

        case XKB_KEY_Escape:
          clear_password();
          break;

        case XKB_KEY_BackSpace:
          delete_char();
          break;

        case XKB_KEY_Return:
          submit_password();
          break;

        case XKB_KEY_c:
          if (hikari_keyboard_check_modifier(keyboard, WLR_MODIFIER_CTRL)) {
            disable_outputs();
            return;
          }
        default:
          codepoint = hikari_keyboard_get_codepoint(keyboard, keycode);

          if (codepoint) {
            put_char(codepoint);
          }
          break;
      }
    }

    if (mode->disable_outputs != NULL) {
      wl_event_source_timer_update(mode->disable_outputs, 10 * 1000);
    }
  }
}

static void
modifiers_handler(struct hikari_keyboard *keyboard)
{}

// [COMMENT] Function purpose: Ignores pointer button events during lock mode.
static void
button_handler(
    struct hikari_cursor *cursor, struct wlr_pointer_button_event *event)
{}

static void
reset_visibility(void)
{
  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    struct hikari_view *view;
    wl_list_for_each (view, &output->views, output_views) {
      if (hikari_view_is_forced(view)) {
        hikari_view_unset_forced(view);

        if (hikari_view_is_hidden(view)) {
          hikari_view_unset_hidden(view);
        } else {
          hikari_view_set_hidden(view);
        }
      }
    }
  }
}

// [COMMENT] Function purpose: Tear down active lock mode state, restoring outputs and
// compositor focus to normal operation. Called on authentication success or forced
// cancellation (e.g. compositor shutdown while locked).
static void
cancel(void)
{
  struct hikari_lock_mode *mode = get_mode();

  // [COMMENT] Action purpose: Clean up the locker event source if authentication
  // is still pending when lock mode is cancelled.
  if (mode->locker_event_source != NULL) {
    wl_event_source_remove(mode->locker_event_source);
    mode->locker_event_source = NULL;
  }

  // [COMMENT] Action purpose: Close pipe FDs and reap the unlocker child if
  // authentication was still in flight when cancel is called (e.g. compositor
  // shutdown while locked). Closing the write end of the password pipe causes
  // the child to receive EOF on its stdin and exit cleanly.
  if (locker_pipe[0][1] != -1) {
    close(locker_pipe[0][1]);
    locker_pipe[0][1] = -1;
  }
  if (locker_pipe[1][0] != -1) {
    close(locker_pipe[1][0]);
    locker_pipe[1][0] = -1;
  }
  // [COMMENT] Action purpose: Non-blocking reap attempt. The child will exit shortly
  // after stdin receives EOF (pipe write-end closed above). If it hasn't exited yet
  // (WNOHANG returns 0), retain locker_pid and defer the final blocking waitpid to
  // hikari_lock_mode_fini so cancel() never blocks the compositor event loop.
  if (locker_pid > 0) {
    int status;
    pid_t result;
    do {
      result = waitpid(locker_pid, &status, WNOHANG);
    } while (result == -1 && errno == EINTR);
    if (result > 0 || (result == -1 && errno == ECHILD)) {
      locker_pid = -1;
    }
    // result == 0: child still running; locker_pid retained for fini reap
  }

  if (mode->disable_outputs != NULL) {
    wl_event_source_remove(mode->disable_outputs);
    mode->disable_outputs = NULL;
  }

  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    hikari_output_enable(output);
    hikari_output_damage_whole(output);
  }

  hikari_lock_indicator_fini(mode->lock_indicator);
  hikari_free(mode->lock_indicator);
  mode->lock_indicator = NULL;

  // [COMMENT] Action purpose: Reset outputs_disabled flag so re-entry into
  // lock mode starts with clean state.
  mode->outputs_disabled = false;

  reset_visibility();

  hikari_cursor_activate(&hikari_server.cursor);
}

static void
cursor_move(uint32_t time_msec)
{}

static int
disable_outputs_handler(void *data)
{
  assert(hikari_server_in_lock_mode());

  disable_outputs();

  return 0;
}

void
hikari_lock_mode_init(struct hikari_lock_mode *lock_mode)
{
  lock_mode->mode.key_handler = key_handler;
  lock_mode->mode.button_handler = button_handler;
  lock_mode->mode.modifiers_handler = modifiers_handler;

  lock_mode->mode.cancel = cancel;
  lock_mode->mode.cursor_move = cursor_move;

  lock_mode->lock_indicator = NULL;
  lock_mode->locker_event_source = NULL;
  lock_mode->locker_reap_timer = NULL;
  lock_mode->outputs_disabled = false;

  mlock(input_buffer, BUFFER_SIZE);
  clear_buffer();
}

// [COMMENT] Function purpose: Final cleanup of lock mode resources, including a
// deferred blocking reap of any unlocker child that cancel() could not collect
// non-blockingly, and unlocking the password buffer from RAM.
void
hikari_lock_mode_fini(struct hikari_lock_mode *lock_mode)
{
  // [COMMENT] Action purpose: Tear down the retry timer before the mode goes
  // away so a pending reap callback cannot fire against freed state.
  if (lock_mode->locker_reap_timer != NULL) {
    wl_event_source_remove(lock_mode->locker_reap_timer);
    lock_mode->locker_reap_timer = NULL;
  }

  // [COMMENT] Action purpose: Final non-blocking reap attempt for any unlocker
  // child still outstanding. The child exits shortly after its stdin receives
  // EOF (the pipe write-end was closed in cancel()). If it somehow has not
  // exited yet, leave it to init rather than blocking compositor shutdown.
  try_reap_locker();

  munlock(input_buffer, BUFFER_SIZE);
}

static void
override_visibility(void)
{
  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    struct hikari_view *view;
    wl_list_for_each (view, &output->views, output_views) {
      if (hikari_view_is_public(view)) {
        if (hikari_view_is_hidden(view)) {
          hikari_view_set_forced(view);
          hikari_view_unset_hidden(view);
        }
      } else {
        if (!hikari_view_is_hidden(view)) {
          hikari_view_set_forced(view);
          hikari_view_set_hidden(view);
        }
      }
    }
  }
}

void
hikari_lock_mode_enter(void)
{
  struct hikari_workspace *workspace = hikari_server.workspace;
  struct hikari_view *focus_view = workspace->focus_view;

#ifdef HAVE_LAYERSHELL
  struct hikari_layer *focus_layer = workspace->focus_layer;

  if (focus_layer != NULL) {
    assert(focus_view == NULL);

    struct wlr_seat *wlr_seat = hikari_server.seat;

    workspace->focus_layer = NULL;

    wlr_seat_keyboard_end_grab(wlr_seat);

    wlr_seat_pointer_clear_focus(wlr_seat);
    wlr_seat_keyboard_clear_focus(wlr_seat);
  } else if (focus_view != NULL) {
    assert(focus_layer == NULL);
    hikari_workspace_focus_view(workspace, NULL);
  }
#else
  if (focus_view != NULL) {
    hikari_workspace_focus_view(workspace, NULL);
  }
#endif

  hikari_cursor_deactivate(&hikari_server.cursor);

  hikari_server.mode = (struct hikari_mode *)&hikari_server.lock_mode;

  struct hikari_lock_mode *mode = get_mode();

  assert(mode->lock_indicator == NULL);

  mode->lock_indicator = hikari_malloc(sizeof(struct hikari_lock_indicator));

  hikari_lock_indicator_init(mode->lock_indicator);

  clear_buffer();
  // [COMMENT] Action purpose: Start the unlocker child process. If pipe or fork
  // setup fails here, submit_password() will retry start_unlocker() on the
  // first password submission attempt.
  start_unlocker();
  override_visibility();

  mode->disable_outputs = wl_event_loop_add_timer(
      hikari_server.event_loop, disable_outputs_handler, NULL);

  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    hikari_output_damage_whole(output);
  }

  wl_event_source_timer_update(mode->disable_outputs, 1000);
}
