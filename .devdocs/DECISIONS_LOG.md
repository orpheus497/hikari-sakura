# Architectural and Structural Decisions Log

*Note: Most recent entries are listed at the top.*

---

## [2026-07-31 20:38] Cleanup: Remove glibc-isms from hikari-unlocker and dead Linux PAM file

* **Context:** Full FreeBSD stack audit revealed `_GNU_SOURCE`, `_DEFAULT_SOURCE`, and a manual `void explicit_bzero(void *, size_t)` prototype in `hikari_unlocker.c`. These are glibc-specific — on FreeBSD, `explicit_bzero` is declared in `<strings.h>` (already included) without feature macros. Additionally, `etc/pam.d/hikari-unlocker.Linux` remains despite Linux support being removed from the project.
* **Decision:** Removed `_GNU_SOURCE`, `_DEFAULT_SOURCE` defines and the redundant prototype. The Linux PAM file should be deleted manually (`rm etc/pam.d/hikari-unlocker.Linux`).
* **Impact:** Cleaner FreeBSD-native code. No functional change — `explicit_bzero` was already available via `<strings.h>`.

---

## [2026-07-31 20:30] Fix: `xdg_surface->data` Type Confusion in Decoration Handler

* **Context:** `hikari_xdg_view_init()` sets `xdg_surface->data = xdg_view->scene_tree` (the wlroots popup parenting convention). However, `server_decoration_handler()` read `xdg_surface->data` as if it were a `hikari_xdg_view*`. Since it's actually a `wlr_scene_tree*`, every decoration event caused heap corruption or segfault by dereferencing a scene tree pointer as a view struct.
* **Decision:** Fixed the decoration handler to follow the correct lookup chain: `xdg_surface->data` → `scene_tree`, then `scene_tree->node.data` → `xdg_view`. Removed the dead store `xdg_surface->data = xdg_view` (line 536 in xdg_view.c) that was immediately overwritten.
* **Impact:** Eliminates crash-level type confusion on every server decoration negotiation.

## [2026-07-31 20:30] Fix: Layer Shell Popup Missing `initial_commit` Handler

* **Context:** In wlroots 0.20, all XDG surfaces (including popups spawned by layer shell surfaces like waybar) require `initial_commit` handling — the compositor must respond with `wlr_xdg_surface_schedule_configure()` on the first commit. The layer shell `commit_popup_handler()` only called `damage_popup()`, skipping this lifecycle step. Popups from layer shell clients could fail to map.
* **Decision:** Added `initial_commit` guard matching the existing XDG view popup handler pattern.
* **Impact:** Layer shell popups (e.g., waybar right-click menus) now correctly map in wlroots 0.20.

## [2026-07-31 20:30] Fix: Cairo Context Leak in `render_image_to_surface()`

* **Context:** `render_image_to_surface()` called `cairo_create(output)` then checked the image surface status with an early return on failure. The early return path did not call `cairo_destroy()`, leaking the cairo context.
* **Decision:** Added `cairo_destroy(cairo)` before the early return.
* **Impact:** No memory leak when loading an invalid PNG background.

## [2026-07-31 20:30] Fix: Noop/Headless Output Missing `wlr_output_init_render()`

* **Context:** `init_noop_output()` created a headless output for the fallback workspace but never called `wlr_output_init_render()`. The regular `new_output_handler()` does call it for real outputs. Without render initialization, any rendering path touching the noop output (e.g., running with no physical monitors) could fail.
* **Decision:** Added `wlr_output_init_render(wlr_output, server->allocator, server->renderer)` before `hikari_output_init()` in `init_noop_output()`.
* **Impact:** Noop/headless output can now safely handle rendering operations.

---

## [2026-07-31 20:14] Fix: Retryable vs Terminal Unlocker Lifecycle in Lock Mode

* **Context:** `hikari-unlocker` runs a `while (!success)` loop: on wrong password (`PAM_AUTH_ERR`) it writes `false`, stays alive, and reads the next password. The previous `locker_result_handler()` unconditionally reaped the child after any result, which would block-deadlock on a retryable `false` (child still alive waiting for stdin).
* **Decision:** Classify results as *terminal* (success, hangup-without-result, read failure) or *retryable* (got `false` result with child still alive). Only terminal results trigger `waitpid(locker_pid, &status, 0)` with EINTR retry and pipe cleanup. Retryable results just show the deny indicator — the child stays alive and `submit_password()` will send the next attempt. `start_unlocker()` now returns `bool`; `submit_password()` guards against `locker_pid <= 0` to prevent writing to invalid descriptors.
* **Impact:** Wrong-password retries no longer deadlock or orphan the unlocker. Fatal failures still guarantee child reaping and pipe cleanup.

