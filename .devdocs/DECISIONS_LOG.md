# Architectural and Structural Decisions Log

*Note: Most recent entries are listed at the top.*

---

## [2026-07-31 14:49] Decision: wlr_session Ownership — Do Not Destroy Separately

* **Context:** `hikari_server_stop()` and `hikari_server_prepare_privileged()` error path both called `wlr_session_destroy()` after `wlr_backend_destroy()`. Reading the wlroots 0.20 `backend.h` header confirmed `wlr_backend_autocreate` creates a session that is **owned by the backend**. The `wlr_session` struct has an internal `event_loop_destroy` listener for cleanup. The tinywl 0.20 reference implementation never calls `wlr_session_destroy`. The double destroy is a use-after-free.
* **Decision:** Removed both `wlr_session_destroy` calls. The session is destroyed by the backend automatically.
* **Impact:** Eliminates crash/heap corruption on compositor shutdown.

## [2026-07-31 14:49] Decision: Use wlr_output_preferred_mode Instead of Manual First Mode

* **Context:** Output initialization manually picked the first mode from `wlr_output->modes` via `wl_container_of(wlr_output->modes.next, mode, link)`. This is not guaranteed to be the EDID-preferred mode. The tinywl reference uses `wlr_output_preferred_mode()`.
* **Decision:** Replaced with `wlr_output_preferred_mode(wlr_output)` which returns the mode flagged as preferred by the monitor.
* **Impact:** Ensures native resolution on monitors that report a preferred mode.

## [2026-07-31 14:49] Decision: Desktop File Should Use start-hikari Wrapper

* **Context:** `hikari.desktop` had `Exec=hikari` which bypasses the wrapper script that provides D-Bus session wrapping and XDG_RUNTIME_DIR bootstrapping. Display managers launching via this file would silently lack D-Bus, breaking portals/clipboard/secrets.
* **Decision:** Changed to `Exec=start-hikari`. Added `start-hikari` installation to Makefile.
* **Impact:** Display manager launches now get proper D-Bus session and XDG_RUNTIME_DIR.

---

## [2026-07-31 14:20] Decision: wlroots 0.20 Initial Commit Lifecycle Pattern

* **Context:** The compositor crashed with `Assertion failed: (surface->initialized)` in `wlr_xdg_surface_schedule_configure`. Deep analysis revealed this is NOT just a single bad call — it's a missing lifecycle pattern. In wlroots 0.20, the `new_toplevel` signal fires before the surface is initialized. The compositor must register a commit listener at `new_toplevel` time and handle `initial_commit` by calling `wlr_xdg_toplevel_set_size(0, 0)`, which sets `initialized = true`. Without this, the surface can never map, and any configure call will crash. Cross-referenced against tinywl 0.20.
* **Decision:** Moved commit listener registration from `map()` to `hikari_xdg_view_init()` (new_toplevel time). Added `initial_commit` guard at the top of `commit_handler` that calls `wlr_xdg_toplevel_set_size(0, 0)` and returns early. Added `popup_commit_handler` with `initial_commit` → `wlr_xdg_surface_schedule_configure`. Guarded `request_fullscreen_handler` with `surface->initialized` check. Added `commit` member to `hikari_xdg_popup` struct.
* **Impact:** Resolves the `surface->initialized` assertion crash. XDG surfaces now follow the correct wlroots 0.20 initialization handshake and can successfully map.

---

## [2026-07-31 12:47] Decision: Revert Native Environment Bootstrapping

* **Context:** The previous decision to inject `setup_env()` in `main.c` violated Wayland architectural standards. Compositors should not generate their own IPC bus (`XDG_RUNTIME_DIR`) or wrap themselves in `dbus-run-session` natively.
* **Decision:** Removed `setup_env()` from `main.c`. Added detailed diagnostic error messages to `server.c` for `wlr_backend_autocreate` failures. Created `start-hikari.sh` to handle dbus/XDG environment bootstrapping externally.
* **Impact:** `hikari` complies with proper `wlroots` daemon and wrapper architectures. C code is cleaner and adheres to the separation of concerns.

## [2026-07-31 12:21] Decision: Native Environment Bootstrapping [REVERTED]

* **Context:** `hikari` failed to run natively on FreeBSD, falling back to a nested Wayland session that caused assertion crashes because `seatd`, `dbus`, and `XDG_RUNTIME_DIR` were not configured properly.
* **Decision:** Implemented `setup_env()` in `src/main.c` before parsing options to dynamically generate `XDG_RUNTIME_DIR` if missing, encapsulate execution via `execvp("dbus-run-session", ...)`, and strictly unset `WAYLAND_DISPLAY` and `DISPLAY` to prevent accidental nesting.
* **Impact:** `hikari` is guaranteed to launch on native DRM/libinput backends and avoids wlroots Wayland-backend bugs.

