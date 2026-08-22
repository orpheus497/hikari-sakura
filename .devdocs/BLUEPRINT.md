# Hikari Project Blueprint

*Last Updated:* 2026-08-22 15:02

## 1. System Architecture

```text
                                  +-----------------------+
                                  |     main() Entry      |
                                  +-----------+-----------+
                                              |
                                              v
                                  +-----------------------+
                                  |   src/server.c        |
                                  |   (Server Lifecycle)  |
                                  +-----------+-----------+
                                              |
           +----------------------------------+----------------------------------+
           |                                  |                                  |
           v                                  v                                  v
+---------------------+            +---------------------+            +---------------------+
|  src/output.c       |            |   src/view.c        |            |  src/keyboard.c     |
|    (wlr_scene)      |            |   src/workspace.c   |            |  src/pointer.c      |
|                     |            | (Views/Sheets/Tiles)|            |  (Input Events)     |
+---------------------+            +---------------------+            +---------------------+
```

### The Hierarchy (Server -> Workspace -> Sheet -> Group -> View)

Hikari organizes its display logic hierarchically:
- **Server (`server.c`)**: The global singleton state holding the wlroots backend, renderer, allocator, scene graph, seat, and global linked lists for outputs, views, keyboards, and pointers.
- **Workspace (`workspace.c`)**: Corresponds 1:1 with a physical output (monitor). Each workspace maintains an array of `hikari_sheet` structures.
- **Sheet (`sheet.c`)**: Analogous to traditional "virtual desktops" or "tags". Hikari allocates 10 sheets (0-9) per workspace. Sheets contain lists of views and an optional tiling layout (`hikari_layout`).
- **Group (`group.c`)**: Views are organized into groups (usually based on `app_id`). Groups allow batch operations like hiding, raising, or lowering multiple windows at once.
- **View (`view.c`)**: The abstract base class representing a single window. It handles geometry, constraints, borders, and indicators. Subclasses implement specific shell protocols (`xdg_view.c`, `xwayland_view.c`).

### Rendering and Scene Graph (`wlr_scene`)

Hikari delegates the heavy lifting of rendering and Z-ordering to the `wlr_scene` API.
- Backgrounds, borders, views, and overlays (like the lock indicator or indicator bar) are represented as `wlr_scene_node`s.
- **Since Phase 73 the scene root holds six named layer trees** — `background`, `bottom`, `views`, `top`, `overlay`, `lock` (bottom-to-top), in `hikari_server.layers`. Nothing may parent itself to `scene->tree` directly. This replaced a flat sibling list whose z-order was maintained by scattered one-shot `raise_to_top()`/`lower_to_bottom()` calls that competed with each other; a raise can no longer escape its layer, and lock mode became a `set_enabled()` toggle rather than a per-view walk. See §12.25a.
- `output.c` connects the `wlr_scene` to the physical `wlr_output`.
- Two distinct node strategies are in use: **solid-colour rects** and **cairo-rendered buffers**.
  - `border.c` and `indicator_frame.c` use `wlr_scene_rect_create` — 4 solid-colour rects per frame, no cairo, no pixel buffer.
  - `indicator_bar.c` and `lock_indicator.c` render text/shapes with `cairo`/Pango, copy the pixels into a `wlr_buffer` via `hikari_server_create_argb8888_buffer()`, and attach it with `wlr_scene_buffer_create`. `output.c` uses its own `wlr_buffer_impl` for the background.
- Child nodes are positioned **parent-relative**. A view's `scene_tree` is placed at the view's layout-absolute origin, so nodes beneath it (borders, indicator frames) must use offsets relative to that origin, not absolute layout coordinates.

### Configuration (`configuration.c`)

All configuration is parsed using `libucl` (a universal configuration library JSON/UCL parser). The configuration is loaded into typed structs (e.g., `hikari_view_config`, `hikari_keyboard_config`) during startup and can be reloaded via SIGHUP or keybindings.

## 2. Modal State Machine Index

Input handling (keyboard and pointer) does not happen globally. Instead, `server.c` tracks a `hikari_mode *mode`. When a key or button is pressed, the event is dispatched to `hikari_server.mode->key_handler` or `button_handler`.

| Mode | Handler File | Description |
|---|---|---|
| **Normal Mode** | `src/normal_mode.c` | Default state. Keybindings and mouse gestures dispatch window actions. |
| **Move Mode** | `src/move_mode.c` | Interactive view repositioning (via cursor drag or keybinding). |
| **Resize Mode** | `src/resize_mode.c` | Interactive view resizing (via cursor drag or keybinding). |
| **Lock Mode** | `src/lock_mode.c` | Screen locker overlay active; dispatches input to lock screen indicator. Uses IPC pipes to external `hikari-unlocker` PAM helper. |
| **Sheet Assign Mode** | `src/sheet_assign_mode.c` | Modal prompt for reassigning focused view to target sheet (0-9). |
| **Group Assign Mode** | `src/group_assign_mode.c` | Modal prompt for assigning focused view to named group. |
| **Mark Assign Mode** | `src/mark_assign_mode.c` | Modal prompt for tagging focused view with quick-jump mark (a-z). |
| **Mark Select Mode** | `src/mark_select_mode.c` | Modal prompt for jumping focus to tagged view mark. |
| **Layout Select Mode** | `src/layout_select_mode.c` | Modal prompt for applying tile layout algorithms. |
| **Input Grab Mode** | `src/input_grab_mode.c` | Redirects input to explicit client surface request. |
| **DnD Mode** | `src/dnd_mode.c` | Tracks drag-and-drop offers while a drag operation is active. |

## 3. Comprehensive Subsystem Breakdown

### Core Initialization & Server Lifecycle

- **`server.c`**: The absolute core. Initializes the `wl_display`, `wlr_backend`, `wlr_renderer`, `wlr_allocator`, and `wlr_scene`. Sets up the `wl_listener` callbacks for new outputs and inputs. **Issue context:** This is where `wlr_backend_start` is called. If the DRM backend fails to initialize the rendering swapchain for `eDP-1`, the compositor invokes a hard `exit(EXIT_FAILURE)`, hiding specific DRM error context.
- **`output.c`**: Handles physical monitor hotplugging (`wlr_output`). Connects the `wlr_output` to a `hikari_workspace` and the `wlr_scene_output`. Generates the background buffer using cairo.
- **`memory.c`**: Fail-fast wrappers around `malloc`, `calloc`, and `free` — allocation failure prints a sized diagnostic and `abort()`s; callers never see NULL.

### View Abstractions & Shell Protocols

- **`view.c`**: The base class for all windows. Manages Z-ordering (raise/lower), moving, resizing, and maximizing. Defines the `wlr_box` geometry boundaries.
- **`xdg_view.c`**: Implements the Wayland native `xdg_shell` protocol. Maps `wlr_xdg_surface` to a `hikari_view`. Handles popup menus and fullscreen requests.
- **`xwayland_view.c` / `xwayland_unmanaged_view.c`**: Handles legacy X11 applications. Maps `wlr_xwayland_surface` to `hikari_view`. Handles X11 window hints, configure requests, and unmanaged surfaces (like tooltips or menus).
- **`layer_shell.c`**: Implements the `wlr_layer_shell_v1` protocol used by panels, wallpapers, and notification daemons (e.g., `waybar`, `mako`). Manages exclusive zones (margins) to prevent windows from covering panels.

### Workspace, Sheet, and Tiling Layouts

- **`workspace.c`**: Manages the 10 `hikari_sheet` instances per output. Handles sheet switching, view snapping (snapping a window to the edge of another window), and view focus.
- **`sheet.c`**: Contains lists of views and an active `hikari_layout`.
- **`group.c`**: Organizes views by `app_id`. Used for mass-visibility toggles.
- **`layout.c` / `split.c` / `tile.c`**: The tiling engine. `layout.c` defines the macro layout state, `split.c` handles the mathematical division of screen real estate (`hikari_geometry_split_vertical/horizontal`), and `tile.c` wraps a `hikari_view` to enforce its tiled geometry.

### Input Devices

- **`keyboard.c`**: Wraps `wlr_keyboard`. Translates `xkbcommon` keycodes and manages keyboard modifiers. Forwards key events to the current `hikari_mode`.
- **`pointer.c`**: Wraps `wlr_pointer`. Configures `libinput` parameters (acceleration, natural scrolling, tap-to-click).
- **`cursor.c`**: Wraps `wlr_cursor` and `wlr_xcursor_manager`. Tracks absolute/relative cursor coordinates across the global output layout. Handles cursor themes and warping.

### Configuration Parsing (`libucl`)

- **`configuration.c`**: The master parser for `hikari.conf`. Evaluates the UCL tree.
- **`action_config.c` / `binding_config.c` / `binding_group.c`**: Parses key combinations (e.g., `L-S-Enter`) and maps them to `hikari_action` function pointers.
- **`keyboard_config.c` / `pointer_config.c`**: Parses `xkb` layouts (rules, model, layout, variant, options) and `libinput` configurations.
- **`output_config.c` / `position_config.c` / `view_config.c` / `layout_config.c` / `switch_config.c`**: Parses rules for specific outputs or application classes (e.g., forcing a window to be floating, or setting a specific background image).

### UI Components, Overlays & Geometry

- **`border.c`**: Renders borders around windows as 4 solid-colour `wlr_scene_rect` nodes (no cairo). Updates border colors based on active/inactive focus states.
- **`indicator.c` / `indicator_bar.c` / `indicator_frame.c`**: Renders the UI overlay that appears when switching sheets or modifying groups. Renders text using Pango/Cairo.
- **`lock_indicator.c`**: Renders the circular typing/verifying/denied indicator during screen lock.
- **`decoration.c`**: Handles server-side decorations (SSD) via `wlr_xdg_decoration_manager_v1`.
- **`font.c`**: Pango font measurement wrapper.
- **`geometry.c`**: Math utilities for constraining `wlr_box` structures within output usable areas.
- **`mark.c`**: Registry for the 'a'-'z' window marks.
- **`maximized_state.c`**: Tracks geometry prior to a window being maximized.
- **`action.c` / `command.c` / `exec.c`**: Maps parsed configuration strings to actual C function pointers. `command.c` executes shell commands using double-forking (`fork() -> fork() -> setsid() -> exec()`) to prevent zombie processes.
- **`completion.c` / `input_buffer.c`**: Handles string manipulation for interactive prompts.

## 4. Codebase Strengths & Weaknesses

**Strengths:**
1. **Pristine Segregation of State**: The use of `hikari_mode` as a state machine for input is brilliant. Instead of spaghetti `if (is_locked)` checks scattered throughout the codebase, inputs are elegantly routed to the active mode's `key_handler` vtable.
2. **Robust WLRoots 0.20 API Adherence**: The codebase perfectly utilizes the modern `wlr_scene` graph. It delegates complex damage tracking, matrix math, and Z-ordering entirely to wlroots, keeping the compositor logic small and clean.
3. **Zero Zombie Processes**: `command.c` implements the classic "double-fork" technique flawlessly to orphan child processes to `init`, completely eliminating zombie processes when launching user applications.
4. **Security Conscious**: `lock_mode.c` correctly uses `mlock()` and `munlock()` to ensure that the password buffer is never paged to disk/swap.

**Identified Issues:**
1. **Output-Commit Failure (loud since Phase 25)**: a failed `wlr_output_commit_state()` during initial modeset still disables the output via early return (`src/output.c`, `hikari_output_init`), leaving the session alive on remaining/noop outputs — but the path is no longer silent: stderr now names the failed output, matching the backend-start guard style (diagnostic + `exit(EXIT_FAILURE)`, `src/server.c:1071-1077`). No renderer fallback (e.g. pixman) is attempted if GLES2 scanout setup fails.
2. **Hardcoded Limits**:
   - `workspace.c` hardcodes exactly 10 sheets (`HIKARI_NR_OF_SHEETS`). 
   - `mark.c` hardcodes exactly 26 marks ('a' through 'z').
3. **UI Buffer Allocation Strategy**: In `indicator_bar.c` and `lock_indicator.c`, the compositor uses `cairo` to render UI elements, maps the memory, copies it to a `wlr_buffer` via `hikari_server_create_argb8888_buffer()`, and attaches it to the scene graph. This is CPU intensive — redrawing the circular `lock_indicator.c` on every timer tick via cairo software rendering is suboptimal compared to a simple GLES2 shader. (Borders and indicator frames are *not* affected: they use solid-colour `wlr_scene_rect` nodes and allocate no buffers.)
4. **Missing Graceful Degradation on XWayland**: If XWayland crashes, the `wlr_xwayland_surface` callbacks can sometimes trigger use-after-free bugs if the destruction signals aren't perfectly synchronized. The transition to the `wlr_scene` API mitigates this visually, but logical structs can leak.

**Known Limitations (verified, not defects):**

- P2-15: `sig_handler` → `hikari_server_terminate` → `wl_event_loop_add_timer` performs allocation in signal context — async-signal-unsafe, upstream-inherited design, noted only (`src/server.c:1046-1049`, `src/server.c:1137-1140`).

## 5. The eDP-1 Swapchain / DRM Failure Analysis (HISTORICAL — resolved, see §13 FB-4)

> **Closed 2026-08-22 (Phase 83).** The laptop's built-in panel has worked for a long time; this failure was fixed by movement in the graphics stack below hikari (Mesa is now 26.1.6, libdrm 2.4.134) and no code change was ever needed. The analysis below is retained for its method and because the H0 hypothesis explains what was observed at the time -- but **none of it describes current behaviour**, and the H1/H2/H3 discrimination matrix it calls for should not be run.

*Corrected 2026-08-13 (Phase 22) against the Phase 19 live evidence and the current source. An earlier draft of this section misattributed the failure to `wlr_backend_start()` and quoted a diagnostic string that does not exist in the tree.*

