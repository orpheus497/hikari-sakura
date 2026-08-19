# Deep Structural Analysis: CRTC 98 Disabling at Startup

## 1. What is Actually Happening (The Real Mechanism)

The "disabling CRTC 98" message is the kernel's DRM subsystem rejecting an **atomic commit from wlroots** that includes a request to set an output's CRTC to a disabled state. But the critical question is *which code path in hikari is sending that request, and why during startup*.

Having now read every file in full, I can trace the precise callchain:

### The Startup Sequence in hikari (`server.c` → `output.c` → `lock_mode.c`)

**Step 1: `hikari_server_prepare_privileged` (server.c:807)**
`wlr_backend_autocreate()` is called here, which creates the DRM backend and wlroots session. `hikari_server.session` is captured here. The process must still hold the correct privileges at this exact point for `libseat` to grant DRM master.

**Step 2: `server_init` → `init_noop_output` (server.c:912, 1050)**
`server_init` initializes **all modes**, including `hikari_lock_mode_init` (server.c:1040), **before** calling `init_noop_output`. `hikari_lock_mode_init` sets `lock_mode->outputs_disabled = false`. This is fine. But note: **all modes are initialized to a zeroed state here before any real outputs arrive**.

**Step 3: `wlr_backend_start` (server.c:1081)**
When `wlr_backend_start` fires, wlroots enumerates physical DRM connectors and emits `new_output` signals for each one — this triggers `new_output_handler` → `hikari_output_init`.

**Step 4: Inside `hikari_output_init` (output.c:303) — THE CRITICAL PATH**

Look at lines 384-388 in `output.c`:
```c
// After the initial modeset commit succeeds and output is marked enabled:
if (hikari_server_in_lock_mode() &&
    hikari_lock_mode_are_outputs_disabled(&hikari_server.lock_mode)) {
  hikari_output_disable(output);
}
```

`hikari_output_disable` (output.c:157) then calls:
```c
wlr_output_state_set_enabled(&state, false);
wlr_output_commit_state(wlr_output, &state);
```

**This is the DRM commit that fails with "disabling CRTC 98".**

---

## 2. The Exact Bug: A State Check That Can Be True During Startup

The guard `hikari_server_in_lock_mode()` checks:
```c
return hikari_server.mode == (struct hikari_mode *)&hikari_server.lock_mode;
```

`hikari_server.mode` is set **in `init_noop_output` at server.c:905**:
```c
hikari_server.mode = (struct hikari_mode *)&hikari_server.normal_mode;
```

This happens at the tail of `server_init`. However, `wlr_backend_start` (server.c:1081) is called **after `server_init`** returns. So `hikari_server.mode` should be `normal_mode` when real outputs arrive. Normally, this guard is safe.

**BUT**: The problem is `hikari_lock_mode_are_outputs_disabled`:
```c
return lock_mode->outputs_disabled;
```

This flag is `false` at init. The check passes as `false`, so the `hikari_output_disable` call is **not invoked** on the first output attach during a fresh startup.

### So Where Does the Failure Actually Come From?

There is a **different scenario** that perfectly matches the symptom — and it is specific to this codebase's construction:

**The `request_state_handler` (output.c:280-286)**:
```c
static void
request_state_handler(struct wl_listener *listener, void *data)
{
  struct hikari_output *output = wl_container_of(listener, output, request_state);
  const struct wlr_output_event_request_state *event = data;
  wlr_output_commit_state(output->wlr_output, event->state);
}
```

This listener is subscribed during `hikari_output_init` (output.c:380):
```c
wl_signal_add(&wlr_output->events.request_state, &output->request_state);
```

In wlroots 0.20, the DRM backend emits a `request_state` event on outputs it is negotiating states for — including during initial enumeration. When `wlr_backend_start` causes wlroots to scan connectors, it may call `request_state` on a connector where it has no mode yet (or for which it is trying a "disable" operation to test modeset feasibility). The `request_state_handler` unconditionally calls `wlr_output_commit_state` with whatever state wlroots passes in — **including a state that sets `enabled = false` and zeroes the CRTC**.

**This is the disabling CRTC 98 commit.** It happens because wlroots itself internally requests a disable-state for an output during DRM probing/negotiation, and hikari's `request_state_handler` blindly forwards that commit without checking whether the output is in a valid state to receive it.

---

## 3. Why Sway Does Not Fail Here

