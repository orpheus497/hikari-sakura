# Hikari Project Blueprint

*Last Updated:* 2026-08-13 19:08

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
- `output.c` connects the `wlr_scene` to the physical `wlr_output`.
- `indicator.c`, `border.c`, and `decoration.c` generate pixel buffers using `cairo` and attach them to the scene graph via `wlr_scene_buffer_create`.

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
- **`border.c`**: Renders 1px/2px borders around windows using cairo buffers. Updates border colors based on active/inactive focus states.
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
3. **UI Buffer Allocation Strategy**: In `indicator.c`, `border.c`, and `lock_indicator.c`, the compositor uses `cairo` to render UI elements, maps the memory, copies it to a `wlr_buffer`, and attaches it to the scene graph. This is CPU intensive. While fine for static borders, redrawing the circular `lock_indicator.c` on every timer tick via cairo software rendering is suboptimal compared to a simple GLES2 shader.
4. **Missing Graceful Degradation on XWayland**: If XWayland crashes, the `wlr_xwayland_surface` callbacks can sometimes trigger use-after-free bugs if the destruction signals aren't perfectly synchronized. The transition to the `wlr_scene` API mitigates this visually, but logical structs can leak.

**Known Limitations (verified, not defects):**

- P2-15: `sig_handler` → `hikari_server_terminate` → `wl_event_loop_add_timer` performs allocation in signal context — async-signal-unsafe, upstream-inherited design, noted only (`src/server.c:1046-1049`, `src/server.c:1137-1140`).

## 5. The eDP-1 Swapchain / DRM Failure Analysis

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

- **H1 (primary):** Mesa DRI/GBM backend broken for the GPU (missing/mismatched `mesa-dri`, or drm-kmod/firmware fault) — one cause explains both log lines.
- **H2:** `IN_FORMATS` modifier-set mismatch between drm-kmod and Mesa GBM.
- **H3:** BO alloc succeeds but `drmModeAddFB2WithModifiers` fails (EINVAL).

Ruled out live (Phase 19): hikari API misuse (commit sequence matches tinywl/wlroots 0.20), seatd/session and `/dev/dri` permissions, configuration load, ZFS/`posix_fallocate` (compositor scanout is GBM/KMS, not shm).

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