The primary runtime blocker on FreeBSD is the failure of the `eDP-1` output during the **output commit**, not during backend start.

### The Verified Code Path (live-tested, Phase 19)

1. `main()` → `hikari_server_prepare_privileged()` → `wlr_backend_autocreate()` (`src/server.c:821`) — **succeeds live** (seatd/session acquired).
2. `server_init()` → `wlr_renderer_autocreate()` (`src/server.c:946`), `wlr_allocator_autocreate()` (`src/server.c:954-955`) — **succeed live**.
3. `hikari_server_start()` → `wlr_backend_start()` (`src/server.c:1070`) — **succeeds live**; the P0-2 guard (diagnostic + `exit(EXIT_FAILURE)`, `src/server.c:1071-1077`) correctly stays silent. Connectors probe fine.
4. `new_output_handler` → `hikari_output_enable()` → enable + EDID-preferred mode → `wlr_output_commit_state()` (`src/output.c:350`) — **THIS FAILS**: wlroots reports `Swapchain for output 'eDP-1' failed test` (`types/output/swapchain.c:109`) — the GBM scanout-alloc / KMS FB-import test.
5. The commit returns false and hikari takes an early return (`src/output.c`, `hikari_output_init`): the process stays alive on the noop output — a dark-but-alive session. Since Phase 25 the path is **loud**: stderr names the failed output before the return.
6. Companion non-fatal error: `eglQueryDeviceStringEXT(EGL_DRM_DEVICE_FILE_EXT)` fails (wlroots `render/egl.c:508`, reached from `wlr_renderer_init_wl_display`, `src/server.c:952`) — the EGL device lacks `EGL_EXT_device_drm`, so dmabuf device feedback is lost and clients degrade to wl_shm.

### Ranked Hypotheses (discrimination needs the user-run diagnostics matrix in TODOS)

- **H0 (NEW, Phase 70 — now the primary hypothesis, outranking H1):** **hybrid-graphics device selection.** This machine carries two KMS devices — `card0`/`renderD128` = Intel TigerLake-LP GT2 Iris Xe (`8086:9A49`, `i915kms.ko`, **eDP-1 is attached here**) and `card1`/`renderD129` = NVIDIA TU117M GTX 1650 Ti (`10DE:1F95`, `nvidia-drm.ko`) — with `hw.nvidiadrm.modeset=1` in `/boot/loader.conf` and `kld_list="i915kms nvidia-modeset nvidia-drm …"` in `/etc/rc.conf`. Because NVIDIA registers a *full* DRM/KMS device, wlroots' DRM backend enumerates both; whichever it picks first becomes primary and owns the renderer, and the other becomes a secondary multi-GPU device requiring cross-device buffer import. If NVIDIA wins, the eDP-1 connector on the Intel device must scan out buffers allocated against NVIDIA's proprietary GBM, which does not export modifiers i915 can import — **exactly the observed `Swapchain for output 'eDP-1' failed test`.** Phase 19 never considered multi-GPU. **Corroborated by item 6 above:** `eglQueryDeviceStringEXT(EGL_DRM_DEVICE_FILE_EXT)` failing for want of `EGL_EXT_device_drm` is expected of NVIDIA's EGL and *not* of Mesa's — so a single cause explains both log lines, which is the property Phase 19 sought in H1 and misattributed to a broken Mesa. Falsifiable in one run: `WLR_DRM_DEVICES=/dev/dri/card0`. Tracked as **FB-3** in §13.
- **H1:** Mesa DRI/GBM backend broken for the GPU (missing/mismatched `mesa-dri`, or drm-kmod/firmware fault) — one cause explains both log lines. *Demoted by H0, which explains the same two lines without requiring Mesa to be broken.* Installed here: Mesa 26.1.6, libdrm 2.4.134.
- **H2:** `IN_FORMATS` modifier-set mismatch between drm-kmod and Mesa GBM.
- **H3:** BO alloc succeeds but `drmModeAddFB2WithModifiers` fails (EINVAL).

Ruled out live (Phase 19): hikari API misuse (commit sequence matches tinywl/wlroots 0.20), seatd/session and `/dev/dri` permissions, configuration load, ZFS/`posix_fallocate` (compositor scanout is GBM/KMS, not shm).

Discrimination knobs, verified present in the installed `libwlroots-0.20.so` (Phase 70): `WLR_DRM_DEVICES`, `WLR_RENDER_DRM_DEVICE`, `WLR_DRM_NO_MODIFIERS`, `WLR_EGL_NO_MODIFIERS`, `WLR_DRM_NO_ATOMIC`, `WLR_RENDERER`, `WLR_RENDERER_ALLOW_SOFTWARE`, `WLR_NO_HARDWARE_CURSORS`, `WLR_SCENE_DEBUG_DAMAGE`, `WLR_SCENE_DISABLE_DIRECT_SCANOUT`.

## 6. Launcher & Session Integration Architecture

*Consolidated 2026-08-13 (Phase 22) from the retired Phase 18/21 investigation report; every claim verified against the tree.*

**Why both `hikari` and `start-hikari` exist:** deliberate separation of concerns, not duplication. The compositor natively owns everything a wlroots compositor should; the wrapper supplies the session environment that, by architecture, is established before or around the compositor.

| Capability | Owner | Evidence |
|---|---|---|
| Seat/session (seatd) | **Compositor-native** — `wlr_backend_autocreate()` → libseat | `src/server.c:821` |
| Wayland socket | **Compositor-native** — `wl_display_add_socket_auto()` | `src/server.c:961` |
| `WAYLAND_DISPLAY` / `DISPLAY` propagation to children | **Compositor-native** — `setenv` | `src/server.c:967`, `src/server.c:507` |
| Lock-screen PAM auth | **Compositor-native (auth-only)** — setuid `hikari-unlocker` runs `pam_start`/`pam_authenticate`/`pam_end`; no session stack exists anywhere in-tree | `hikari_unlocker.c:85/134/153` |
| D-Bus session bus | **Session layer** — separate `dbus-daemon`; zero dbus references in-tree; wrapper guards with `dbus-run-session` | `start-hikari.sh:111-115` |
| Portal activation env (`XDG_CURRENT_DESKTOP`) | **Session layer** — wrapper for TTY starts; `DesktopNames=Hikari` for display managers | `start-hikari.sh:26`, `share/wayland-sessions/hikari.desktop:6` |
| `XDG_RUNTIME_DIR` creation | **Login layer** — pam_xdg in the login-time PAM stack; wrapper only bootstraps/validates (owner, 0700) and warns on ZFS | `start-hikari.sh:31-81` |
| Nested-backend guard | **Session layer** — `unset WAYLAND_DISPLAY`/`DISPLAY` (Phase 19 run 1 proved its necessity) | `start-hikari.sh:13-14` |

Key points:

- Hikari's PAM usage is **auth-only** (screen unlock). The session-establishing PAM stack (pam_xdg → `XDG_RUNTIME_DIR`) belongs to the login layer, which runs before hikari exists; a non-root compositor (asserted at `main.c:254-255`) cannot perform it post-hoc. "Uses PAM" does not imply "owns the session PAM stack".
- The wrapper is fully conditional/idempotent: under a display manager it reduces to a pass-through plus the D-Bus guard. Install wiring: `Makefile:264` (installs `start-hikari`), `Makefile:278` (desktop entry rewritten to the absolute wrapper path).
- The 2026-07-31 12:47 revert of native `setup_env()` bootstrapping (the Phase 11 experiment) is re-affirmed on complete evidence (Phase 21 analysis).

## 7. Implementation Registry

* **Standard Memory Allocation:** Migrated compositor allocation paths to `hikari_malloc`.
* **wlr_scene Rendering:** Migrated borders, lock indicator, backgrounds, and indicator bars to wlroots `wlr_scene` graph.
* **wlroots 0.20 Migration:** Resolved 13+ API-breaking changes across `cursor.c`, `output.c`, `server.c`, `switch.c`, and `xdg_view.c`.
* **FreeBSD Adaptations:** Native evdev headers adopted, Makefile points to wlroots 0.20 via pkg-config with epoll-shim conditional.
* **DOD Reverted:** Struct-of-Arrays (SoA) and Object Pool implementations were removed as they added unnecessary complexity and are incompatible with `wlr_scene` workflows.
* **Non-Blocking PAM I/O:** Replaced blocking `read()` with `wl_event_loop_add_fd()` in lock mode. Added `locker_result_handler` callback.
* **Phase 13 Audit Fixes:** Switch toggle cascading-if bug, output cairo surface check, duplicate includes, blocking `wait()` → `waitpid(WNOHANG)`, `output->server` initialization robustness, `main.c` comment prefix migration.
* **Phase 14 Comprehensive Audit:** BUG-1 `move_resize_view()` dx/dy fix, BUG-2 `outputs_disabled` stale state, BUG-3 `command.c` waitpid errno, BUG-4 stale comment removal. Security: `explicit_bzero`. Robustness: EINTR-retrying pipe write. 5 missing listener cleanups in `hikari_server_stop()`. Dead code removal (render.h, mode_handler, unused struct members). Desktop entry `DesktopNames=Hikari`.
* **Phase 15 Binary Resolution Fix:** `start-hikari.sh` derives `SCRIPT_DIR` from script's own location and checks sibling `${SCRIPT_DIR}/hikari` before PATH or `./hikari`.
* **Phase 16 Review Fixes:** Fatal error guard on `SCRIPT_DIR` derivation; README tmpfs verification switched from macOS-only `stat -f '%T'` to portable `mount | grep`.
* **Phase 17 Review Fixes:** Markdown table pipe escaping in `SESSION_HANDOFF.md` (GFM/markdownlint compliance); README tmpfs troubleshooting broadened to cover all setup steps (fstab entry, reboot), not just step 1.
* **Phase 18 Runtime Failure Investigation:** Full static root-cause analysis of post-login crash / black-screen+dead-input symptoms. Identified 4 release-blockers (hallucinated `xkb_map_new_from_names` symbol; unchecked `wlr_backend_start()`; wrong `wlr_headless_backend_create()` argument with false API comment; missing `etc/hikari/hikari.conf` + wallpaper assets), 3 P1 and 8 P2 defects. (Standalone report retired in Phase 22 — consolidated into §5/§6 and the trackers.)
* **Phase 18b Remediation:** All 15 investigation defects + 3 build-discovered defects fixed and annotated (register: SESSION_HANDOFF Phase 18b; archived runtime investigation retired in Phase 22). New default `etc/hikari/hikari.conf` authored against the verified parser grammar. Layer shell integrated with the scene graph (`wlr_scene_layer_surface_v1_create`, z-order by layer class, layout-global positioning, map/unmap visibility). Xwayland migrated to the wlroots 0.20 lifecycle (`associate`-deferred map/unmap registration, `xcb_size_hints_t` constraints). Popup geometry migrated to `popup->current.geometry`. TC-BUILD-01/02 both pass from clean trees.
* **Phase 19 Runtime Triage:** First live TTY test (2026-08-13). Session, backend start, renderer, allocator, and connector probe verified live; startup halts at the eDP-1 scanout swapchain test — localized to the Mesa/EGL/GBM ↔ drm-kmod layer, not hikari code. Ranked hypotheses H1/H2/H3 with a user-run diagnostics matrix (TODOS active list). No code changes.
* **Phase 21/22 Validity Audit, Launcher Analysis & Consolidation:** Runtime-failure findings audited for current validity; the launcher/session architecture analysis (compositor-native vs session-layer responsibilities) lives in §6, the corrected eDP-1 failure analysis in §5, and residual open items in TODOS (P2-14 added). The archived runtime investigation was retired and the devdocs restored to the AGENTS.md 7-file structure. 2026-07-31 12:47 `setup_env()` revert re-affirmed on complete evidence. Documentation-only phases.
* **Phase 25 Hardening (Phase 24 P0/P1 backlog):** Unknown `outputs` keys now fail the parse (was log-and-continue, `src/configuration.c`); `parse_switches` frees its UCL iterator (`src/configuration.c`); lock-helper child exits `_exit(EXIT_FAILURE)` with a stderr diagnostic after a failed `execl` (was `exit(0)`, `src/lock_mode.c`); failed initial output commit now names the output on stderr (was silent, `src/output.c`). TC-BUILD-01/02 pass, 0 errors; edited files warning-clean.
* **Phase 26 Hardening (Phase 24 P2/P3 backlog):** CSD damage granularity — whole-output early-outs removed from `hikari_view_damage_whole`/`hikari_view_damage_surface`; CSD main surface damaged by buffer extents via the per-surface iteration (`src/view.c`). Allocation policy resolved fail-fast (user-directed) — `hikari_malloc`/`hikari_calloc` sized diagnostic + `abort()` on NULL (`src/memory.c`, never-NULL contract in `include/hikari/memory.h`). Changelog `wloots` typos fixed (`CHANGELOG.md`). TC-BUILD-01/02 pass, 0 errors; edited files warning-clean. Phase 24 hardening stream closed at 7/7.
* **Handbook Verification:** FreeBSD Handbook Ch.6 §6.1-6.4 cross-referenced — all requirements verified correct.
* **Test Specifications:** Added build compilation (TC-BUILD-01), pkg-config dependencies (TC-PKG-01), and manual protocols for Evdev, Shared Memory, and PAM.

## 8. Test Specifications & Verification Framework

