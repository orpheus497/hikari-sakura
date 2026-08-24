#include <hikari/ipc.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <wlr/util/log.h>

#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/sheet.h>
#include <hikari/view.h>
#include <hikari/workspace.h>

/* A request is one short line. Anything longer is a client bug or an attempt
 * to make us allocate, so it is refused rather than buffered. */
#define HIKARI_IPC_MAX_REQUEST 512

/* Bounded so a wedged or malicious peer cannot pin the compositor's fd table
 * open. A sheet switcher opens one connection at a time. */
#define HIKARI_IPC_MAX_CLIENTS 8

struct hikari_ipc_client {
  int fd;
  struct wl_event_source *source;
  struct wl_list link;
  size_t len;
  char buf[HIKARI_IPC_MAX_REQUEST];
};

static void
client_destroy(struct hikari_ipc_client *client)
{
  assert(client != NULL);

  wl_list_remove(&client->link);
  if (client->source != NULL) {
    wl_event_source_remove(client->source);
  }
  close(client->fd);
  hikari_server.ipc_nr_clients--;
  free(client);
}

/* [COMMENT] Function purpose: Write a whole response to a non-blocking socket.
 *
 * Responses are a few dozen bytes and the kernel buffer is orders of magnitude
 * larger, so a partial write here means the peer is pathological rather than
 * merely slow. Retrying on EINTR is worth it; spinning on EAGAIN is not, since
 * that would block the compositor's event loop on a client. Give up and let
 * the caller close the connection. */