## [2026-07-31 16:45] Fix: `output->server` Not Initialized in `hikari_output_init()`

* **Context:** `struct hikari_output` has a `server` field used by `frame_handler` (`output.c:263`: `output->server->scene`). This field was only set by the caller in `new_output_handler` (`server.c:226`), not inside `hikari_output_init()`. If any other code path called `hikari_output_init()` without setting `output->server`, a NULL dereference would occur.
* **Decision:** Added `output->server = &hikari_server;` inside `hikari_output_init()`. Since `hikari_server` is a global singleton, this is safe and idempotent with the caller's assignment.
* **Impact:** Defensive robustness — init is now self-contained.

## [2026-07-31 16:45] Fix: Duplicate `#include` Directives in `server.c`

* **Context:** `src/server.c` had duplicate `#include <wlr/types/wlr_data_device.h>` (lines 19, 31) and `#include <wlr/types/wlr_seat.h>` (lines 25, 32). No functional impact, but violates code hygiene standards.
* **Decision:** Removed the duplicate includes on lines 31-32.
* **Impact:** Cosmetic cleanup — no behavioral change.

---

## [2026-07-31 16:34] Fix: Switch Toggle Handler Cascading If Bug

* **Context:** Full codebase wiring audit discovered that `toggle_handler` in `src/switch.c` used two sequential `if` statements instead of `if/else if`. After the first block set `state = WLR_SWITCH_STATE_ON`, the second `if (state == ON)` immediately fired because there was no `else`. Both begin AND end actions executed on every toggle event.
* **Decision:** Changed the second `if` to `else if` so only one branch executes per toggle event.
* **Impact:** Switch-based operations (e.g., laptop lid toggle actions) now fire correctly — begin on OFF→ON, end on ON→OFF.

## [2026-07-31 16:34] Fix: Output Cairo Surface Status Check (Wrong Surface)

* **Context:** In `hikari_output_load_background()` (`src/output.c:85`), after creating `output_surface` via `cairo_image_surface_create`, the status check was `cairo_surface_status(image)` instead of `cairo_surface_status(output_surface)`. This meant an `output_surface` allocation failure would go undetected if `image` was valid.
* **Decision:** Changed to `cairo_surface_status(output_surface)`.
* **Impact:** Prevents use of a failed cairo surface for background rendering.

## [2026-07-31 16:17] Decision: Non-blocking PAM Authentication I/O (BUG-6 Resolved)

* **Context:** `submit_password()` in `lock_mode.c` used a synchronous `read(locker_pipe[1][0], &success, sizeof(bool))` that blocked the entire Wayland event loop during `pam_authenticate()`. PAM's `pam_unix.so` may delay 1-3 seconds on failure, freezing all rendering and input.
* **Decision:** Replaced blocking `read()` with `wl_event_loop_add_fd()`. A new `locker_result_handler()` callback fires asynchronously when `hikari-unlocker` writes the result boolean. The compositor event loop continues processing frames and input during authentication.
* **Implementation:** Added `locker_event_source` field to `struct hikari_lock_mode`. The fd source is registered per-submission and cleaned up in both the result handler and the `cancel()` path.
* **Impact:** Resolves BUG-6 from Phase 11. Compositor remains responsive during password verification.

---

## [2026-07-31 16:17] Decision: PAM Config — Use `auth include system` Instead of `auth include passwd`

* **Context:** `hikari-unlocker.FreeBSD` contained `auth include passwd`. Live system verification confirmed FreeBSD's `/etc/pam.d/passwd` explicitly states: "passwd(1) does not use the auth, account or session services." The file contains only a `password` stack (for changing passwords), not an `auth` stack. This means `pam_authenticate()` would always fail because OpenPAM finds no auth rules in the include chain.
* **Decision:** Changed to `auth include system`. The `/etc/pam.d/system` file contains the correct auth chain: `auth required pam_unix.so no_warn try_first_pass nullok`.
* **Impact:** Screen unlock authentication will now work correctly on FreeBSD.

---

## [2026-07-31 16:12] Research Finding: ZFS Automount Overrides fstab tmpfs on /tmp