| Test Case ID | Test Target | Description | Expected Outcome | Status |
|--------------|-------------|-------------|------------------|--------|
| `TC-BUILD-01` | `Makefile` (`bmake`) | FreeBSD build compilation test (wlroots 0.20, Phase 6) | Clean compilation of `hikari` and `hikari-unlocker` | Passed ✓ (2026-08-13 05:41 — `make clean && make`, 0 errors, both binaries linked; revalidated after P0-1 fix) |
| `TC-BUILD-02` | `Makefile` full-feature flags | Compilation of all feature-gated paths (`WITH_XWAYLAND/LAYERSHELL/SCREENCOPY/GAMMACONTROL/VIRTUAL_INPUT=YES`) | Clean compilation + link | Passed ✓ (2026-08-13 05:38 — 0 errors; first time feature configs ever compiled in this tree; surfaced and fixed P1-16/17/18) |
| `TC-PKG-01` | `pkg-config` | Resolve dependencies: `wlroots-0.20`, `pango`, `cairo`, `pixman`, `xkbcommon`, `libinput`, `libucl`, `epoll-shim` | All CFLAGS and LIBS resolved without missing packages | Passed ✓ (Phase 6 — dependency set unchanged) |
| `TC-RUNTIME-01` | TTY bring-up via `start-hikari` (seatd) | Session → backend → renderer → allocator → connector probe → modeset + first frame | Full session with working output and input | **Blocked** (2026-08-13) — passes through connector probe; fails at the eDP-1 scanout swapchain test (environmental per Phase 19 triage) |
| `TC-DOC-01` | `AGENTS.md` Linter | Source prefix audit: every script/source, standalone function, and specific action block has the exact `[COMMENT] Script function and purpose:`, `[COMMENT] Function purpose:`, or `[COMMENT] Action purpose:` prefix annotation | All modified files comply with the three defined prefixes | Passed ✓ (Phase 13 — all modified files verified, `main.c` `##` prefixes migrated) |
| `TC-FORMAT-01` | `clang-format` | Code formatting compliance | Zero formatting diffs against `.clang-format` rules | Pending |

### FreeBSD Manual Verification Protocol

#### Test Protocol 1: Evdev Input Device Initialization

1. Verify `/etc/sysctl.conf` contains `kern.evdev.rcpt_mask=12` (or `3` if `moused` enabled).
2. Inspect `/dev/input/event*` nodes permissions.
3. Launch `hikari` and confirm single mouse pointer movement without duplicate cursor offset drift.

#### Test Protocol 2: Shared Memory & `XDG_RUNTIME_DIR` Allocation

1. Mount `tmpfs` at a dedicated test mountpoint (e.g. `mount -t tmpfs tmpfs /mnt/test-tmpfs`).
2. Set `export XDG_RUNTIME_DIR=/mnt/test-tmpfs/runtime-${USER}` and create the directory with restrictive permissions (`mkdir -p -m 0700 "$XDG_RUNTIME_DIR"`).
3. Launch Wayland client (e.g. `alacritty` or `firefox`).
4. Confirm surface buffer allocations succeed via `posix_fallocate`.
5. Add explicit cleanup to unmount the temporary filesystem after testing (`umount /mnt/test-tmpfs`).

#### Test Protocol 3: PAM Unlocker Security (`hikari-unlocker`)

1. Verify `/usr/local/etc/pam.d/hikari-unlocker` exists.
2. Identify the canonical absolute path for the `hikari-unlocker` binary. Verify that this exact binary has trusted package provenance, root ownership (e.g., `root:wheel`), and the expected permissions. Only apply mode 4555 (e.g. `chmod 4555 /usr/local/bin/hikari-unlocker`) after all these checks pass.
3. Trigger lock mode (`Meta+L`) in `hikari`.
4. Input user password and verify session unlocks cleanly upon PAM authentication success.

## 9. Resources Index

Read and cross-reference the content against the codebase.
* https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/Getting-started
* https://wayland-book.com/
* https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/tinywl
* https://drewdevault.com/blog/Writing-a-Wayland-compositor-1/
* https://drewdevault.com/blog/Writing-a-wayland-compositor-part-2/
* https://drewdevault.com/blog/Input-handling-in-wlroots/
* https://drewdevault.com/blog/Wayland-shells/
* https://emersion.fr/blog/2019/intro-to-damage-tracking/
* https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/Packaging-recommendations
* https://www.phoronix.com/news/wlroots-0.20-Released
* https://docs.freebsd.org/en/books/handbook/wayland/
* https://codeberg.org/thomasadam/hikari

## 10. Phase 24 Deep Audit Addendum (2026-08-13)

### Wiring Verdict

- Startup, backend/session acquisition, renderer/allocator bring-up, output registration, scene graph attachment, seat/input wiring, and mode dispatch are concretely implemented in code paths exercised by the server lifecycle.
- FreeBSD launch/session integration (`start-hikari`, PAM unlock helper, desktop session entry, install wiring) is real and coherent.
- No fake/simulated placeholder subsystem was found in active core compositor paths.

### Empty-Handler Classification

- Multiple mode files intentionally use no-op callbacks for channels they do not consume in that modal state (e.g., suppressing pointer/button behavior while waiting for key confirmation).
- These no-op handlers are architectural modal boundaries, not missing implementation stubs in most cases.

### Actionable Risks Captured

1. ~~Unknown `outputs` config keys log but still pass parse~~ — **Resolved (Phase 25):** parse now fails (`src/configuration.c`).
2. ~~`parse_switches` iterator without explicit free~~ — **Resolved (Phase 25):** iterator freed (`src/configuration.c`).
3. ~~Lock helper child exits success after failed `execl("hikari-unlocker")`~~ — **Resolved (Phase 25):** stderr diagnostic + `_exit(EXIT_FAILURE)` (`src/lock_mode.c`).
4. ~~Output commit failure branch too quiet~~ — **Resolved (Phase 25):** loud stderr diagnostic naming the output (`src/output.c`).
5. ~~Two TODO-tagged CSD damage paths still over-damage whole outputs (`src/view.c`).~~ — **Resolved (Phase 26):** granular per-surface damage for CSD; main surface damaged by buffer extents.
6. ~~Allocation wrappers are plain `malloc`/`calloc` pass-through; many callsites assume success.~~ — **Resolved (Phase 26):** fail-fast wrappers (sized diagnostic + `abort()`); NULL unreachable at callsites.

### Documentation Drift

- ~~Changelog contains `wloots` typo entries~~ — **Resolved (Phase 26):** `wlroots` restored at both sites; build/docs alignment remains wlroots 0.20.

## 11. Core Codebase Mechanical Mapping (File & Function Level)

*This section provides an exhaustive, 100% complete mechanical wiring map of the `hikari` compositor, tracing the core lifecycle, output, view, and input subsystems file-by-file and function-by-function. This explicitly avoids repeating the high-level conceptual architecture outlined in Sections 1-6.*

### 11.1 Server Lifecycle, Event Loop, and Global State

- **`src/server.c`**: The central orchestrator.
  - `main()`: Entry point; validates environment, drops privileges, initializes Wayland display, invokes `hikari_server_prepare_privileged()`.
  - `hikari_server_prepare_privileged()`: Acquires `seatd` session via `wlr_backend_autocreate()`.
  - `server_init()`: Creates `wlr_renderer`, `wlr_allocator`, `wlr_scene`. Registers `new_output` and `new_input` listeners.
  - `hikari_server_start()`: Executes `wlr_backend_start()` to begin the event loop; explicit error handling on failure.
  - `hikari_server_stop()`: Safely unregisters global listeners (layer shell, XWayland, decorations, virtual inputs) and shuts down display.
  - `new_input_handler()`: Routes incoming `wlr_input_device` structs to `hikari_keyboard_init()`, `hikari_pointer_init()`, or `hikari_switch_init()`.
- **`src/memory.c`**: Allocation wrappers.
  - `hikari_malloc()` / `hikari_calloc()`: Wraps system allocators; implements a fail-fast policy (prints stderr diagnostic and calls `abort()`) ensuring NULL is never returned to the compositor.

### 11.2 Rendering, Outputs, and Backgrounds

- **`src/output.c`**: Hardware output management.
  - `new_output_handler()`: Intercepts `wlr_backend` new output events; wraps `wlr_output` in `hikari_output`.
  - `hikari_output_init()`: Executes `wlr_output_commit_state()` to test the preferred modeset (eDP-1 swapchain test point). Fails gracefully with stderr log if modeset is rejected.
  - `hikari_output_damage_whole()` / `hikari_output_add_damage()`: Notifies `wlr_scene_output` of damage regions.
  - `frame_handler()`: The vsync callback executing `wlr_scene_output_commit()` and `wlr_scene_output_send_frame_done()`.
- **`src/workspace.c`**: Output-to-desktop mapping.
  - `hikari_workspace_init()`: Wires an output to 10 `hikari_sheet` instances.
  - `hikari_workspace_focus_view()`: Modifies global focus state across all sheets on the output.

### 11.3 Core View Abstraction & Hierarchy

- **`src/view.c`**: The base window abstraction.
  - `hikari_view_damage_whole()` / `hikari_view_damage_surface()`: Granular damage bounding box (`wlr_box`) calculators. Differentiates SSD (border extents) and CSD (buffer extents).
  - `hikari_view_move()` / `hikari_view_resize()`: Safely mutates `view->geometry`.
  - `hikari_view_raise()` / `hikari_view_lower()`: Manipulates `wlr_scene_node_raise_to_top()` and `lower_to_bottom()`.
- **`src/sheet.c`**: Virtual desktop abstraction.
  - `hikari_sheet_init()`: Prepares the view lists and ties them to a workspace.
  - `hikari_sheet_add_view()` / `hikari_sheet_remove_view()`: Manages the visibility of views mapped to this specific sheet.
- **`src/group.c`**: App_id based grouping.
  - `hikari_group_init()`: Groups identical client apps for mass visibility toggles.
- **`src/maximized_state.c`**: Geometry state caching.
  - `hikari_maximized_state_save()`: Caches pre-maximized `wlr_box` geometry so views can be safely restored.

### 11.4 Tiling & Layout Engine

- **`src/layout.c`**: The macro tiling engine.
  - `hikari_layout_apply()`: Triggers the recursive math for tiling algorithms.
- **`src/split.c`**: Geometry subdivision math.
  - `hikari_split_vertical()` / `hikari_split_horizontal()`: Recursively divides available `wlr_box` output space into discrete fractional rectangles.
- **`src/tile.c`**: View constraint wrappers.
  - `hikari_tile_init()`: Wraps a `hikari_view` to force it to adhere strictly to the computed `split.c` geometry.

### 11.5 Shell Protocols & Client Interfaces

- **`src/xdg_view.c`**: Native Wayland shell (`xdg_shell`).
  - `new_xdg_surface()`: Filters incoming `wlr_xdg_surface` objects (toplevel vs popup).
  - `hikari_xdg_view_init()`: Instantiates the `wlr_scene_tree`, wires up `initial_commit`, `map`, `unmap`, and `destroy` listeners.
  - `initial_commit_handler()`: Prepares bounding boxes prior to buffer mapping; asserts `initialized = true`.
- **`src/xwayland_view.c` & `src/xwayland_unmanaged_view.c`**: Legacy X11 integration.
  - `hikari_xwayland_view_init()`: Checks `wlr_scene_tree_create()` for NULL failure before dereferencing. Defers `map` and `unmap` registration to the `associate` listener due to wlroots 0.20 API lifecycle.
- **`src/layer_shell.c`**: Desktop components (panels, wallpapers).
  - `new_layer_surface()`: Handles `wlr_layer_shell_v1` requests.
  - `hikari_layer_init()`: Generates `wlr_scene_layer_surface_v1_create()` node, injects it into the scene tree, and positions it according to `exclusive_zone` margins.
- **`src/decoration.c`**: Server-side decoration negotiation.
  - `new_decoration_handler()`: Answers `wlr_xdg_decoration_manager_v1` requests to enforce server-rendered borders.

### 11.6 Input Hardware Routing & Modifiers

- **`src/keyboard.c`**: Keyboard hardware interfacing.
  - `hikari_keyboard_init()`: Compiles `xkbcommon` rules and connects the `modifiers` and `key` event listeners.
  - `key_handler()`: Translates raw evdev scancodes to `xkb_keysym_t` and passes them to `hikari_server.mode->key_handler()`.
- **`src/pointer.c`**: Mouse/Touchpad interfacing.
  - `hikari_pointer_init()`: Applies `libinput` profiles (acceleration, tap-to-click) and attaches the device to the global `wlr_cursor`.
- **`src/touch.c`**: Touchscreen device lifecycle.
  - `hikari_touch_init()`: Tracks the device on `server->touches` (drives `WL_SEAT_CAPABILITY_TOUCH`). No hardware-config surface of its own (unlike pointers); `add_touch()` (`src/server.c`) attaches the device to `wlr_cursor` via `wlr_cursor_attach_input_device()`, then resolves output confinement via `wlr_touch_from_input_device(device)->output_name`.