static bool
write_all(int fd, const char *buf, size_t len)
{
  size_t off = 0;

  while (off < len) {
    ssize_t n = write(fd, buf + off, len - off);

    if (n > 0) {
      off += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }

  return true;
}

static bool
respond(struct hikari_ipc_client *client, const char *text)
{
  return write_all(client->fd, text, strlen(text));
}

/* [COMMENT] Function purpose: Resolve the workspace a request applies to.
 *
 * hikari_server.workspace tracks the focused output's workspace and is NULL
 * during output teardown, so every command has to tolerate its absence rather
 * than assume a compositor always has one. */
static struct hikari_workspace *
current_workspace(void)
{
  return hikari_server.workspace;
}

/* [COMMENT] Function purpose: Gate every request on the compositor being in
normal mode. This is the same guard `can_act()` applies in src/foreign_toplevel.c
and it exists for the same reason, which is worth restating because the failure
is silent under NDEBUG.

An IPC request is client-driven: it arrives whenever an external switcher sends
it, including while the user is mid-drag in move or resize mode, in mark-select,
or on a LOCKED SCREEN. Both operations this socket exposes reach code with hard
preconditions -- `hikari_workspace_switch_sheet()` runs `display_sheet()`, which
calls `hikari_view_show()`/`hide()`, and `hikari_view_pin_to_sheet()` asserts
`!hikari_view_is_hidden(view)` and itself calls `hikari_view_hide()`, which
asserts `!hikari_view_is_forced(view)`. Lock mode forces every view, so a `pin`
on a locked session violates that assertion; with asserts compiled out of every
shipping build (`-DNDEBUG`) it instead corrupts the visibility linkage, which is
the Phase 55 use-after-free class rather than a clean abort.

Because lock mode IS a mode, one test closes both holes at once: the modal-abort
hole, and an external process being able to switch sheets or move windows on a
locked screen. `state` is gated too -- the per-sheet view counts would otherwise
report how many windows are open to anything that can reach the socket while the
screen is locked, which is the leak Phase 88 was careful to avoid for titles. */
static bool
can_act(void)
{
  return hikari_server_in_normal_mode();
}

/* [COMMENT] Function purpose: Report which sheet is displayed and how many
 * views each of the ten sheets holds.
 *
 * The counts are what let a switcher dim empty sheets. Sheet 0 is included on
 * the same footing as the rest even though its views stay visible underneath
 * whichever sheet is displayed -- the caller is told the number and decides
 * how to present that asymmetry. */
static bool
handle_state(struct hikari_ipc_client *client)
{
  struct hikari_workspace *workspace = current_workspace();

  if (workspace == NULL) {
    return respond(client, "error no active workspace\n");
  }

  char out[512];
  int n = snprintf(out,
      sizeof(out),
      "sheet %d\noutput %s\ncounts",
      (int)workspace->sheet->nr,
      workspace->output->wlr_output->name);

  if (n < 0 || (size_t)n >= sizeof(out)) {
    return respond(client, "error response too long\n");
  }

  for (int i = 0; i < HIKARI_NR_OF_SHEETS; i++) {
    int written = snprintf(out + n,
        sizeof(out) - (size_t)n,
        " %d",
        wl_list_length(&workspace->sheets[i].views));

    if (written < 0 || (size_t)written >= sizeof(out) - (size_t)n) {
      return respond(client, "error response too long\n");
    }
    n += written;
  }

  if ((size_t)n + 6 >= sizeof(out)) {
    return respond(client, "error response too long\n");
  }
  memcpy(out + n, "\nEND\n", 6);

  return respond(client, out);
}

/* [COMMENT] Function purpose: Parse a sheet number argument, 0-9.
 *
 * Returns -1 on anything that is not exactly one in-range decimal number, so
 * a garbled request becomes an error rather than sheet 0. */
static int
parse_sheet_nr(const char *arg)
{
  if (arg == NULL || arg[0] == '\0') {
    return -1;
  }

  char *end = NULL;
  long value = strtol(arg, &end, 10);

  if (end == arg || *end != '\0') {
    return -1;
  }
  if (value < 0 || value >= HIKARI_NR_OF_SHEETS) {
    return -1;
  }

  return (int)value;
}

static bool
handle_sheet(struct hikari_ipc_client *client, const char *arg)
{
  struct hikari_workspace *workspace = current_workspace();

  if (workspace == NULL) {
    return respond(client, "error no active workspace\n");
  }

  int nr = parse_sheet_nr(arg);

  if (nr < 0) {
    return respond(client, "error sheet number must be 0-9\n");
  }

  hikari_workspace_switch_sheet(workspace, &workspace->sheets[nr]);

  return respond(client, "ok\n");
}

/* [COMMENT] Function purpose: Move the focused view to another sheet.
 *
 * This is the operation no Wayland protocol expresses -- foreign-toplevel has
 * no workspace concept at all, and ext-workspace-v1's `assign` moves a
 * workspace to an output group rather than a window to a workspace. It exists
 * here because otherwise it cannot exist anywhere. */
static bool
handle_pin(struct hikari_ipc_client *client, const char *arg)
{
  struct hikari_workspace *workspace = current_workspace();

  if (workspace == NULL) {
    return respond(client, "error no active workspace\n");
  }

  int nr = parse_sheet_nr(arg);

  if (nr < 0) {
    return respond(client, "error sheet number must be 0-9\n");
  }

  struct hikari_view *view = workspace->focus_view;

  if (view == NULL) {
    return respond(client, "error no focused view\n");
  }

  /* [COMMENT] Action purpose: Refuse while a resize is in flight. For a tiled
  view hikari_view_pin_to_sheet() reaches queue_reset(), which queues into the
  single `pending_operation` slot -- queuing a second operation over one still
  awaiting the client's ack loses the first and leaves the view dirty forever.
  Every in-tree caller of that class already guards this way
  (hikari_view_toggle_full_maximize() opens with the same early return); an IPC
  request is the one caller whose timing the compositor does not control, so it
  is the one that actually hits it. */
  if (hikari_view_is_dirty(view)) {
    return respond(client, "error view busy\n");
  }

  hikari_view_pin_to_sheet(view, &workspace->sheets[nr]);

  return respond(client, "ok\n");
}

/* [COMMENT] Function purpose: Dispatch one complete request line.
 *
 * @returns false when the connection should be closed afterwards. Every path
 * returns false today -- the protocol is one request per connection, which
 * keeps the server free of per-client state machines. */
static bool
dispatch(struct hikari_ipc_client *client, char *line)
{
  char *arg = strchr(line, ' ');

  if (arg != NULL) {
    *arg = '\0';
    arg++;
    while (*arg == ' ') {
      arg++;
    }
  }

  /* [COMMENT] Action purpose: One gate in front of the whole command table
  rather than one per handler, so a command added later cannot be forgotten --
  the only way to reach compositor state from here is through this branch. See
  can_act() for why this is not optional. */
  if (!can_act()) {
    respond(client, "error compositor busy\n");
    return false;
  }

  if (!strcmp(line, "state")) {
    handle_state(client);
  } else if (!strcmp(line, "sheet")) {
    handle_sheet(client, arg);
  } else if (!strcmp(line, "pin")) {
    handle_pin(client, arg);
  } else {
    respond(client, "error unknown command\n");
  }

  return false;
}

static int
client_readable(int fd, uint32_t mask, void *data)
{
  struct hikari_ipc_client *client = data;

  if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
    client_destroy(client);
    return 0;
  }

  ssize_t n =
      read(fd, client->buf + client->len, sizeof(client->buf) - client->len);

  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return 0;
    }
    client_destroy(client);
    return 0;
  }
  if (n == 0) {
    client_destroy(client);
    return 0;
  }

  client->len += (size_t)n;

  char *nl = memchr(client->buf, '\n', client->len);

  if (nl == NULL) {
    /* [COMMENT] Action purpose: A full buffer with no newline is a request
    that will never terminate. Refuse it instead of waiting forever. */
    if (client->len == sizeof(client->buf)) {
      respond(client, "error request too long\n");
      client_destroy(client);
    }
    return 0;
  }

  *nl = '\0';
  /* Tolerate CRLF from a hand-typed session. */
  if (nl > client->buf && nl[-1] == '\r') {
    nl[-1] = '\0';
  }

  dispatch(client, client->buf);
  client_destroy(client);

  return 0;
}