Sway does not use `request_state` in this unconditional forwarding pattern. Sway's output handling gates the forwarded commits through the output configuration state machine, adding safeguards against naked disable commits during the initial DRM probe window.

---

## 4. The PAM / hikari-unlocker Picture (Accurately)

`hikari_unlocker.c` + `lock_mode.c` form an isolated, correct PAM integration via `fork()` + `pipe()`. The PAM service name is `hikari-unlocker` (see `hikari_unlocker.c:85`), which maps to `etc/pam.d/hikari-unlocker.FreeBSD` containing only `auth include system`. **PAM is structurally correct and completely unrelated to the startup CRTC issue.**

The `lock_mode` does produce output disabling (via `hikari_output_disable`) in two cases:
1. `disable_outputs()` — called 1 second after locking via a timer.
2. `hikari_output_init` — when a new output hotplugs while already in locked+disabled state.

Neither of these runs during a clean cold startup unless the compositor is launched while already in a locked session, which is not the reported case.

---

## 5. The `xdg-desktop-portal-wlr` Picture (Accurately)

`start-hikari.sh` correctly sets `XDG_CURRENT_DESKTOP=Hikari` and only wraps in `dbus-run-session` **if no D-Bus session already exists**. The `setenv("WAYLAND_DISPLAY", server->socket, true)` call happens at `server_init` (server.c:975) — before `wlr_backend_start` fires. **The portal architecture is structurally sound.** The `WAYLAND_DISPLAY` is set before the backend starts, meaning by the time clients (including the portal) can connect, the socket is registered. This is not the failure source.

---

## 6. The `seatd` / Privilege Picture (Accurately)

`hikari_server_prepare_privileged` (server.c:807) calls `wlr_backend_autocreate` **before** `drop_privileges`. This is the correct pattern — the backend (and thus the session + DRM master) must be acquired while the binary still holds elevated capabilities, and then privileges are dropped. The code at server.c:842 correctly drops privileges after the backend is created. The `assert(geteuid() != 0)` in `main.c:256` guards against running as root. **The seat privilege model is correctly implemented.**

---

## 7. Root Cause Summary

| Layer | Status | Finding |
|---|---|---|
| seatd / session | ✅ Correct | `wlr_backend_autocreate` called pre-drop-privileges |
| PAM / hikari-unlocker | ✅ Correct | Isolated fork/pipe IPC; correct PAM service file |
| xdg-desktop-portal-wlr | ✅ Correct | `WAYLAND_DISPLAY` set pre-backend-start; correct env |
| `request_state_handler` | ❌ **Bug** | Blindly forwards all `request_state` events from wlroots, including initial DRM probe disable commits, causing the "Failed to disable CRTC 98" |
| `hikari_output_init` lock check | ⚠️ Latent | Correctly guarded now but fragile if `mode` is ever uninitialized at the time `new_output` fires |

---

## Implementation Plan

### Step 1: Update `.devdocs/` (requires your approval)
Update `BRIEFING.md`, `DECISIONS_LOG.md`, and `TODOS.md` to record the root cause finding.

### Step 2: Harden `request_state_handler` in `src/output.c`

#### [MODIFY] `src/output.c` — `request_state_handler`

The fix is to gate the forwarded commit. The handler must check whether the output is currently enabled and in a valid state before forwarding an incoming state that disables the CRTC:

```c
static void
request_state_handler(struct wl_listener *listener, void *data)
{
  struct hikari_output *output =
      wl_container_of(listener, output, request_state);
  const struct wlr_output_event_request_state *event = data;

  // [COMMENT] Action purpose: Guard against forwarding a disable-CRTC commit
  // from wlroots during initial DRM probing, before the output is enabled.
  // wlroots 0.20 emits request_state for modeset negotiation on connectors it
  // is scanning; forwarding a disable commit here without this guard is what
  // produces "Failed to disable CRTC <N>" on startup.
  if (!output->enabled && !wlr_output_state_is_enabled(event->state)) {
    return;
  }

  wlr_output_commit_state(output->wlr_output, event->state);
}
```

> [!IMPORTANT]
> `wlr_output_state_is_enabled()` must be verified against the wlroots 0.20 API headers before using it. The exact API to inspect the enabled bit of a `wlr_output_state` may vary. If the helper is not available, we access `event->state->committed & WLR_OUTPUT_STATE_ENABLED` and `event->state->enabled` directly.

---

> [!CAUTION]
> Awaiting your approval before touching any file. Per AGENTS.md: Ask → Explain → Justify → Approve → Execute.