- **`src/cursor.c`**: Pointer coordinate mapping, plus touch and gesture dispatch.
  - `cursor_button_handler()` / `cursor_motion_handler()`: Updates `wlr_cursor_move()` absolute coordinates, then delegates to `hikari_server.mode->button_handler()` or `cursor_handler()`.
  - `cursor_touch_down/motion_handler()`: Converts wlroots' normalized 0..1 touch coordinates to layout pixels via `wlr_cursor_absolute_to_layout_coords()` before hit-testing (`wlr_touch` reports device-relative coordinates, not layout coordinates — the codebase's own hit-testing helper, `hikari_server_node_at()`, only ever accepts layout coordinates elsewhere). Forwards to the focused surface via `wlr_seat_touch_notify_*`. The first touch point of a fresh multi-touch sequence (`cursor->primary_touch_id`) additionally synthesizes `BTN_LEFT` press/move/release calls into `hikari_server.mode->button_handler()`/`cursor_move()`, reusing the mouse-driven modal state machine verbatim for tap-to-focus/drag-to-move/resize; further simultaneous touch points stay pure client-forwarded input.
  - `cursor_swipe/pinch/hold_*_handler()`: Accumulates each gesture stream (`struct hikari_gesture_state`, buffering up to `HIKARI_GESTURE_MAX_UPDATES` update events) between `_begin` and `_end`. At `_end`, classifies the gesture's direction (dominant swipe axis, or pinch scale vs. 1.0) and looks it up against `hikari_configuration->gesture_binding_configs`. A match fires the bound `hikari_action` and the gesture is never sent to the client; a non-match replays the buffered `_begin`/`_update`/`_end` sequence via `wlr_pointer_gestures_v1_send_*`. Update events beyond the `HIKARI_GESTURE_MAX_UPDATES` (128) cap are silently dropped from the buffer rather than replayed, so an unmatched gesture longer than that is forwarded to the client as a truncated (not strictly verbatim) update stream.
- **`src/switch.c`**: Lid switches and tablet modes.
  - `switch_toggle_handler()`: Executes `hikari_action` macros based on hardware switch state changes.

### 11.7 The Modal State Machine (Input Delegation)

Hikari dynamically re-routes input based on the active `hikari_server.mode`.
- **`src/normal_mode.c`**: Default state. Executes user-configured `hikari_action`s via keybindings.
- **`src/lock_mode.c`**: Screen lock overlay. Suspends compositor keybindings. Pipes input non-blockingly to `hikari-unlocker` via `wl_event_loop_add_fd()` using `locker_result_handler`.
- **`src/move_mode.c` & `src/resize_mode.c`**: Interactive geometry mutators. Re-routes pointer motion to dynamically update `view->geometry`.
- **`src/dnd_mode.c`**: Wayland Drag-and-Drop. Tracks source and destination nodes during a drag operation.
- **`src/input_grab_mode.c`**: Explicit grabs. Routes all inputs exclusively to a single requesting client surface.
- **`src/sheet_assign_mode.c`, `src/group_assign_mode.c`, `src/mark_assign_mode.c`, `src/mark_select_mode.c`, `src/layout_select_mode.c`**: Modal prompt logic for interacting with internal compositor state via keystroke completion.

### 11.8 UI Overlays & Cairo Rendering

- **`src/border.c`**: Renders coloured frames as 4 solid `wlr_scene_rect` nodes (no cairo, no buffer).
  - `hikari_border_refresh_geometry()`: Repositions and resizes the 4 rects parent-relative to the view's scene tree, and sets their colour from the active/inactive configuration.
- **`src/indicator.c`, `src/indicator_bar.c`, `src/indicator_frame.c`**: Text-based UI overlays.
  - `hikari_indicator_damage()`: Calculates damage regions for Pango/Cairo rendered text popups (e.g., sheet switching).
- **`src/lock_indicator.c`**: Circular lock screen graphic.
  - `hikari_lock_indicator_damage()`: Re-renders the password validation state circles via CPU-bound cairo draws.
- **`src/font.c`**: Pango text measurement.
  - `hikari_font_get_text_extents()`: Calculates pixel widths for indicator overlays based on user font configuration.

### 11.9 Configuration, Parsing, and Action Execution

- **`src/configuration.c`**: The `libucl` entrypoint.
  - `parse_output_config()`, `parse_switches()`: Converts JSON/UCL syntax into compositor runtime C structs. Employs strict failure on unknown keys (e.g., `goto done`).
- **`src/keyboard_config.c`, `src/pointer_config.c`, `src/output_config.c`, `src/layout_config.c`, `src/view_config.c`, `src/switch_config.c`, `src/action_config.c`, `src/position_config.c`**: Discrete parsers for their respective subsystems.
- **`src/binding_config.c` & `src/binding_group.c`**: Input parsing.
  - `parse_binding()`: Translates strings like `L-S-Enter` into `xkb_keysym_t` arrays and maps them to `hikari_action` function pointers.
- **`src/action.c` & `src/exec.c`**: Core dispatch.
  - `hikari_action_execute()`: Triggers the C function assigned to a binding.
- **`src/command.c`**: Shell execution.
  - `hikari_command_execute()`: Spawns external processes using double-fork (`fork() -> fork() -> setsid() -> exec()`) to securely orphan the child to `init` and prevent zombies.
- **`src/completion.c` & `src/input_buffer.c`**: String logic.
  - `hikari_input_buffer_add()`: Safely accumulates keystrokes into strings for the modal prompts (e.g. typing a mark name).
- **`src/geometry.c`**: Math utilities.
  - `hikari_geometry_constrain()`: Clamps view bounding boxes to ensure they do not exceed the physical output extents.
- **`src/mark.c`**: A-Z memory registry.
  - `hikari_mark_set()`: Binds a specific view to a character index in a static 26-slot array.

## 12. Exhaustive Manual Codebase Mapping (Function-by-Function)

*Per explicit instruction, this section is an ongoing manual, painstakingly detailed function-by-function mapping of the codebase. It traces every struct and function signature, detailing exact `wlroots` integration.*

### 12.1 `include/hikari/server.h` & `src/server.c` (Core Orchestrator)

**Data Structures:**
- `struct hikari_server`: The central singleton (accessed via `extern struct hikari_server hikari_server`).
  - **State**: `bool cycling`, `bool track_damage`, `struct wl_event_source *shutdown_timer`.
  - **Wayland Globals**: `struct wl_display *display`, `struct wl_event_loop *event_loop`, `struct wlr_backend *backend`, `struct wlr_session *session`, `struct wlr_renderer *renderer`, `struct wlr_allocator *allocator`, `struct wlr_scene *scene`, `struct wlr_compositor *compositor`.
  - **Listeners**: `new_output`, `new_input`, `new_toplevel`, `request_set_primary_selection`, `request_set_selection`, `output_layout_change`, `new_decoration`, `new_toplevel_decoration`, `request_start_drag`, `start_drag`, `new_layer_shell_surface`.
  - **Input Modes**: `struct hikari_mode *mode` (active), and static mode instances (`normal_mode`, `lock_mode`, `move_mode`, etc.).
  - **Lists**: `outputs`, `keyboards`, `pointers`, `switches`, `groups`, `visible_groups`, `visible_views`, `toplevels`.

**Lifecycle & Init Functions (`src/server.c`):**
- `int main(int argc, char **argv)`: 
  - Validates `geteuid() != 0` (refuses to run as root) using a runtime privilege check `if (geteuid() != 0) { exit(EXIT_FAILURE); }` instead of `assert()`.
  - Drops privileges using `setuid(getuid())` and `setgid(getgid())`.
  - Sets `WAYLAND_DISPLAY`.
  - Calls `hikari_server_prepare_privileged()` to acquire seatd before finishing user mode setup.
  - Calls `hikari_server_start()` and drops into `wl_display_run()`.
- `void hikari_server_prepare_privileged(void)`:
  - Calls `wlr_backend_autocreate(hikari_server.display, &hikari_server.session)`. This strictly handles logind/seatd acquisition.
- `static void server_init(void)`:
  - Allocates core components: `wlr_renderer_autocreate()`, `wlr_allocator_autocreate()`.
  - Initializes the scene graph: `wlr_scene_create()`, `wlr_scene_attach_output_layout()`.
  - Initializes protocol managers: `wlr_xdg_output_manager_v1`, `wlr_data_device_manager`, `wlr_primary_selection_v1`.
  - Wires up `new_output_handler` and `new_input_handler` via `wl_signal_add()`.
- `void hikari_server_start(char *config_path, char *autostart)`:
  - Calls `wlr_backend_start(hikari_server.backend)`.
  - Forks the autostart process via `hikari_command_execute()`.

**Output Handling (`src/server.c`):**
- `static void new_output_handler(struct wl_listener *listener, void *data)`:
  - Casts data to `struct wlr_output`.
  - Allocates `struct hikari_output`.
  - Calls `wlr_output_init_render(wlr_output, server->allocator, server->renderer)`. Fails hard with `exit(EXIT_FAILURE)` if this fails (vital for debugging GBM/KMS).
  - Delegates to `hikari_output_init()`.

**Input Handling (`src/server.c`):**
- `static void new_input_handler(struct wl_listener *listener, void *data)`:
  - Casts to `struct wlr_input_device`. Routes based on `device->type`.
- `static void add_keyboard(struct hikari_server *server, struct wlr_input_device *device)`:
  - Allocates `struct hikari_keyboard`, calls `hikari_keyboard_init()`.
  - Applies `hikari_keyboard_config` via `hikari_keyboard_configure()`.
- `static void add_pointer(struct hikari_server *server, struct wlr_input_device *device)`:
  - Allocates `struct hikari_pointer`, calls `hikari_pointer_init()`.
  - Attaches to the global cursor: `wlr_cursor_attach_input_device()`.
- `static void add_switch(struct hikari_server *server, struct wlr_input_device *device)`:
  - Allocates `struct hikari_switch`, parses `hikari_switch_config`.

**State Machine / Modes (`src/server.c`):**
- `void hikari_server_enter_normal_mode(void *arg)`: Updates `hikari_server.mode` to `&hikari_server.normal_mode`.
- `void hikari_server_enter_lock_mode(void *arg)`: Updates `hikari_server.mode` to `&hikari_server.lock_mode`.
- *(Repeated for all modes: `move`, `resize`, `input_grab`, `sheet_assign`, etc.)*

**Workspace / View Operations (`src/server.c`):**
- `void hikari_server_move_view_up(void *arg)`: Delegates to `hikari_workspace_move_view_up()`.
- *(Macro-generated actions for `bottom_left`, `center`, `top_right` via `MOVE()` macro)*.
- *(Macro-generated actions for `decrease_view_size_up`, `snap_view_left`, etc.)*.
- `static struct hikari_node * node_at(double lx, double ly, ...)`:
  - Traces the global `lx/ly` coordinates.
  - Queries `wlr_output_layout_output_at()` to find the physical output.
  - Iterates through Layer Shell (Overlay, Top), XWayland Unmanaged, XDG Toplevels, and Layer Shell (Bottom, Background) using `topmost_of()` and `surface_at()` to find the exact intersecting `wlr_surface`.

### 12.2 `include/hikari/output.h` & `src/output.c` (Hardware Display & Rendering)

**Data Structures:**
- `struct hikari_output`: Wraps `wlr_output`.
  - **Core**: `struct hikari_server *server`, `struct wlr_output *wlr_output`, `struct wlr_scene_output *scene_output`, `struct hikari_workspace *workspace`.
  - **State**: `bool enabled`.
  - **Geometry**: `struct wlr_box geometry` (global output position and size), `struct wlr_box usable_area` (local geometry minus panels/exclusive zones).
  - **Scene Nodes**: `struct wlr_scene_buffer *background`, `struct wlr_scene_buffer *lock_indicator_node`.
  - **Listeners**: `frame`, `request_state`, `destroy`.
  - **Lists**: `layers[4]` (layer shell surfaces), `views` (mapped views), `unmanaged_xwayland_views`.

**Initialization & Modesetting (`src/output.c`):**
- `void hikari_output_init(struct hikari_output *output, struct wlr_output *wlr_output)`:
  - Allocates and links the `hikari_workspace`.
  - Initializes view and layer lists.
  - Sets up `frame`, `request_state`, and `destroy` listeners.
  - **Critical Path (Modesetting)**: Attempts to fetch `wlr_output_preferred_mode()`. Applies this mode to a `wlr_output_state` and executes `wlr_output_commit_state()`. If this fails (as seen on the eDP-1 swapchain test), it prints a loud stderr diagnostic and returns early, leaving the output structurally disabled.
  - Generates the `wlr_scene_output` and registers it with `wlr_output_layout_add()`.
  - Calls `output_geometry()` to sync coordinate space.
- `void hikari_output_enable(struct hikari_output *output)` / `hikari_output_disable(struct hikari_output *output)`:
  - Generates a `wlr_output_state` with `set_enabled(true/false)`.
  - Commits the state and safely adds/removes the `frame` and `request_state` `wl_listener` structs to prevent double-registration.

**Rendering Loop (`src/output.c`):**
- `static void frame_handler(struct wl_listener *listener, void *data)`:
  - The core vsync callback triggered by DRM.
  - Retrieves `wlr_scene_output`.
  - Calls `wlr_scene_output_commit(scene_output, NULL)` to render the scene graph to the hardware swapchain.
  - Calls `wlr_scene_output_send_frame_done()` to notify clients to render the next frame.
- `void hikari_output_damage_whole(struct hikari_output *output)` / `hikari_output_add_damage(...)`:
  - Simply invokes `wlr_output_schedule_frame()` to queue the next vsync cycle, delegating actual granular region damage math entirely to `wlr_scene`.

**Backgrounds & Buffers (`src/output.c`):**
- `void hikari_output_load_background(struct hikari_output *output, const char *path, enum hikari_background_fit background_fit)`:
  - Reads PNG via `cairo_image_surface_create_from_png`.
  - Performs CPU-bound scaling/tiling onto a target ARGB32 cairo surface.
  - **Manual Buffer Allocation**: Allocates memory via `hikari_background_buffer` receiving Cairo ARGB32 pixels and its wrapping in a `wlr_scene_buffer`, with a solid-color `wlr_scene_rect` fallback when creation fails.

**Output Layout Updates (`src/output.c`):**
- `static void output_geometry(struct hikari_output *output)`:
  - Queries `wlr_output_layout_get_box()` to derive absolute `X, Y, Width, Height`.
  - Repositions the `output->background->node` to match.

### 12.3 `include/hikari/view.h` & `src/view.c` (Core Window Abstraction)

