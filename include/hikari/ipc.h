#if !defined(HIKARI_IPC_H)
#define HIKARI_IPC_H

/**
 * @file ipc.h
 * @brief Minimal control socket for external panels.
 *
 * hikari's sheet model is entirely internal: ten fixed sheets per workspace,
 * permanently bound to their output. No standards-track Wayland protocol
 * expresses it -- foreign-toplevel has no notion of a workspace, and
 * ext-workspace-v1's `assign` moves a workspace to an output group rather than
 * a window to a workspace. An external sheet switcher therefore has nothing to
 * read and nothing to call.
 *
 * This is that missing surface, deliberately kept as small as it can be: a
 * request/response text socket that reports which sheet is displayed and how
 * many views each sheet holds, and accepts the two operations a switcher
 * needs. It is not a general scripting interface and should not grow into one
 * -- anything expressible as a Wayland protocol belongs in a Wayland protocol.
 *
 * The socket lives at $XDG_RUNTIME_DIR/hikari.sock, mode 0600. It is served
 * from the compositor's own wl_event_loop, so handlers run on the main thread
 * and may touch compositor state directly; they must never block.
 */

struct hikari_server;

/**
 * Create and listen on the control socket.
 *
 * Failure is non-fatal and warned about: losing the control socket costs an
 * external sheet switcher, not the session.
 */
void
hikari_ipc_setup(struct hikari_server *server);

/** Close the socket, drop every connected client, and unlink the path. */
void
hikari_ipc_fini(struct hikari_server *server);

#endif