static int
socket_connection(int fd, uint32_t mask, void *data)
{
  struct hikari_server *server = data;

  if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
    return 0;
  }

  int client_fd = accept(fd, NULL, NULL);

  if (client_fd < 0) {
    return 0;
  }

  /* [COMMENT] Action purpose: Refuse rather than queue once the connection
  budget is spent. A switcher uses one connection; anything holding eight open
  is not one. */
  if (server->ipc_nr_clients >= HIKARI_IPC_MAX_CLIENTS) {
    close(client_fd);
    return 0;
  }

  /* [COMMENT] Action purpose: Close-on-exec, set explicitly because accept(2)
  does NOT inherit it from the listening socket -- POSIX specifies the accepted
  descriptor comes back with FD_CLOEXEC clear, so creating the listener with
  SOCK_CLOEXEC (see hikari_ipc_setup) does nothing for this one.

  It matters here because hikari_command_execute() forks twice and execs
  /bin/sh with no descriptor hygiene of its own -- no closefrom(), unlike the
  topbar and unlocker helpers -- so every application launched from a keybinding
  or from autostart would otherwise inherit whatever control-socket connections
  happened to be open at the time.

  Set with F_SETFD rather than by switching to accept4(SOCK_CLOEXEC) so this
  needs nothing beyond POSIX; the usual argument for accept4 is the fork race
  between accept and fcntl, and hikari is single-threaded with every fork
  happening in an event handler on this same thread. */
  if (fcntl(client_fd, F_SETFD, FD_CLOEXEC) < 0) {
    close(client_fd);
    return 0;
  }

  int flags = fcntl(client_fd, F_GETFL, 0);
  if (flags < 0 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(client_fd);
    return 0;
  }

  struct hikari_ipc_client *client = calloc(1, sizeof(struct hikari_ipc_client));

  if (client == NULL) {
    close(client_fd);
    return 0;
  }

  client->fd = client_fd;
  client->len = 0;
  wl_list_insert(&server->ipc_clients, &client->link);
  server->ipc_nr_clients++;

  client->source = wl_event_loop_add_fd(server->event_loop,
      client_fd,
      WL_EVENT_READABLE,
      client_readable,
      client);

  if (client->source == NULL) {
    client_destroy(client);
  }

  return 0;
}