**Data Structures:**
- `struct hikari_view`: The abstract base class representing any managed window.
  - **Pointers**: `struct hikari_sheet *sheet`, `struct hikari_group *group`, `struct hikari_mark *mark`, `struct hikari_output *output`.
  - **WLRoots Integration**: `struct wlr_surface *surface`, `struct wlr_scene_node *scene_node`.
  - **Geometry**: `struct wlr_box geometry` (requested), `struct wlr_box *current_geometry`, `struct wlr_box *current_unmaximized_geometry`.
  - **Decorations**: `struct hikari_border border`, `struct hikari_indicator_frame indicator_frame`, `struct hikari_view_decoration decoration`.
  - **State Flags**: `use_csd`, `child`, `hidden`, `invisible`, `floating`, `public`, `forced`. (Flags managed via macro-generated getters/setters like `hikari_view_is_hidden`).
  - **VTable (Polymorphism)**: Function pointers for protocol-specific implementations: `resize`, `move`, `move_resize` (XWayland only), `activate`, `quit`, `constraints`.
  - **State Machines**: `struct hikari_maximized_state *maximized_state`, `struct hikari_operation pending_operation` (handles async serials during resize).
  - **Subsurfaces**: `struct hikari_view_child` and `struct hikari_view_subsurface`.
- `struct hikari_view_decoration`: Negotiates `wlr_server_decoration` vs `wlr_xdg_decoration`.

**Geometry & Movement (`src/view.c`):**
- `void hikari_view_move(struct hikari_view *view, int x, int y)`:
  - Validates `maximized_state` to prevent moving horizontally maximized windows vertically, etc.
  - Calls `move_view_constrained()` which clamps `X` and `Y` against `view->output->usable_area` via `hikari_geometry_constrain_relative()`.
  - Mutates `view->geometry.x` and `view->geometry.y`.
  - If XWayland (`view->move != NULL`), delegates to the X11 configure request.
  - Triggers `refresh_border_geometry()`, `hikari_view_damage_whole()`, and repositions the indicator via `hikari_indicator_position()`.
- *(Macro-generated actions for `hikari_view_move_bottom_left`, `center`, `top_right`)*:
  - Calculates the absolute target coordinates based on `output->usable_area` and current geometry, then calls `hikari_view_move()`.
- `void hikari_view_resize(struct hikari_view *view, int dx, int dy)`:
  - Invokes the `view->resize` vtable function (e.g., `wlr_xdg_toplevel_set_size`).
  - Sets the `pending_operation` state to wait for the client's `ack_configure` serial.

**Visibility & Z-Ordering (`src/view.c`):**
- `static void move_to_top(struct hikari_view *view)`:
  - Manipulates the linked lists (`sheet_views`, `group_views`, `output_views`) to push the view to the end of the lists (which represents the top in wlroots/hikari traversal).
- `static void place_visibly_above(struct hikari_view *view, struct hikari_workspace *workspace)`:
  - Pushes the view to the front of `hikari_server.visible_views`.
- `static void raise_view(struct hikari_view *view)`:
  - Calls `move_to_top()` and `place_visibly_above()`.
- `void hikari_view_damage_whole(struct hikari_view *view)`:
  - If the view is on an enabled output and not hidden, it calls `hikari_output_add_damage()` using the `view->geometry` box.
- `void hikari_view_damage_surface(struct hikari_view *view, struct wlr_surface *surface, bool whole)`:
  - Calculates damage specific to the client buffer extents rather than the server border extents (CSD vs SSD handling).

**Subsurface Management (`src/view.c`):**
- `void hikari_view_subsurface_init(struct hikari_view_subsurface *view_subsurface, struct hikari_view *parent, struct wlr_subsurface *subsurface)`:
  - Sets up the `destroy` listener for `wlr_subsurface`.
- `void hikari_view_child_init(...)`:
  - Wires up the `commit` and `new_subsurface` listeners for a child surface (used for popups and tooltips).

**Async Operations & Serials (`src/view.c`):**
- `static void commit_pending_operation(struct hikari_view *view, struct hikari_operation *operation)`:
  - Called when a client acknowledges a configure request.
  - Re-evaluates Z-indexing (`raise_view()`), applies the `operation->geometry` via `commit_pending_geometry()`, and re-centers the cursor if the operation flagged it.

### 12.4 `include/hikari/workspace.h` & `src/workspace.c` (Virtual Desktops & Action Dispatch)

**Data Structures:**
- `struct hikari_workspace`: Represents a physical output's logical working state.
  - **Pointers**: `struct hikari_output *output`, `struct hikari_view *focus_view`, `struct hikari_sheet *sheet` (active), `struct hikari_sheet *alternate_sheet`, `struct hikari_layer *focus_layer` (Layer Shell exclusive focus).
  - **Arrays & Lists**: `struct hikari_sheet *sheets` (Heap allocated array of 10), `struct wl_list views`.

**Initialization & Tear Down (`src/workspace.c`):**
- `void hikari_workspace_init(struct hikari_workspace *workspace, struct hikari_output *output)`:
  - Initializes the workspace list heads.
  - Dynamically allocates `HIKARI_NR_OF_SHEETS` (10) instances of `struct hikari_sheet` and initializes them.
  - Defaults `sheet` to index 1, `alternate_sheet` to index 0.
- `void hikari_workspace_fini(struct hikari_workspace *workspace)`:
  - Frees the sheets array.
- `void hikari_workspace_merge(struct hikari_workspace *workspace, struct hikari_workspace *into)`:
  - Evacuates all views (and XWayland unmanaged views) from the source workspace and merges them into the destination workspace (used during output hotplug/disconnect).

**Focus Management (`src/workspace.c`):**
- `void hikari_workspace_focus_view(struct hikari_workspace *workspace, struct hikari_view *view)`:
  - Asserts that the server is in normal mode.
  - Mutates the previous `focus_view`: clears its keyboard/pointer grabs via `wlr_seat_keyboard_clear_focus()` and damages its border to render the inactive color.
  - Elevates the new `view`: calls `hikari_view_activate(view, true)` and wires the wlroots seat focus via `wlr_seat_keyboard_notify_enter()`.
  - Damages the new border to render the active color.

**Sheet & Visibility Mutators (`src/workspace.c`):**
- `static void display_sheet(struct hikari_workspace *workspace, struct hikari_sheet *sheet)`:
  - Core visibility toggle. Loops over `workspace->views`.
  - If a view belongs to the newly active `sheet` (or the pervasive sheet 0), it calls `hikari_view_show()`.
  - Otherwise, it calls `hikari_view_hide()`.
- `void hikari_workspace_switch_to_next_inhabited_sheet(struct hikari_workspace *workspace)`:
  - Queries `hikari_sheet_next_inhabited()` and cascades into `display_sheet()`.
- *(Macro-generated actions for `display_sheet_1` through `9`, `switch_to_next_sheet`, etc.)*
- *(Macro-generated actions for `show_all`, `show_invisible`, `toggle_view_invisible`)*

**Window Geometry Snapping (`src/workspace.c`):**
- `void hikari_workspace_snap_view_up(struct hikari_workspace *workspace)` (and `down`, `left`, `right`):
  - Casts a bounding ray ("lookahead") from the current `focus_view->geometry` in the specified direction.
  - Iterates through all other views in the workspace to find intersecting geometries.
  - Selects the closest edge.
  - Updates the focused view's coordinates to perfectly align next to the target view, adjusting for `hikari_configuration->gap` and `border`.
  - Calls `hikari_view_move_absolute()`.

**Navigation Iterators (`src/workspace.c`):**
- *(Macro-generated actions for `next_view`, `prev_layout_view`, `next_group_view`)*:
  - Safely steps through linked lists (`visible_group_views`, `workspace_views`) while verifying that the retrieved pointers are not the list head sentinel itself via `wl_container_of`.

### 12.5 `include/hikari/sheet.h` & `src/sheet.c` (Virtual Desktop & Tiling Implementations)

**Data Structures:**
- `struct hikari_sheet`: The equivalent of a "tag" or virtual desktop.
  - **State**: `uint8_t nr` (Index 0-9).
  - **Pointers**: `struct hikari_workspace *workspace`, `struct hikari_layout *layout` (Current tiling algorithm context).
  - **Lists**: `struct wl_list views` (Windows assigned to this sheet).

**Initialization & Navigation (`src/sheet.c`):**
- `void hikari_sheet_init(struct hikari_sheet *sheet, int nr, struct hikari_workspace *workspace)`:
  - Sets the index, initializes the `views` list head, and links the workspace.
- `struct hikari_sheet *hikari_sheet_next_inhabited(struct hikari_sheet *sheet)` (and `prev_inhabited`):
  - Cycles through the `workspace->sheets` array (skipping empty sheets via `wl_list_empty(&name->views)` loop) to find the next active sheet.

**Tiling Layout Implementations (`src/sheet.c`):**
- `static struct hikari_view *grid_layout(...)`:
  - Dynamically calculates rows and columns based on the total number of tileable views.
  - Subtracts `hikari_configuration->gap` and `border` to compute optimal cell dimensions.
  - Mutates each view's target geometry via `hikari_view_tile()`.
- `SPLIT_LAYOUT(queue, x, y, width, height)` & `SPLIT_LAYOUT(stack, y, x, height, width)`:
  - Macro-generated layout logic for arranging the master view alongside secondary stacked/queued views, recalculating proportional splits based on `nr_of_views`.
- `static struct hikari_view *full_layout(...)`, `single_layout(...)`, `empty_layout(...)`:
  - Concrete fallback/fullscreen tiling logic.

**Layout Application (`src/sheet.c`):**
- `void hikari_sheet_apply_split(struct hikari_sheet *sheet, struct hikari_split *split)`:
  - Checks if any views in the existing layout are "dirty" (currently awaiting resize ACKs). If so, drops the request to prevent layout thrashing.
  - Allocates or reuses `struct hikari_layout`.
  - Applies the mathematical subdivision (`hikari_split_apply()`) to the output's `usable_area`.
  - Re-raises any floating views (`raise_floating()`) so they remain on top of the newly tiled windows.

**Visibility Filters (`src/sheet.c`):**
- `SHOW_VIEWS(cond)` macro: Iterates over all `views` in the sheet, evaluating a condition, and calling `hikari_view_show(view)`.
- `void hikari_sheet_show(struct hikari_sheet *sheet)`: Shows views where `!hikari_view_is_invisible()`.
- `void hikari_sheet_show_invisible(struct hikari_sheet *sheet)`: Shows explicitly hidden/invisible views.
- `void hikari_sheet_show_group(struct hikari_sheet *sheet, struct hikari_group *group)`: Shows views matching the group pointer.

### 12.6 `include/hikari/xdg_view.h` & `src/xdg_view.c` (Native Wayland Surfaces)

**Data Structures:**
- `struct hikari_xdg_view`: Concrete implementation of `struct hikari_view`.
  - **Inheritance**: Embeds `struct hikari_view view` as its first member (enabling safe casting between `hikari_view` and `hikari_xdg_view`).
  - **WLRoots Proxies**: `struct wlr_xdg_surface *surface`, `struct wlr_xdg_toplevel *xdg_toplevel`.
  - **Scene Graph**: `struct wlr_scene_tree *scene_tree` (hikari-owned parent) and `struct wlr_scene_tree *surface_tree` (wlroots-owned, parented beneath it). The split is deliberate — see the ownership note below.
  - **Listeners**: `map`, `unmap`, `destroy`, `commit`, `new_popup`, `request_fullscreen`, `set_title`.
- `struct hikari_xdg_popup`: Represents menus and tooltips attached to a toplevel.

**Initialization & wlroots 0.20 Lifecycle (`src/xdg_view.c`):**
- `void hikari_xdg_view_init(struct hikari_xdg_view *xdg_view, struct wlr_xdg_surface *xdg_surface, struct hikari_workspace *workspace)`:
  - Invokes the superclass initializer: `hikari_view_init()`.
  - **Scene Tree Ownership (critical):** Creates a *hikari-owned* parent tree with `wlr_scene_tree_create()`, then attaches the wlroots-managed surface tree beneath it with `wlr_scene_xdg_surface_create(scene_tree, xdg_surface)`. This split is mandatory, not stylistic: `wlr_scene_xdg_surface_create` installs its own listener on `xdg_surface.events.destroy` (see `wlroots-0.20.0/types/scene/xdg_shell.c`) that destroys the tree it returned **and every child node**. Parenting hikari's border and indicator rects into that tree would hand their lifetime to wlroots and leave hikari with dangling pointers. Border and indicator rects therefore parent to `scene_tree`, never to `surface_tree`. `hikari_xwayland_view_init` uses the same ownership model.
  - `xdg_surface->data` stores the **hikari-owned** `scene_tree`, not `surface_tree`, because `server_decoration_handler` (`src/server.c`) resolves a decoration back to its view via `xdg_surface->data->node.data` and only `scene_tree` carries that back-reference.
  - **Critical wlroots 0.20 Change**: Wires up the `commit` listener *immediately* upon object creation, before the client maps the window.
- `static void commit_handler(struct wl_listener *listener, void *data)`:
  - **`initial_commit` Handling**: If `surface->initial_commit` is true, it replies with `wlr_xdg_toplevel_set_size(xdg_view->xdg_toplevel, 0, 0)` immediately. This signals wlroots to set `surface->initialized = true`, allowing the client to safely request a map.
  - Normal Commits: Checks if `hikari_view_was_updated()` (comparing serials) and applies the `pending_operation` bounds via `wlr_xdg_toplevel_set_tiled` constraints. Modifies `geometry` based on the committed buffer size.
- `static void map(struct hikari_view *view, bool focus)`:
  - Wires up the interactive listeners for the mapped surface (`set_title`, `request_fullscreen`, `new_popup`).