* **Context:** Live system testing revealed that although `/etc/fstab` contains a `tmpfs /tmp` entry, `stat -f '%T' /tmp` reports `zfs`. The mount table shows BOTH `tmpfs on /tmp` and `zroot/tmp on /tmp` — ZFS automount runs after fstab and mounts the dataset on top of the tmpfs.
* **Technical Finding:** `posix_fallocate()` returns `EOPNOTSUPP (45)` on both `/var/run/user/1001` (ZFS) and `/tmp` (ZFS over tmpfs). However, `shm_open()` + `posix_fallocate()` and `memfd_create()` + `posix_fallocate()` both succeed — POSIX SHM and anonymous memory bypass ZFS entirely. wlroots 0.20 uses `shm_open()` (confirmed via `nm -D`), not filesystem-backed temp files.
* **Decision:** Fix is `sudo zfs set canmount=noauto zroot/tmp` (system admin, one command). Added ZFS detection warning to `start-hikari.sh` and expanded README with step-by-step instructions.
* **Impact:** Users with ZFS root will get actionable warnings instead of silent client failures.

---

## [2026-07-31 15:46] Research Finding: XDG_RUNTIME_DIR on ZFS Incompatible with Wayland

* **Context:** Deep investigation into whether mounting XDG on tmpfs works with ZFS or needs re-addressing. System analysis revealed FreeBSD 15.1-RELEASE with full ZFS root (`zroot`). `/var/run/user/1001` (set by `pam_xdg` via `/etc/pam.d/system`) resides on the root ZFS dataset. `/tmp` is also ZFS-backed (`zroot/tmp`). The only tmpfs mount on the system is `/compat/linux/dev/shm` (Linux compat layer).
* **Technical Finding:** ZFS on FreeBSD does not support `posix_fallocate()` — returns `EINVAL` (since FreeBSD r325320, 2017). ZFS's Copy-on-Write architecture cannot provide the pre-allocation guarantees POSIX requires. Wayland clients use `posix_fallocate()` to pre-allocate `wl_shm` shared memory buffers inside `XDG_RUNTIME_DIR`. When `XDG_RUNTIME_DIR` is on ZFS, these allocations fail, causing client crashes or rendering failures.
* **Impact on hikari:** The `start-hikari.sh` fallback to `/tmp/hikari-runtime-$UID` does not resolve the issue because `/tmp` is also ZFS. The fallback also never triggers because `pam_xdg` already sets `XDG_RUNTIME_DIR`. Runtime testing is blocked until this is resolved.
* **Decision:** Must implement a tmpfs mount for `XDG_RUNTIME_DIR`. Four options identified:
  - **Option A (Recommended):** Mount tmpfs at `/var/run/user` via `/etc/fstab`
  - **Option B:** Update `start-hikari.sh` to use a verified tmpfs-backed path (e.g., `/dev/shm`)
  - **Option C:** Replace `zroot/tmp` with tmpfs at `/tmp`
  - **Option D:** Mount tmpfs in the wrapper script (requires privileges)
* **Status:** Pending implementation — awaiting user direction on preferred option.

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

## [2026-07-29 04:47] Decision: Sheet Pool Capacity & Array Contiguity [SUPERSEDED]

* **Context:** `hikari_workspace` allocates its 10 sheets simultaneously via `calloc(HIKARI_NR_OF_SHEETS, sizeof(struct hikari_sheet))` to hold them in a contiguous array format. Our Slab allocator traditionally manages single instances per block.
* **Decision:** To guarantee array contiguity without modifying `struct hikari_workspace` pointer mechanics or breaking `wl_list`, the `sheet_pool` `item_size` in `src/server.c` is initialized to `HIKARI_NR_OF_SHEETS * sizeof(struct hikari_sheet)`. A single allocation from the pool yields the contiguous block necessary for the workspace arrays.
* **Superseded by:** The object pool allocator (`pool.c`, `pool.h`) was removed entirely in the 2026-07-29 15:16 decision "Revert DOD SoA Tables and Object Pool Allocator". Sheet allocation now uses standard `hikari_malloc`/`calloc`.

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

### 1. Non-Blocking PAM I/O for `hikari-unlocker` (Bug 6) [SUPERSEDED]

* **Status:** Fully implemented (2026-07-31 16:17). See decision entry "Non-blocking PAM Authentication I/O (BUG-6 Resolved)" above.
* **Resolution:** Child-process fork with pipe IPC and `wl_event_loop_add_fd()` callback. All tabled design questions resolved by the implementation.
* **Remaining:** Live-test PAM non-blocking unlock on FreeBSD target to confirm end-to-end behavior.

### 2. PAM Verification (`hikari-unlocker`)

* **Context / Clarification:** The unlocker requires root privileges to read `/etc/master.passwd` via OpenPAM on FreeBSD. We must ensure the binary is owned by `root:wheel` and has the `4555` setuid bit. Testing this requires a live FreeBSD system; it cannot be simulated inside an unprivileged sandbox.
* **Tabled Questions:**
  * Q: Are there specific native FreeBSD testing harnesses for Wayland surfaces we should use instead of manual Wayland clients?