## [2026-07-31 12:21] Decision: Remove Manual Damage Ring Hooks

* **Context:** Manual `wlr_damage_ring` logic was left as migration debt. `wlr_scene` handles surface damage implicitly.
* **Decision:** Removed `wlr_damage_ring_add_whole` and `wlr_damage_ring_add` from output utilities (`src/output.c`, `include/hikari/output.h`). Retained `wlr_output_schedule_frame`.
* **Impact:** Eliminates redundant damage tracking and aligns fully with `wlr_scene` architecture.

---

## Architectural Decisions

### Architecture: XDG Shell Surface Initialization

* **Context:** In wlroots 0.17+, `wlr_xdg_shell.events.new_surface` emits before the surface role (toplevel or popup) is assigned, causing `xdg_surface->toplevel` to be NULL and leading to segmentation faults when clients connect.
* **Decision:** Migrated `wlr_xdg_shell` event binding from `new_surface` to `new_toplevel`, guaranteeing that the surface is fully initialized as a toplevel before `hikari` processes it. Popups are already correctly handled internally via the toplevel's `new_popup` event.
* **Impact:** Prevents compositor crashes when XDG clients (like `foot`) map their windows.

### Architecture: Background Buffer Allocation (FreeBSD)

* **Context:** Forcing `DRM_FORMAT_MOD_LINEAR` during background buffer allocation caused `wlr_allocator_create_buffer` to fail or return CPU-unmappable buffers on FreeBSD's GBM backend, leading to a permanent black screen for the wallpaper.
* **Decision:** Removed hardcoded modifiers (`.len = 0, .modifiers = NULL`), aligning the background allocator with the UI text allocator (`indicator_bar.c`).
* **Impact:** Allows the allocator to implicitly select the optimal fallback (e.g., SHM), resolving the black screen without requiring a custom `wlr_buffer` implementation.



### Architecture: Scene Output Initialization Order

* **Context:** Moving to `wlr_scene` revealed a timing flaw where `wlr_output_layout_add` emitted signals causing frames to be scheduled *before* `scene_output` was created. This caused early frames to damage without a valid output backing, leading to a black screen and unresponsiveness.
* **Decision:** Moved `wlr_scene_output_create` and `wlr_scene_output_layout_add_output` to occur *before* `wlr_output_layout_add` inside `hikari_output_init`.
* **Impact:** Resolves compositor black-screen failures on startup, ensuring the damage ring is properly attached before layout changes trigger initial frames.

### API Migration: wlroots 0.20 Output State Management

* **Context:** wlroots 0.20 removed the implicit enablement and mode-setting from standard output signals.
* **Decision:** Adopted `wlr_output_state` and `wlr_output_commit_state` explicitly during `hikari_output_enable` and `hikari_output_disable`.
* **Impact:** Restores normal monitor power-management and resolution negotiation.

### API Migration: Preserve `xdg_surface->data = scene_tree` Convention

* **Context:** During code review, the assignment `xdg_surface->data = xdg_view->scene_tree` appeared to overwrite the `xdg_view` back-reference. Cross-referencing against tinywl (wlroots master) revealed this is the standard wlroots popup parenting convention: `xdg_surface->data` stores the scene_tree so `wlr_scene_xdg_surface_create` can find the parent scene node for popups via `parent->data`.
* **Decision:** Reverted the removal. `scene_tree->node.data = xdg_view` (for view lookup) and `xdg_surface->data = scene_tree` (for popup parenting) are on different objects and serve different purposes. Both are required.

---

## [2026-07-30 01:45] Decision: Track Manual Damage Ring Calls as Migration Debt [SUPERSEDED]

* **Context:** `hikari_output_damage_whole()` and `hikari_output_add_effective_surface_damage()` reach into `scene_output->damage_ring` directly. Verified against tinywl and labwc — neither uses manual damage ring calls when using `wlr_scene`. The scene graph handles damage tracking internally via `wlr_scene_output_commit`.
* **Decision:** Retain the manual calls for now (hikari is mid-migration, some damage sources may not be scene-managed). Tracked in TODOS.md for removal once all visual elements are scene graph nodes. *(Note: Manual damage ring calls were removed in the 2026-07-31 12:21 decision once scene graph migration was fully adopted.)*

---

## [2026-07-29 15:16] Decision: Revert DOD SoA Tables and Object Pool Allocator
* **Context:** The custom object pool allocator and DOD SoA view state/geometry tables added complexity without proven benefit. The wlr_scene migration made the custom renderer (which DOD optimized for) obsolete.
* **Decision:** Removed pool.c/pool.h, reverted view flags to local struct field, removed all dod_id/view_state indirection.

---