**Popup Management (`src/xdg_view.c`):**
- `static void xdg_popup_create(struct wlr_xdg_popup *wlr_popup, struct hikari_view *parent)`:
  - Invoked when a client requests a popup menu.
  - Wires up popup-specific `commit`, `map`, `unmap`, and `destroy` listeners.
  - Calls `wlr_xdg_popup_unconstrain_from_box()` via `popup_unconstrain()` to ensure the menu renders within the visible bounds of the physical output.

**Polymorphic VTable Implementations (`src/xdg_view.c`):**
- `static uint32_t resize(struct hikari_view *view, int width, int height)`:
  - Casts `view` to `hikari_xdg_view`.
  - Executes `wlr_xdg_toplevel_set_size(xdg_view->xdg_toplevel, width, height)`. Returns the serial for tracking.
- `static void quit(struct hikari_view *view)`:
  - Executes `wlr_xdg_toplevel_send_close(xdg_view->xdg_toplevel)`.
- `static void constraints(struct hikari_view *view, int *min_width, int *min_height, int *max_width, int *max_height)`:
  - Maps wlroots state bounds (`state->min_width`, etc.) to the out-pointers, capping max dimensions at the output's bounding box.

### 12.7 `include/hikari/xwayland_view.h` & `src/xwayland_view.c` (Legacy X11 Support)

**Data Structures:**
- `struct hikari_xwayland_view`: Implementation of `struct hikari_view` for X11 clients.
  - **Inheritance**: Embeds `struct hikari_view view`.
  - **WLRoots Proxies**: `struct wlr_xwayland_surface *surface`.
  - **Scene Graph**: `struct wlr_scene_tree *scene_tree`.
  - **Listeners**: `associate`, `dissociate`, `map`, `unmap`, `destroy`, `request_configure`, `set_title`, `commit`.
- `struct hikari_xwayland_unmanaged_view`: Lightweight wrapper for unmanaged override-redirect windows (tooltips, dropdowns). Implements `struct hikari_node` directly instead of inheriting from `hikari_view` because they lack borders and tiling semantics.

**Initialization & wlroots 0.20 Lifecycle (`src/xwayland_view.c`):**
- `void hikari_xwayland_view_init(...)`:
  - Registers the core struct, initializes the `wlr_scene_tree`, and attaches `associate` and `dissociate` listeners.
  - **Critical wlroots 0.20 Change**: The underlying `wlr_surface` is NULL when the `wlr_xwayland_surface` is created. Hikari defers attaching `map` and `unmap` listeners until the `associate_handler` fires (which guarantees `wlr_surface != NULL`).
- `static void associate_handler(...)` / `dissociate_handler(...)`:
  - Attach and detach the `map`/`unmap` signals to the dynamically associated `wlr_surface`.
- `static void map_handler(...)`:
  - If the view is unmapped (`hikari_view_is_unmanaged`), calls `first_map()`.
- `static void first_map(...)`:
  - Parses the `surface->class` (WM_CLASS) to resolve configuration rules via `hikari_configuration_resolve_view_config()`.

**Coordinate Translations (`src/xwayland_view.c` & `src/xwayland_unmanaged_view.c`):**
- `static void commit_handler(...)` (in both files):
  - Wayland coordinates are inherently relative. X11 coordinates are absolute across a global screen bounding box.
  - When XWayland clients commit, Hikari translates their absolute `surface->x` coordinates into output-relative coordinates: `surface_x_in_hikari = surface->x - output->geometry.x`.
  - Updates the native bounding boxes and triggers `hikari_view_damage_whole()`.
- `static void request_configure_handler(...)`:
  - Intercepts X11 configure requests (`XMoveWindow`, `XResizeWindow`), clamps them via `hikari_geometry_constrain_absolute()`, and replies with `wlr_xwayland_surface_configure()`.

**Polymorphic VTable Implementations (`src/xwayland_view.c`):**
- `static void move(struct hikari_view *view, int x, int y)` / `move_resize(...)`:
  - XWayland requires the compositor to instruct the X server to move the window natively via `wlr_xwayland_surface_configure()`.
- `static void constraints(struct hikari_view *view, int *min_width, int *min_height, int *max_width, int *max_height)`:
  - **Critical wlroots 0.20 Change**: Uses the public `xcb_size_hints_t *size_hints = surface->size_hints` struct to parse ICCCM `WM_NORMAL_HINTS` bounds.

### 12.8 `include/hikari/layer_shell.h` & `src/layer_shell.c` (UI Shell Components)

**Data Structures:**
- `struct hikari_layer`: Represents desktop UI elements (panels, docks, wallpapers, notifications).
  - **WLRoots Proxies**: `struct wlr_layer_surface_v1 *surface`.
  - **Scene Graph**: `struct wlr_scene_layer_surface_v1 *scene_layer_surface`.
  - **Listeners**: `map`, `unmap`, `commit`, `destroy`, `new_popup`.
- `struct hikari_layer_popup`: Represents menus/tooltips spawned by a layer surface. Contains a polymorphic `parent` pointer (`HIKARI_LAYER_NODE_TYPE_LAYER` or `HIKARI_LAYER_NODE_TYPE_POPUP`) to handle recursive nesting.

**Initialization & Scene Graph (`src/layer_shell.c`):**
- `void hikari_layer_init(struct hikari_layer *layer, struct wlr_layer_surface_v1 *wlr_layer_surface)`:
  - Identifies the target output and links it.
  - **Scene Graph Abstraction**: Creates the scene node via `wlr_scene_layer_surface_v1_create()`. This wlroots helper automatically creates subsurface and popup tree nodes managed by the layer surface.
  - Registers `commit`, `destroy`, and `map` listeners.
- `static void map(struct hikari_layer *layer)` / `unmap(struct hikari_layer *layer)`:
  - `wlr_scene_node_set_enabled(&layer->scene_layer_surface->tree->node, true/false)` is called to toggle visibility without destroying the node, ensuring no stale buffers remain on screen.
  - Wires `new_popup` and `unmap` during map; drops them during unmap.

**Geometry & Exclusive Zones (`src/layer_shell.c`):**
- `static void calculate_exclusive(struct hikari_output *output)`:
  - Re-evaluates `output->usable_area` based on the native resolution.
  - Mutates the `usable_area` bounding box by subtracting `exclusive_zone` margins reserved by top/bottom layers (e.g., status bars preventing windows from drawing underneath them).
- `static void calculate_geometry(struct hikari_layer *layer)`:
  - Extracts `state->anchor` (Left, Right, Top, Bottom) and `state->margin`.
  - Applies stretching semantics: if a layer is anchored left AND right, it stretches to `bounds.width`. If anchored Top AND Bottom, it stretches to `bounds.height`.
  - Updates `layer->geometry` and instructs wlroots via `wlr_layer_surface_v1_configure()`.

**Nested Damage Tracking (`src/layer_shell.c`):**
- `static void damage_popup(struct hikari_layer_popup *layer_popup, bool whole)`:
  - Iterates UP the `parent` polymorphic chain (`get_layer()`) until it hits the root `hikari_layer`.
  - Accumulates `current->geometry.x` and `y` offsets at every step.
  - Emits damage on the physical output matching the absolute bounding box.
  - **wlroots 0.20 Change**: Reads popup relative offsets directly from the state block (`popup->current.geometry`) as `wlr_xdg_popup_get_geometry()` no longer exists.

### 12.9 `include/hikari/group.h` & `src/group.c` (Window Grouping)

**Data Structures:**
- `struct hikari_group`: Logical collection of windows, used for applying collective actions.
  - **State**: `char *name` (The string identifier).
  - **Lists**: `struct wl_list views` (All views in this group), `struct wl_list visible_views` (Currently mapped/unhidden views).
  - **Global Lists**: `server_groups` and `visible_server_groups` attach this group to the global `hikari_server`.

**Logic (`src/group.c`):**
- `void hikari_group_show(struct hikari_group *group)` / `hide(...)`:
  - Iterates over the `views` list. If a view is hidden, calls `hikari_view_show()`. For hiding, it iterates over `visible_views` and calls `hikari_view_hide()`.
- `void hikari_group_raise(struct hikari_group *group, struct hikari_view *top)` / `lower(...)`:
  - Reorders the Z-index of all views in a group by cascading `hikari_view_raise()` on the target `top` view, followed by all other views in `visible_views`, and finishing with `top` again so it ends up definitively on top.

### 12.10 `include/hikari/mark.h` & `src/mark.c` (Register Marks)

**Data Structures:**
- `struct hikari_mark`: A single-letter register (like vim marks `a`-`z`) pointing to a specific view.
  - **State**: `char *name` (String representation of the character), `int nr` (Array index 0-25).
  - **Pointers**: `struct hikari_view *view` (The view this mark currently points to, or `NULL`).

**Logic (`src/mark.c`):**
- Static array of 26 registers initialized in `hikari_marks_init()`.
- `void hikari_mark_set(struct hikari_mark *mark, struct hikari_view *view)`:
  - If the view is already marked, clears the old mark (`hikari_mark_clear(view->mark)`).
  - If the mark already points to another view, clears the old view (`hikari_mark_clear(mark)`).
  - Establishes the bidirectional link: `mark->view = view` and `view->mark = mark`.

### 12.11 `include/hikari/keyboard.h` & `src/keyboard.c` (Keyboard Input & Bindings)

**Data Structures:**
- `struct hikari_keyboard`: Wraps a physical `wlr_input_device` of type keyboard.
  - **WLRoots Proxies**: `struct wlr_keyboard *wlr_keyboard`.
  - **State**: `struct xkb_keymap *keymap`.
  - **Listeners**: `modifiers`, `key`, `destroy`.
  - **Bindings**: `struct hikari_binding_group bindings[256]` (Maps keysyms/keycodes + modifier bitmasks to action structs).

**Input Dispatch (`src/keyboard.c`):**
- `static void key_handler(struct wl_listener *listener, void *data)`:
  - Intercepts raw key events and delegates directly to the current mode's polymorphic handler: `hikari_server.mode->key_handler(keyboard, event)`.
- `static void modifiers_handler(...)`:
  - Tracks modifier state (Logo, Alt, Ctrl, Shift) and delegates to `hikari_server.mode->modifiers_handler(keyboard)`.
- `static void configure_bindings(...)`:
  - Converts configured XKB keysyms to hardware keycodes using `xkb_keymap_key_for_each()` and `xkb_state_key_get_one_sym()`. This translation avoids performance penalties on every keypress during the hot rendering loop.

### 12.12 `include/hikari/pointer.h` & `src/pointer.c` (Pointer Devices & libinput)

**Data Structures:**
- `struct hikari_pointer`: Wraps a physical `wlr_input_device` of type pointer (mice, touchpads, trackpoints).
  - **WLRoots Proxy**: `struct wlr_input_device *device`.
  - **Listeners**: `destroy`.

**Hardware Configuration (`src/pointer.c`):**
- `void hikari_pointer_configure(...)`:
  - If the device is managed by `libinput` (via `wlr_input_device_is_libinput`), extracts the raw `libinput_device` handle.
  - Applies configurations mapped from the user's `hikari.conf` (e.g., `libinput_device_config_accel_set_speed`, `libinput_device_config_dwt_set_enabled` (disable while typing), `libinput_device_config_scroll_set_natural_scroll_enabled`, tap-to-click).

### 12.13 `include/hikari/touch.h` & `src/touch.c` (Touchscreen Devices)

**Data Structures:**
- `struct hikari_touch`: Wraps a physical `wlr_input_device` of type `WLR_INPUT_DEVICE_TOUCH`.
  - **WLRoots Proxy**: `struct wlr_input_device *device`.
  - **Listeners**: `destroy`.
  - No per-device hardware-config surface (touchscreens have no accel/tap-to-click knobs); the only device-attach-time decision is output confinement, made in `add_touch()` (`src/server.c`) via `wlr_touch_from_input_device(device)->output_name` -> `wlr_cursor_map_input_to_output()`.

### 12.14 `include/hikari/gesture_config.h` & `src/gesture_config.c` (Gesture Bindings)

**Data Structures:**
- `struct hikari_gesture_binding_config`: One parsed `inputs { gestures { "<key>" = <action> } }` entry.
  - `enum hikari_gesture_type type`: `SWIPE` / `PINCH` / `HOLD`.
  - `enum hikari_gesture_direction direction`: `UP`/`DOWN`/`LEFT`/`RIGHT` (swipe), `IN`/`OUT` (pinch), `NONE` (hold).
  - `uint32_t fingers`, `struct hikari_action action` — resolved through the same `hikari_action_parse()` every other binding type uses.
  - Stored on `hikari_configuration->gesture_binding_configs`; looked up directly from `src/cursor.c` at gesture-`_end` time (no separate compiled/copied table — gesture bindings are few enough that a linear `wl_list` scan is unnecessary to optimize, unlike the 256-bucket modifier-mask arrays keyboard/mouse bindings use).
- `bool hikari_gesture_binding_config_key_parse(...)`: Parses a binding key string (`"swipe-left-3"`, `"pinch-in-4"`, `"hold-3"`) into the three typed fields above.

### 12.15 `include/hikari/cursor.h` & `src/cursor.c` (Virtual Cursor & Axis Dispatch)

**Data Structures:**
- `struct hikari_cursor`: The unified virtual cursor aggregating all physical pointers.
  - **WLRoots Proxy**: `struct wlr_cursor *wlr_cursor`, `struct wlr_xcursor_manager *cursor_mgr`.
  - **Listeners**: `motion`, `motion_absolute`, `button`, `axis`, `frame`, `request_set_cursor`.
  - **Bindings**: `struct hikari_binding_group bindings[256]`.