void
hikari_ipc_setup(struct hikari_server *server)
{
  assert(server != NULL);

  wl_list_init(&server->ipc_clients);
  server->ipc_nr_clients = 0;
  server->ipc_fd = -1;
  server->ipc_source = NULL;
  server->ipc_path = NULL;

  const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

  if (runtime_dir == NULL) {
    wlr_log(WLR_ERROR,
        "XDG_RUNTIME_DIR is unset; not creating the hikari control socket. "
        "External sheet switchers will not work.");
    return;
  }

  char path[sizeof(((struct sockaddr_un *)NULL)->sun_path)];
  int n = snprintf(path, sizeof(path), "%s/hikari.sock", runtime_dir);

  if (n < 0 || (size_t)n >= sizeof(path)) {
    wlr_log(WLR_ERROR,
        "XDG_RUNTIME_DIR is too long for a unix socket path; not creating the "
        "hikari control socket.");
    return;
  }

  /* [COMMENT] Action purpose: Clear a socket left behind by a compositor that
  died without unlinking. bind(2) fails with EADDRINUSE on a stale node, and
  the alternative -- refusing to start the socket for the rest of the session
  -- is worse than replacing a path only we ever create. */
  unlink(path);

  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

  if (fd < 0) {
    wlr_log(WLR_ERROR,
        "could not create the hikari control socket: %s",
        strerror(errno));
    return;
  }

  struct sockaddr_un addr = { 0 };
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path, path, (size_t)n + 1);

  /* [COMMENT] Action purpose: Restrict the socket before it is reachable.
  XDG_RUNTIME_DIR is already 0700, so this is belt and braces -- but the socket
  can switch sheets and move windows, and it costs one call. */
  mode_t old_umask = umask(0077);
  int rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
  umask(old_umask);

  if (rc < 0) {
    wlr_log(WLR_ERROR,
        "could not bind the hikari control socket at %s: %s",
        path,
        strerror(errno));
    close(fd);
    return;
  }

  if (listen(fd, HIKARI_IPC_MAX_CLIENTS) < 0) {
    wlr_log(WLR_ERROR,
        "could not listen on the hikari control socket: %s",
        strerror(errno));
    close(fd);
    unlink(path);
    return;
  }

  server->ipc_source = wl_event_loop_add_fd(
      server->event_loop, fd, WL_EVENT_READABLE, socket_connection, server);

  if (server->ipc_source == NULL) {
    wlr_log(WLR_ERROR, "could not watch the hikari control socket");
    close(fd);
    unlink(path);
    return;
  }

  server->ipc_fd = fd;
  server->ipc_path = strdup(path);

  wlr_log(WLR_INFO, "hikari control socket listening on %s", path);
}

void
hikari_ipc_fini(struct hikari_server *server)
{
  assert(server != NULL);

  if (server->ipc_source != NULL) {
    wl_event_source_remove(server->ipc_source);
    server->ipc_source = NULL;
  }

  /* [COMMENT] Action purpose: Drop clients before closing the listener, using
  a safe iterator because client_destroy() unlinks the node it is handed. */
  if (server->ipc_clients.next != NULL) {
    struct hikari_ipc_client *client, *client_tmp;
    wl_list_for_each_safe (client, client_tmp, &server->ipc_clients, link) {
      client_destroy(client);
    }
  }

  /* [COMMENT] Action purpose: `> 0`, not `>= 0`. `hikari_server` is a global and
  therefore zero-initialised, so if hikari_ipc_setup() never ran -- it is called
  from server_init(), which has fatal-exit paths above it -- `ipc_fd` is 0 rather
  than -1, and `>= 0` would close STDIN on the way out. The field is only ever
  assigned a real descriptor after the event source is confirmed registered, so
  a genuine listener can never be fd 0. */
  if (server->ipc_fd > 0) {
    close(server->ipc_fd);
    server->ipc_fd = -1;
  }

  if (server->ipc_path != NULL) {
    unlink(server->ipc_path);
    free(server->ipc_path);
    server->ipc_path = NULL;
  }
}