## [2026-07-29 15:16] Decision: Migrate Rendering to wlr_scene Graph
* **Context:** wlroots 0.18+ provides wlr_scene for automatic damage tracking and composition, eliminating the need for manual renderer passes.
* **Decision:** Gutted renderer.c, migrated borders to wlr_scene_rect nodes, lock indicator and backgrounds to wlr_scene_buffer nodes. Scene graph handles damage tracking and output composition automatically.

---

### Architecture: Continuous Quad Batch Rendering [SUPERSEDED]

* **Context:** Wayland rendering overhead via multiple wlroots API calls per frame impacted FreeBSD native performance.
* **Decision:** Implemented a single-pass `hikari_renderer` loop that buffers texture/color quads and flushes them in a single batch. Note: This was implemented but subsequently REVERTED as `wlr_scene` natively handles optimal rendering without requiring a manual batching pipeline.
* **Impact:** Removed 30+ internal API roundtrips per compositor frame.

---

### Architecture: Hybrid DOD (Data-Oriented Design) View State [SUPERSEDED]

* **Context:** The proliferation of linked lists for view states hampered cache coherency.
* **Decision:** Replaced scattered structs with a centralized SoA (Struct-of-Arrays) layout in `hikari_server`. Note: This was implemented but subsequently REVERTED as it was incompatible with wlroots 0.20's `wlr_scene` graph requirements.
* **Impact:** State mutations require table lookups rather than pointer dereferences.

---

## [2026-07-29 04:47] Decision: Sheet Pool Capacity & Array Contiguity
* **Context:** `hikari_workspace` allocates its 10 sheets simultaneously via `calloc(HIKARI_NR_OF_SHEETS, sizeof(struct hikari_sheet))` to hold them in a contiguous array format. Our Slab allocator traditionally manages single instances per block.
* **Decision:** To guarantee array contiguity without modifying `struct hikari_workspace` pointer mechanics or breaking `wl_list`, the `sheet_pool` `item_size` in `src/server.c` is initialized to `HIKARI_NR_OF_SHEETS * sizeof(struct hikari_sheet)`. A single allocation from the pool yields the contiguous block necessary for the workspace arrays.

---

## [2026-07-29 03:15] Decision: Data-Oriented Design (DOD) Orientation & FreeBSD Primary Target [SUPERSEDED in part]

* **Context:** The user requested modernizing the `hikari` Wayland compositor with primary focus on FreeBSD compatibility, thorough documentation inside `docs/`, and adoption of Data-Oriented Design (DOD) principles.
* **Decision:**
  1. (Historical Intent) Structure core data layouts (views, sheets, groups, outputs, tiles) into cache-aligned contiguous arrays / struct-of-arrays (SoA) where appropriate to minimize pointer chasing during render/layout loops. *Note: The DOD architecture was superseded by the wlr_scene migration.*
  2. Isolate FreeBSD platform integration requirements (`evdev`, `epoll-shim`, `tmpfs` `/tmp` `posix_fallocate`, PAM unlocker, `seatd`) in system setup documentation (`.devdocs/docs/freebsd_requirements.md`) and build definitions (`Makefile`).
  3. Strict adherence to `AGENTS.md` operational cycle: Ask → Explain → Justify → Wait for Approval → Execute.

---

## [2026-07-29 03:15] Decision: Devdocs Separation of Concerns
* **Context:** `AGENTS.md` mandates absolute separation of AI tracking docs (`.devdocs/`) from product documentation (`docs/`) and code in root.
* **Decision:** Keep all operational and tracking files inside `.devdocs/` and user/product technical documentation inside `.devdocs/docs/`.

---

## Design Implementation Requests

### 1. Non-Blocking PAM I/O for `hikari-unlocker` (Bug 6)
* **Context / Clarification:** Currently, PAM authentication blocks the main `wl_event_loop`, freezing the compositor. The structural solution is to fork `hikari-unlocker` or use a pipe so the compositor can continue rendering while waiting for authentication over `wl_event_loop_add_fd`.
* **Tabled Questions:**
  * Q: How should we bridge OpenPAM's inherently synchronous `pam_authenticate` calls with the asynchronous `wl_event_loop` in the compositor?
  * Q: Should we isolate the PAM authentication into a separate threaded worker or child process that communicates via `wl_event_loop_add_fd` pipes?
  * Q: Does FreeBSD provide asynchronous PAM extensions we could leverage?

### 2. PAM Verification (`hikari-unlocker`)
* **Context / Clarification:** The unlocker requires root privileges to read `/etc/master.passwd` via OpenPAM on FreeBSD. We must ensure the binary is owned by `root:wheel` and has the `4555` setuid bit. Testing this requires a live FreeBSD system; it cannot be simulated inside an unprivileged sandbox.
* **Tabled Questions:**
  * Q: Are there specific native FreeBSD testing harnesses for Wayland surfaces we should use instead of manual Wayland clients?