**Input Dispatch (`src/cursor.c`):**
- `static void motion_handler(...)` / `motion_absolute_handler(...)`:
  - Moves the virtual `wlr_cursor` bounding box via `wlr_cursor_move` or `wlr_cursor_warp_absolute`.
  - Dispatches to the mode handler via `hikari_server.mode->cursor_move()`.
- `static void button_handler(...)`:
  - Intercepts clicks and routes to `hikari_server.mode->button_handler(cursor, event)`.
- `static void axis_handler(...)`:
  - Routes scroll wheels natively: `wlr_seat_pointer_notify_axis(hikari_server.seat, ...)`.
- `static void request_set_cursor_handler(...)`:
  - Allows Wayland clients to set custom cursor images (e.g., resizing arrows, text I-beams) using `wlr_cursor_set_surface()`. Guards against inactive clients hijacking the cursor image.

### 12.16 `include/hikari/indicator.h` & `src/indicator.c` (Status HUD & Focus Frames)

**Data Structures:**
- `struct hikari_indicator`: The central HUD showing window metadata during mode switching (super-key held).
  - **Text Bars**: `title`, `sheet`, `group`, `mark`.
- `struct hikari_indicator_frame`: The coloured rectangular border wrapping the focused view.

**Rendering (`src/indicator.c`):**
- `void hikari_indicator_update(struct hikari_indicator *indicator, struct hikari_view *view)`:
  - Formats strings for the target view's group, sheet location (e.g., `[1] @ 2 - DP-1`), and title.
- `void hikari_indicator_position(...)`:
  - Synchronises the absolute coordinates of the 4 text bars with the `geometry` of the currently focused view's border so they float underneath/above it.
  - Calls `hikari_indicator_frame_show(&view->indicator_frame)` to activate the bounding border color.

### 12.17 `include/hikari/mode.h` & `src/normal_mode.c` (Input Routing Polymorphism)

**Architecture (`include/hikari/mode.h`):**
- `struct hikari_mode`: A VTable defining state-dependent input routing.
  - **Pointers**: `key_handler`, `modifiers_handler`, `button_handler`, `cursor_move`, `cancel`.
  - **Usage**: The active mode is a global singleton pointer `hikari_server.mode`. The hardware abstractions (`keyboard.c`, `cursor.c`) blindly execute `hikari_server.mode->key_handler(...)` rather than using `switch(state)` blocks, guaranteeing strict state separation.

**Normal Mode Implementation (`src/normal_mode.c`):**
- Implements the standard desktop state (`hikari_normal_mode`).
- `static void modifiers_handler(struct hikari_keyboard *keyboard)`:
  - Intercepts modifier key releases (`mod_released`). If the server is in a cycling operation (`hikari_server_is_cycling()`), releasing the modifier finalizes the operation (e.g., Alt-Tab release), triggering `hikari_view_raise()` and cursor centering on the newly selected window.
- `static void cursor_down_move(uint32_t time)` / `cursor_up_move(uint32_t time)`:
  - When the cursor is pressed down on a client surface, mouse motion is delegated to `wlr_seat_pointer_notify_motion()` so the client can drag content (e.g., selecting text).
  - When up, `wlr_seat_pointer_notify_motion()` routes the absolute coordinates across the compositor surface.

### 12.18 `include/hikari/action.h` & `src/action.c` (Command & Binding Execution)

**Data Structures:**
- `struct hikari_event_action`: A typed function pointer wrapper (`void (*action)(void *)`) mapping a parsed configuration string to a concrete C function.
- `struct hikari_action`: Bundles `begin` (key press) and `end` (key release) actions.

**Action Parsing (`src/action.c`):**
- `static bool parse_binding(..., const ucl_object_t *obj, void (**action)(void *), void **arg)`:
  - Translates string values from `hikari.conf` (e.g., `"view-move-up"`, `"workspace-switch-to-sheet-1"`, `"view-toggle-floating"`) to their corresponding `server.c` implementations (`hikari_server_move_view_up`, etc.).
  - Relies heavily on C macros (`PARSE_MOVE_BINDING`, `PARSE_WORKSPACE_BINDING`) to generate the massive string-matching sequence efficiently.

### 12.19 `include/hikari/configuration.h` & `src/configuration.c` (State Parsing)

**Data Structures:**
- `struct hikari_configuration`: The central, singleton store for user settings.
  - **Arrays**: `float clear[4]`, `float border_active[4]`, etc. for colours.
  - **Lists**: `view_configs` (app-specific rules), `output_configs`, `pointer_configs`, `keyboard_configs`, `layout_configs`.

**Parsing Engine (`src/configuration.c`):**
- Parses the configuration file using the `libucl` library.
- `static bool parse_layout_func(...)`: Maps string identifiers (`"queue"`, `"grid"`) to layout pointers (`hikari_sheet_queue_layout`).
- `struct hikari_view_config *hikari_configuration_resolve_view_config(..., const char *app_id)`:
  - Resolves X11 `WM_CLASS` or Wayland `app_id` against the configured application regex/rules (e.g., forcing floating geometry for `pavucontrol`).
- Bootstraps the keyboard and mouse bindings, translating them via `action.c`, so `normal_mode.c` can traverse the `struct hikari_binding_group bindings[256]` O(1) array at runtime during key events.

### 12.20 `include/hikari/border.h` & `src/border.c` (Window Decoration Rendering)

**Data Structures:**
- `struct hikari_border`: Represents the coloured bounding box around a window.
  - **Scene Nodes**: `struct wlr_scene_rect *top, *bottom, *left, *right`. Uses 4 distinct rectangle nodes from the wlroots scene graph to frame the view.
  - **State**: `enum hikari_border_state state` (`HIKARI_BORDER_NONE`, `HIKARI_BORDER_ACTIVE`, `HIKARI_BORDER_INACTIVE`).

**Rendering (`src/border.c`):**
- `void hikari_border_init(struct hikari_border *border, struct wlr_scene_tree *parent)`:
  - Attaches the 4 scene rects to the view's scene tree (`wlr_scene_rect_create()`), sets their initial color to transparent, and pushes them to the bottom of the tree so they render underneath the view buffer (`wlr_scene_node_lower_to_bottom()`).
- `void hikari_border_refresh_geometry(struct hikari_border *border, struct wlr_box *geometry)`:
  - Re-evaluates the view's requested geometry. If the border is active/inactive, it inflates the bounding box by `hikari_configuration->border`.
  - Mutates the scene graph position and size of the 4 border rects to precisely hug the new bounding box.
  - **Coordinate space:** the rects are positioned **parent-relative**. `wlr_scene_node_set_position` is relative to the parent node, and the view's `scene_tree` has already been positioned at the view's layout-absolute origin by `hikari_view_refresh_geometry`. Passing absolute coordinates here applies the view origin twice. `border->geometry` itself remains **absolute**, because hit-testing and damage tracking consume it in layout coordinates. The same rule applies to `hikari_indicator_frame_refresh_geometry`.
  - Toggles the rect color between `hikari_configuration->border_active` and `border_inactive`.

### 12.21 Tiling Engine (`src/layout.c`, `src/split.c`, `src/tile.c`)

**`src/layout.c` (Tiling Context):**
- `hikari_layout_init()` / `hikari_layout_fini()`: Initializes the tiling state for a `hikari_sheet`, wrapping a mathematical `hikari_split` tree.
- `hikari_layout_restack_append()` / `hikari_layout_restack_prepend()`: Reorders the Z-index of all views in a layout (calling `hikari_view_raise()`) and triggers `hikari_sheet_apply_split()` to enforce the geometry.
- `hikari_layout_reset()`: Iterates through all tiled views and strips their tiled geometry constraints via `hikari_view_reset_geometry()`.

**`src/split.c` (Geometry Subdivision Math):**
- `hikari_split_apply()`: Recursively walks the binary split tree (`HIKARI_SPLIT_TYPE_VERTICAL`, `HIKARI_SPLIT_TYPE_HORIZONTAL`, `HIKARI_SPLIT_TYPE_CONTAINER`).
- `split_scale_width()` / `split_scale_height()`: Dynamically calculates the fractional percentage of the screen a window should occupy based on `HIKARI_SPLIT_SCALE_TYPE_FIXED` or `DYNAMIC` limits.
- Calls `hikari_geometry_split_vertical/horizontal()` to divide a `wlr_box` into two smaller `wlr_box` structures, factoring in `hikari_configuration->gap`.

**`src/tile.c` (View Enforcer):**
- `hikari_tile_init()`: Wraps a `hikari_view`.
- `hikari_tile_apply()`: Given a computed `wlr_box` from `split.c`, it forces the view to conform by mutating its geometry and calling `hikari_view_damage_whole()`.

### 12.22 Interactive Geometry Mutators (`src/move_mode.c`, `src/resize_mode.c`)

**`src/move_mode.c`:**
- `struct hikari_move_mode`: Implements the `hikari_mode` VTable for interactive dragging.
- `cursor_move()`: Calculates the delta (`delta_x = current_x - start_x`). Calls `hikari_view_move()` iteratively as the user drags the mouse, snapping to the `usable_area` boundaries.
- `button_handler()` / `cancel()`: Releasing the mouse button or pressing Escape reverts `hikari_server.mode` to `normal_mode`.

**`src/resize_mode.c`:**
- `struct hikari_resize_mode`: Implements the `hikari_mode` VTable for interactive resizing.
- `cursor_move()`: Calculates the mouse delta. Depending on which quadrant/edge of the window was grabbed, mutates the `wlr_box` dimensions. Emits `hikari_view_resize()` (which triggers a `wlr_xdg_toplevel_set_size` configure request) and waits for the client serial.

### 12.23 Modal Prompt State Machines (`src/*_assign_mode.c`)

These files implement interactive keystroke buffers where the user presses a modifier, types a sequence (e.g. `g` then `t` then `e` `r` `m` `Enter` to assign a group), and completes an action.

- **`src/sheet_assign_mode.c`**: Prompts for a number `0-9`. Calls `hikari_view_pin_to_sheet()`.
- **`src/group_assign_mode.c`**: Prompts for a string. Uses `src/input_buffer.c` to accumulate characters. On `Enter`, calls `hikari_group_assign()`.
- **`src/mark_assign_mode.c`**: Prompts for a single char `a-z`. Maps the active view to the register in `src/mark.c`.
- **`src/mark_select_mode.c`**: Prompts for `a-z`. Jumps focus to the registered view.
- **`src/layout_select_mode.c`**: Prompts for a layout string identifier (`g` for grid, `f` for full) and triggers `hikari_layout_apply()`.

### 12.24 Process Execution (`src/command.c`, `src/exec.c`)

**`src/command.c` (Subprocess Spawning):**
- `hikari_command_execute()`: Safely executes an external shell command (e.g. `alacritty` or `waybar`).
- **Architecture**: Uses the double-fork technique.
  1. `fork()` (Parent 1 waits via `waitpid()`).
  2. `fork()` (Parent 2 exits immediately).
  3. `setsid()` (Child creates a new session group).
  4. `execvp()` (Child replaces itself with the target binary).
  This strictly ensures no zombie processes accumulate in the compositor.

**`src/exec.c` (Autostart Config):**
- `hikari_exec_init()`: Parses `autostart` rules from `hikari.conf`.
- `hikari_exec_apply()`: Iterates over the `hikari_configuration->execs` array during compositor startup and calls `hikari_command_execute()` for each.

### 12.25 Screen Locker (`src/lock_mode.c`, `src/lock_indicator.c`)

**`src/lock_mode.c` (Security Boundary):**
- `struct hikari_lock_mode`: Implements the `hikari_mode` VTable. Suppresses all normal keybindings.
- `key_handler()`: Accumulates keystrokes into a dynamically allocated buffer. Uses `mlock()` and `munlock()` to prevent the password buffer from ever being written to disk swap.
- On `Enter`, non-blockingly pipes the buffer to the setuid `hikari-unlocker` PAM helper. Uses `wl_event_loop_add_fd()` to monitor the pipe for authentication success.

**`src/lock_indicator.c` (Locker UI):**
- `hikari_lock_indicator_init()`: Creates a `wlr_scene_buffer` overlay.
- `hikari_lock_indicator_damage()`: Re-draws the visual state (`HIKARI_LOCK_INDICATOR_TYPING`, `VERIFYING`, `DENIED`) as concentric circles using CPU-bound `cairo` arcs. Raises itself to the top of the lock layer, which — since Phase 73 — is scoped and no longer competes with the bar.

### 12.25a The Lock Screen (Phases 73–77)

*Added 2026-08-22. The lock screen became a compositor-drawn surface in its own right; this records how the pieces fit, since it now spans six files.*

**The boundary is structural, not a flag.** `override_visibility()` disables the `bottom`/`views`/`top`/`overlay` scene layers (§1, Rendering) and wlr_scene disables every child of a disabled node, so views, the top bar, indicator overlays and every layer-shell surface go dark together. Nothing a client does afterwards can put a window back on screen — a newly mapped window parents into the disabled view layer and is invisible for the same reason. `background` is deliberately left enabled so the wallpaper shows through where no backdrop was captured.

Views marked `public` are reparented onto `layers.lock` **and explicitly enabled**, which is not redundant with the hidden-flag flip: a public view parked on another sheet has a *disabled* scene node the flag never touched. Before Phase 73 that was exactly why a `public` clock never appeared.

**Ordering at lock time is load-bearing** (`hikari_lock_mode_enter`):
1. capture every output — *before* anything is hidden, or it photographs an empty screen;
2. blur each capture;
3. attach backdrops to `layers.lock`, lowered to the bottom;
4. `override_visibility()` — disable the desktop layers;
5. reparent public views in;
6. create the clock.

All six run before returning to the event loop, so no frame is ever committed mid-transition.

**`src/screen_capture.c`** — renders an output off-screen. wlroots has no compositor-facing screenshot call (screencopy serves clients), so this supplies its own swapchain to `wlr_scene_output_build_state()` and never commits the resulting state. Two hard-won constraints:
- The format **must** be built with `wlr_drm_format_set_add()`, not by filling in a `struct wlr_drm_format`. Hand-construction violated `format->len > 0` (`render/allocator/gbm.c:66`) and then `src->len <= src->capacity` (`render/drm_format_set.c:144`), aborting the compositor twice. `DRM_FORMAT_MOD_INVALID` in a one-entry list is how implicit modifiers are requested; an empty list is simply invalid.
- The swapchain is sized by `wlr_output_transformed_resolution()`, because rotation is baked into the rendered buffer rather than applied at scanout.
- `wlr_output_update_needs_frame()` is called first: `build_state()` tracks damage, and an idle desktop has none, so without it the capture works only while something is animating.
- Readback uses `wlr_texture_read_pixels()` (glReadPixels-backed), which never needs a CPU-mappable buffer — see §13 FB-2. Alpha is forced opaque afterwards, since XRGB captures carry none and a 0 would make the backdrop invisible.

**`src/blur.c`** — three-pass separable box blur with a running sum, so cost is independent of radius. Edges are clamped, not wrapped (bleeds one screen edge into the other) or zero-padded (vignettes). Runs once per lock, not per frame.

**`src/lock_clock.c`** — cairo/Pango per output into the shared `hikari_buffer_create_argb8888()`. Ticks on the **minute boundary** rather than every 60 s, because a fixed interval drifts against the wall clock. Drawn with a soft shadow: it sits over a photograph of the user's own desktop, whose brightness is unknown. Vertical placement uses `mm_to_logical_pixels()` so offsets are physical distances rather than pixel counts that shrink with density.

**`src/lock_config.c`** — the `ui { lock { … } }` block, plus `hikari_lock_config_blank_timeout()`, which reads `hw.acpi.acline` **at every timer arm** so unplugging mains mid-lock takes effect on the next keystroke. A missing sysctl (desktop, VM) falls through to the AC value deliberately.

### 12.26 Server-Side Decorations (`src/decoration.c`)

**`src/decoration.c`:**
- `new_decoration_handler()`: Responds to `wlr_xdg_decoration_manager_v1`. If a client requests decorations, Hikari forces `WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE` to ensure the compositor (via `border.c`) controls the window framing.
- Wires up `destroy` listeners to clean up the `hikari_view_decoration` struct when the client disconnects.

### 12.27 Drawing Utilities (`include/hikari/color.h`, `src/font.c`)

**`include/hikari/color.h`:**
- `hikari_color_convert()`: Parses hex color strings (e.g., `#FF0000FF`) into 4-element `float[4]` arrays normalized between 0.0 and 1.0 for use by `cairo` and `wlr_scene_rect`.

**`src/font.c`:**
- `hikari_font_init()`: Initializes a Pango context.
- `hikari_font_get_text_extents()`: Renders text off-screen to calculate its exact pixel width and height, used to correctly size the floating indicator bars (`indicator.c`).

## 13. FreeBSD / ZFS Native-Compatibility Register (opened 2026-08-22, Phase 70)

*Standing register. Target platform is **FreeBSD 15.1-RELEASE on a ZFS root** (`zroot`), Mesa 26.1.6, libdrm 2.4.134, wlroots 0.20.2, clang 19.1.7. This section exists so platform constraints stop being rediscovered — Phases 19, 33 and 53 each burned a cycle re-deriving facts recorded here. Every row states the constraint, today's handling, and what a genuinely native fix requires. Rows are only removed when the constraint itself disappears, never when a symptom is worked around.*

**Design principle governing this register (Phase 70, D2/D3).** Platform behaviour is to be **probed at runtime via public API and logged**, never assumed in a hardcoded platform branch. `struct wlr_renderer.render_buffer_caps` (`wlr/render/wlr_renderer.h:31`, a bitmask of `WLR_BUFFER_CAP_DATA_PTR | DMABUF | SHM`) is the sanctioned probe. `hikari_platform_probe()` (planned, W1) emits one structured `wlr_log(WLR_INFO)` block at startup covering renderer name, buffer caps, DRM device(s) opened, multi-GPU status, `XDG_RUNTIME_DIR` filesystem type and `posix_fallocate` support, and dmabuf negotiation result.

| # | Constraint | Status | Today | Native fix requires |
|---|---|---|---|---|
| **FB-1** | ZFS returns `EOPNOTSUPP`/`EINVAL` from `posix_fallocate()` — copy-on-write cannot give POSIX pre-allocation guarantees (FreeBSD r325320, 2017) | **Not a hikari defect.** wlroots uses `shm_open()` — anonymous POSIX SHM — which bypasses ZFS entirely (verified, DECISIONS_LOG line ~1798) | Client-side only. Mitigated by advertising `zwp_linux_dmabuf_v1` (`server.c:1510`) so GPU clients skip shm, plus the ZFS warning in `start-hikari.sh:70-81` | Upstream: toolkits preferring `SHM_ANON` over files in `XDG_RUNTIME_DIR`. Admin: tmpfs `/tmp` via `zfs set canmount=noauto zroot/tmp` (ZFS automount otherwise mounts *over* the fstab tmpfs) |
| **FB-2** | A compositor cannot CPU-map allocator-provided buffers to write Cairo pixels into them | **RESOLVED Phase 72** (corrected Phase 70 — never a FreeBSD workaround). wlroots 0.20.2 exposes exactly one allocator entry point (`wlr_allocator_autocreate`) and **no public shm/CPU allocator**, so this is true on every platform and the custom `wlr_buffer_impl` is idiomatic | **One** implementation, `src/buffer.c`. The two byte-identical copies (`output.c`, `server.c`) are gone, and `hikari_platform.render_buffer_caps` now answers the capability question by probing instead of assuming | Nothing outstanding here. Genuine upstream ask, unchanged: a public `wlr_shm_allocator_create()`. **Phase 33's "fails with GBM buffers on FreeBSD/drm-kmod" framing is retired at its source — the code comment no longer says it either** |
| **FB-3** | **Hybrid Intel + NVIDIA with `hw.nvidiadrm.modeset=1`** — two KMS devices enumerated; wlroots picks one unaided | **PRESENT, no known impact (downgraded Phase 83)** | Unhandled by choice. `card0` = Intel Iris Xe (eDP-1), `card1` = NVIDIA GTX 1650 Ti. **With FB-4 resolved, wlroots is evidently selecting a workable device on its own**, so the configuration causes no observed harm | Nothing to do while nothing is broken. `hikari_platform_log()` already names the DRM node and the `WLR_DRM_DEVICES` override whenever more than one GPU is present, so a future recurrence is self-diagnosing. Do **not** pin a device pre-emptively — that would hard-code a choice the current stack is making correctly |
| **FB-4** | eDP-1 scanout swapchain test failure | **RESOLVED / STALE — closed Phase 83** | The laptop's built-in panel has worked "for a long time" (user, 2026-08-22). The failure was real when recorded in Phase 19 but has since been fixed by movement in the graphics stack below hikari — Mesa is now 26.1.6, libdrm 2.4.134 | Nothing. **This entry was carried as an open CRITICAL blocker for ~60 phases after it stopped being true**, distorting every priority list that referenced it. See the Phase 83 note on why it survived so long |
| **FB-5** | `epoll-shim` needed for wlroots' `wl_event_loop` | Handled | `Makefile:176-179`, FreeBSD-conditional | Acceptable as-is. A native `kqueue` loop is not available while wlroots owns the event loop |
| **FB-6** | `explicit_bzero` / `setgroups` / `usleep` need `__BSD_VISIBLE`, which FreeBSD's `<sys/cdefs.h>` clears whenever `_POSIX_C_SOURCE` is defined | **RESOLVED Phase 73 — flag retired** (user ruling, Option 1) | `WITH_POSIX_C_SOURCE` no longer exists. The three symbols are reached normally because nothing in the build defines a feature-test macro any more | Nothing outstanding. The knob was never set by `WITH_ALL`, had been a broken configuration since it was added, and strict-POSIX namespace enforcement had no consumer in a FreeBSD-only compositor. `src/topbar.c`'s `__BSD_VISIBLE` comment is now unconditionally true and was kept for that reason |
| **FB-7** | NVIDIA proprietary driver present on the host | **Note only, no action** | Not a project dependency | AGENTS.md §2 governs *project* dependencies; a user's system driver is out of scope. But hikari must not *require* it. With FB-4 resolved there is no FB-3 fix to apply — see that row |
| **FB-8** | `.ifdef` tests definedness, not value, so `make WITH_XWAYLAND=NO` still **enabled** XWayland — no feature could be disabled from the command line at all | **RESOLVED Phase 72** | All 11 switches now `.if defined(X) && ${X:tu} != "NO"`; verified by `make -V` across the matrix, default config unchanged | Nothing outstanding. **Note for future edits:** `.for` + `.undef` normalisation does **not** work — bmake keeps command-line variables in a scope `.undef` cannot touch, so the check must be repeated at each site. Recorded in the Makefile comment |
| **FB-9** | `sysctlbyname("hw.acpi.acline", …)` is FreeBSD-native and unavailable to the Linux-based IDE analysis path | **Introduced by W4** (Q2 power-aware blank timeout) | Idiom already proven at `topbar.c:328-332`; `<sys/sysctl.h>` is new to `lock_mode.c` | Guard as `lock_mode.c:16-18` already guards `explicit_bzero`, so clangd keeps working off-target. No new library. A machine with no battery must fall back to the AC timeout, not to `0` |

**Assessment (Phase 70, updated Phase 83).** Of the nine rows, **FB-2, FB-4, FB-6 and FB-8 are resolved**, and **FB-3 is downgraded to present-but-harmless**. **No row in this register is now a known-open defect.** FB-4 — the eDP-1 scanout failure carried as CRITICAL since Phase 19 — turned out to have been fixed by the graphics stack below hikari and to have been stale for a long time; it was never re-verified because the documentation treated "recorded" as "still true". FB-1 and FB-2 had drifted into being cited as hikari-side FreeBSD hacks and were neither; that record is now corrected both here and in the code comment that originally asserted it. The overall FreeBSD-native debt is therefore **much smaller than the accumulated prose suggested**, and the remaining risk concentrates in one hardware-configuration question that a single environment variable can answer — which `hikari_platform_log()` now names in the log, beside the symptom, whenever more than one GPU is present.

## 14. Screen Sharing & Portal Integration (added 2026-08-22, Phases 78-81)

*Recorded because the failure modes here are silent, span four layers, and cost most of a session to work through. `xdg-desktop-portal-wlr` is the adopted backend (Phase 81); alternative capture routes are deliberately not pursued.*

**The chain. All four must hold, and three are compositor-side.**

| # | Requirement | Where it lives | Failure signature |
|---|---|---|---|
| 1 | A capture protocol the client can use | `server.c`, `HAVE_SCREENCOPY` | portal logs `Compositor supports neither ext_image_copy_capture or wlr_screencopy` |
| 2 | `XDG_CURRENT_DESKTOP` matches a backend's `UseIn` | `start-hikari.sh`, `hikari.desktop` | **no backend resolves at all**; nothing logs why |
| 3 | `WAYLAND_DISPLAY` in the **D-Bus activation environment** | `export_activation_environment()`, `server.c` | backend activates, cannot connect, exits; **nothing logs why** |
| 4 | PipeWire + WirePlumber running | user session (`~/.config/hikari/autostart`) | portal negotiates, stream carries no frames |

**Why (3) is not obvious.** `start-hikari.sh` wraps the compositor in `dbus-run-session`, so the session bus starts *before* the compositor creates its Wayland socket. D-Bus hands every service it activates the environment the bus itself was started with, which therefore can never contain `WAYLAND_DISPLAY` -- and `setenv()` inside `server_init()` cannot retroactively change an already-running bus. `export_activation_environment()` republishes it (plus `DISPLAY`, `XDG_CURRENT_DESKTOP`, `XDG_SESSION_TYPE`, `XDG_RUNTIME_DIR`) after `wlr_backend_start()`.

**Why `ext-image-copy-capture` is opt-in.** portal-wlr *prefers* it the moment it is advertised (it logs `wayland: using ext_image_copy_capture`), and on this hybrid-GPU hardware that path delivers black frames while `wlr-screencopy` captures correctly. Advertising it therefore makes screen sharing worse. Behind `WITH_EXT_IMAGE_CAPTURE`, excluded from `WITH_ALL`. The implementation is entirely inside wlroots -- hikari creates two globals and nothing more -- so there is no hikari-side fix. See section 13, FB-3.

**Diagnostic technique that works.** `grim` is the control: it binds `wlr-screencopy` directly and never faces the protocol choice, so **if `grim` captures real content and a portal client does not, the compositor is not implicated.** Verified this way in Phase 80 (3840x1200, 1520/1600 samples non-black). For the portal side, `xdg-desktop-portal-wlr -l TRACE` prints the chosen backend and per-frame errors -- but it must be started *before* the D-Bus-activated instance claims the name, or it exits with `failed to acquire service name: File exists`.

**Still open.** OBS ScreenCast renders black with every compositor-side requirement verified. The residual failure is in portal-wlr -> PipeWire -> OBS. Leading hypothesis is the same hybrid-GPU dmabuf problem as FB-3: PipeWire negotiates dmabuf with OBS, and a buffer allocated on one GPU and imported on the other yields a connected stream of uniformly black frames. portal-wlr's `force_mod_linear=1` governs only its own allocation, not that handoff, which is consistent with it not helping.
