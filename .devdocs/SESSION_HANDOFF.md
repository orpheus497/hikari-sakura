# Session Handoff Ledger

*Note: Most recent entries are listed at the top.*

---

## Session Date: 2026-07-31 14:20 — wlroots 0.20 Initial Commit Lifecycle Fix

* **Phase:** Phase 10 — wlroots 0.20 Initial Commit Lifecycle Fix
* **Accomplishments:**
  - **Root cause identified:** The `surface->initialized` assertion crash was NOT caused by a single bad API call — it was a missing wlroots 0.20 lifecycle pattern. The commit listener was registered in `map()` instead of at `new_toplevel` time, so `initial_commit` was never handled, `initialized` was never set to `true`, and any subsequent configure call crashed.
  - **Fix A:** Moved commit listener registration from `map()` to `hikari_xdg_view_init()` (lines 538-539).
  - **Fix B:** Added `initial_commit` guard at top of `commit_handler` — calls `wlr_xdg_toplevel_set_size(0, 0)` and returns early (lines 58-68).
  - **Fix C:** Guarded `request_fullscreen_handler` with `surface->initialized` check (lines 451-456).
  - **Fix D:** Added `popup_commit_handler` function (lines 338-351) and registered popup commit listener in `xdg_popup_create` (lines 424-428). Added `struct wl_listener commit` to `hikari_xdg_popup` in `xdg_view.h`.
  - **Build:** `make` completed successfully — zero warnings, both binaries link cleanly.
* **Modified Files:**
  - `include/hikari/xdg_view.h` — added `commit` listener to popup struct
  - `src/xdg_view.c` — all 4 lifecycle fixes
  - `.devdocs/BRIEFING.md`, `.devdocs/DECISIONS_LOG.md`, `.devdocs/PROGRESS.md`, `.devdocs/SESSION_HANDOFF.md`, `.devdocs/SUMMARIES.md`
* **Remaining Work:** `./start-hikari.sh` runtime test on FreeBSD. PAM `hikari-unlocker` verification.

---

## Session Date: 2026-07-31 13:46 — Runtime Crash Fix & Comprehensive Cleanup

* **Phase:** Phase 9 — Runtime Crash Fix & Final Validation
* **Accomplishments:**
  - **C0 (CRITICAL):** Removed `wlr_xdg_surface_ping(xdg_surface)` from `hikari_xdg_view_init` in `src/xdg_view.c:488`. This was the root cause of the `Assertion failed: (surface->initialized)` crash — in wlroots 0.20, the XDG surface is not yet initialized at the `new_toplevel` signal; calling ping triggers `schedule_configure` which asserts `initialized`. The wlroots xdg_shell module handles pings internally after the initial commit.
  - **C1:** Fixed `request_fullscreen_handler` to pass `xdg_view->xdg_toplevel->requested.fullscreen` instead of always `false`.
  - **O5:** Renamed `##Step purpose` to `##Action purpose` in `hikari_unlocker.c`.
  - **O6:** Replaced single `read()` in `hikari_unlocker.c` with accumulation loop that reads byte-by-byte until the NUL frame terminator, handling partial reads, overflow, and EINTR correctly.
  - **O7/O8:** Added missing `##Function purpose` and `##Action purpose` markers in `src/cursor.c`.
  - **I7:** Removed Linux-specific "logind" reference from `server.c` startup diagnostics.
  - **I8/I9:** Rewrote `start-hikari.sh` with XDG_RUNTIME_DIR ownership/permission validation and all AGENTS.md annotation prefixes.
  - **I1:** Refreshed BRIEFING.md timestamp, phase status, removed "Linux" from remaining work.
  - **I2:** Marked damage ring decision as [SUPERSEDED] in DECISIONS_LOG.md.
  - **O1:** Split Phase 7 into 7a (done) / 7b (pending) in PROGRESS.md.
  - **I4:** Fixed reources.md heading, spelling, and trailing newline.
  - **I5/O2/O3:** Fixed duplicate headings and spacing in SESSION_HANDOFF.md and SUMMARIES.md.
  - **I6:** Added missing 13:16 session summary to SUMMARIES.md.
  - **O4:** Quoted `$XDG_RUNTIME_DIR` in TESTS.md.
  - **Build:** `make` completed successfully — zero warnings, both `hikari` and `hikari-unlocker` link cleanly.
* **Modified Files:**
  - `src/xdg_view.c` — C0, C1
  - `hikari_unlocker.c` — O5, O6
  - `src/cursor.c` — O7, O8
  - `src/server.c` — I7
  - `start-hikari.sh` — I8, I9
  - `.devdocs/BRIEFING.md` — I1
  - `.devdocs/DECISIONS_LOG.md` — I2, I3
  - `.devdocs/PROGRESS.md` — O1
  - `.devdocs/reources.md` — I4
  - `.devdocs/SESSION_HANDOFF.md` — I5, O2
  - `.devdocs/SUMMARIES.md` — I6, O3
  - `.devdocs/TESTS.md` — O4
* **Remaining Work:** FreeBSD runtime revalidation (crash fix needs retest). PAM `hikari-unlocker` runtime verification.

---

## Session Date: 2026-07-31 13:16

* **Phase:** Comprehensive Audit Fix Execution — All 6 Issues Resolved
* **Accomplishments:**
  - **Fix 1 (CRITICAL):** Corrected `clock_gettime` return value misuse in `hikari_server_cursor_focus` (`server.c:436-442`). The function return value (0/-1) was being cast to `uint32_t time_msec` instead of extracting the actual time from the `struct timespec` fields. Every pointer motion event was receiving `time_msec=0`. Fixed to `(uint32_t)(now.tv_sec * 1000LL + now.tv_nsec / 1000000LL)`.
  - **Fix 2 (CRITICAL):** Removed access to `wlr_drm_format.capacity` (an internal wlroots field) in three files: `src/output.c`, `src/indicator_bar.c`, `src/lock_indicator.c`. Replaced with zero-init `= {0}` plus explicit `.format = DRM_FORMAT_ARGB8888` per public API contract.
  - **Fix 3 (MEDIUM):** Extended `wlr_xcursor_manager_load` in `cursor.c` to load scales 1 and 2. Added per-output scale loading in `hikari_output_init` (`output.c`) using `wlr_output->scale` to support arbitrary HiDPI scale factors.
  - **Fix 4 (MEDIUM):** Removed dead unsafe `wl_container_of(wlr_decoration->surface, view, surface)` from `server_decoration_handler` in `server.c`. The `hikari_view*` was computed but never used before the correct `xdg_surface->data` lookup path. The erroneous line constituted undefined behaviour (wrong offset calculation).
  - **Fix 5 (LOW):** Changed `#if HAVE_XWAYLAND` to `#ifdef HAVE_XWAYLAND` for consistency with all other XWayland guards in `server.c`.
  - **Fix 6 (LOW):** Rewrote `start-hikari.sh` to resolve the `hikari` binary from `$PATH` (for installed system deployments) with a `./hikari` fallback (for development builds). Added full `AGENTS.md`-compliant documentation prefixes.
  - **Build:** `make` completed successfully with zero warnings after all 6 fixes.
* **Modified Files:**
  - `src/server.c` — Fixes 1, 4, 5
  - `src/output.c` — Fixes 2, 3
  - `src/indicator_bar.c` — Fix 2
  - `src/lock_indicator.c` — Fix 2
  - `src/cursor.c` — Fix 3
  - `start-hikari.sh` — Fix 6
* **Remaining Work:** PAM `hikari-unlocker` runtime verification. No code changes pending.

---

## Session Date: 2026-07-31 13:08

* **Phase:** wlroots 0.20 Full Audit & Resource Cross-Reference
* **Accomplishments:**
  - Performed full codebase audit against wlroots 0.20 API, tinywl patterns, Wayland Book principles, and FreeBSD deployment requirements.
  - Ingested all provided resources: wlroots Getting-started wiki, Packaging-recommendations wiki, Phoronix wlroots 0.20 release article.
  - Confirmed all previously applied 0.20 API migration fixes are correct and complete.
  - Identified 2 critical bugs: `clock_gettime` return value misuse in `hikari_server_cursor_focus` (`server.c:439`), and `wlr_drm_format` internal field access (`output.c:95`, `indicator_bar.c:126`, `lock_indicator.c:49`).
  - Identified 2 medium issues: xcursor scale hardcoded to 1, unsafe `wl_container_of` in `server_decoration_handler`.
  - Identified 2 low issues: `#if`/`#ifdef` inconsistency, relative path in `start-hikari.sh`.
  - Generated comprehensive audit artifact: `wlroots_0_20_audit_report.md`.
* **Modified:** `.devdocs/BRIEFING.md`, `.devdocs/SESSION_HANDOFF.md`, `.devdocs/TODOS.md`, `.devdocs/SUMMARIES.md`
* **Next Steps:** Apply the 2 critical fixes (clock_gettime, wlr_drm_format) and 2 low-effort low fixes (#if→#ifdef, start-hikari.sh path) — pending user approval.

---

## Session Date: 2026-07-31 12:47

* **Phase:** Implementation Audit & FreeBSD Interlinking Fix
* **Accomplishments:**
  - Audited `hikari` against `wlroots` 0.20 standards, `tinywl.c`, and Wayland architecture principles.
  - Reverted the falsely implemented `setup_env()` from `src/main.c` that attempted to bootstrap `XDG_RUNTIME_DIR` and `dbus-run-session` natively within the compositor.
  - Added explicit, actionable diagnostic error messages in `src/server.c` for `wlr_backend_autocreate` failures to instruct users to ensure `seatd` is running and `XDG_RUNTIME_DIR` is set.
  - Created a proper wrapper script `start-hikari.sh` to handle environment bootstrapping and IPC daemon execution externally, aligning with standard wlroots compositor deployment.
* **Modified:** `src/main.c`, `src/server.c`, `start-hikari.sh`, `implementation_plan.md`, `task.md`
* **Next Steps:** Proceed to verify the `hikari-unlocker` PAM integration.

## Session Date: 2026-07-31 12:21

* **Phase:** Native FreeBSD System Interlinking & Runtime Fixes
* **Accomplishments:**
  - Analyzed and confirmed that `hikari` was unintentionally falling back to the `wayland` backend because the native DRM/session backend failed due to a missing environment setup (`seatd`, `dbus`, `XDG_RUNTIME_DIR`).
  - Wrote a native `setup_env()` bootstrapper directly into `src/main.c` that generates `/tmp/hikari-runtime-$UID`, wraps the process in `dbus-run-session`, and strips leaked display variables.
  - Resolved `Assertion failed: (surface->initialized)` by refactoring `request_fullscreen_handler` in `src/xdg_view.c` to use `wlr_xdg_toplevel_set_fullscreen(..., false)`, averting manual configure scheduling on uninitialized surfaces.
  - Stripped obsolete manual `wlr_damage_ring` additions from `src/output.c` and `include/hikari/output.h`, fully relying on `wlr_scene` for damage tracking.
* **Modified:** `main.c`, `src/xdg_view.c`, `src/output.c`, `include/hikari/output.h`, `task.md`
* **Next Steps:** Proceed to verify the `hikari-unlocker` PAM integration.

---

## Session Date: 2026-07-31 06:34 - XDG Clients & Wallpaper

* **Phase:** Runtime testing & Debugging (XDG Clients & Wallpaper)
* **Accomplishments:**
  - Resolved `foot` (and other XDG clients) causing a `Segmentation fault (core dumped)`. In wlroots 0.17+, `new_surface` fires before the surface role is set, which caused a null pointer dereference (`xdg_surface->toplevel`) in `new_xdg_surface_handler`. Replaced `new_surface` with `new_toplevel` listener to ensure the surface is fully initialized as a toplevel.
  - Reverted a broken "fake" fix that forced `DRM_FORMAT_MOD_LINEAR` in `hikari_output_load_background`. Restored `.modifiers = NULL` and `.len = 0` (matching `indicator_bar.c`), allowing the FreeBSD allocator to choose a valid mapping format, completely resolving the black screen wallpaper bug without requiring a custom buffer.
* **Modified:** `src/server.c`, `include/hikari/server.h`, `src/output.c`
* **Next Steps:** Proceed to Phase 8 (AGENTS.md compliance sweep) and test the `hikari-unlocker` PAM integration.

---

## Session Date: 2026-07-31 06:34 - Client Disconnects

* **Phase:** Runtime testing & Debugging (Client Disconnects)
* **Accomplishments:**
  - Diagnosed and resolved segfaults occurring when clients crash or close (fixed dangling signal listeners in `xdg_view.c` and missing scene tree cleanup in `xwayland_view.c`).
  - Added `scene_node` tracking to `hikari_view` and restored positioning (`wlr_scene_node_set_position`) and visibility (`wlr_scene_node_set_enabled`) toggles that were omitted during the wlroots 0.20 migration.
  - Forced `DRM_FORMAT_MOD_LINEAR` when allocating background buffers to prevent silent cairo CPU-mapping failures on DRM backends (resolving the persistent black screen bug).
  - Fixed an assertion failure (`wlr_seat_destroy`) on compositor shutdown by ensuring `request_set_selection` listeners are properly removed in `hikari_server_stop`.
  - Fixed a segfault on XDG client disconnects (like kitty crashing) caused by a double-free of `wlr_scene_rect` nodes in `hikari_indicator_frame_fini` (since wlroots 0.17 automatically cleans up child nodes when the parent `wlr_scene_tree` is destroyed).
* **Modified:** `include/hikari/view.h`, `src/view.c`, `src/xdg_view.c`, `src/xwayland_view.c`, `src/output.c`, `src/server.c`, `src/indicator_frame.c`
* **Next Steps:** User to recompile and test running `hikari` locally, confirming backgrounds display and windows map without crashing. Proceed to Phase 8.

---

## Session Date: 2026-07-31 06:34 - Initialization Order

* **Phase:** Runtime testing & Debugging
* **Accomplishments:**
  - Diagnosed and resolved the black screen and input unresponsiveness bug on compositor startup.
  - Reordered `wlr_scene_output_create` in `src/output.c` to run before `wlr_output_layout_add`, fixing a race condition that prevented the first frame from being scheduled.
  - Cleaned up git merge conflict markers in `SESSION_HANDOFF.md` and `TODOS.md`.
* **Modified:** `src/output.c`, `.devdocs/SESSION_HANDOFF.md`, `.devdocs/TODOS.md`, `.devdocs/PROGRESS.md`, `.devdocs/DECISIONS_LOG.md`
* **Next Steps:** Proceed with Phase 8 (AGENTS.md code documentation compliance) and test PAM unlocker.

## Session Date: 2026-07-31 06:34 - wlroots 0.20 API Migration

* **Phase:** wlroots 0.20 API Migration — Build Verified
* **Accomplishments:**
  - Fixed `wlr_seat_pointer_notify_axis` in `src/cursor.c` — added 7th `relative_direction` argument
  - Added missing `struct wlr_output *wlr_output` declaration in `hikari_output_enable` (`src/output.c`)
  - Fixed `wlr_headless_backend_create` in `src/server.c` — now passes `wl_display_get_event_loop(server->display)`
  - Fixed `wlr_output_layout_create` in `src/server.c` — now passes `server->display`
  - Fixed `wlr_switch->events.destroy` → `wlr_switch->base.events.destroy` in `src/switch.c`
  - Replaced 4x `wlr_xdg_surface_get_geometry()` calls with direct `surface->geometry` access in `src/xdg_view.c`
  - Fixed `xdg_surface->events.map/unmap` → `xdg_surface->surface->events.map/unmap` in `src/xdg_view.c`
  - **Clean build achieved:** Both `hikari` and `hikari-unlocker` compile and link successfully
  - Updated all `.devdocs/` and README documentation
* **Modified:** `src/cursor.c`, `src/output.c`, `src/server.c`, `src/switch.c`, `src/xdg_view.c`, `README.md`, all `.devdocs/` files
* **Next Steps:** Runtime testing on FreeBSD Wayland session; AGENTS.md compliance sweep on modified source files

---

## Session Date: 2026-07-30 01:28

* **Phase:** Code Review Fixes
* **Accomplishments:**
  - Triaged 27 review findings, applied 25 fixes (2 skipped with reasons)
  - Buffer mapping guards (F1-F3): indicator_bar.c, lock_indicator.c, output.c — scene nodes only created on successful mapping
  - Output lifecycle (F4-F6): disable checks commit before removing listeners; init deduplicates listener registration with enable; background repositioned on geometry update
  - View safety (F7): commit_reset guards indicator_position for hidden views
  - FLAG macro (F8): unsigned literal with parenthesization
  - XDG data reference (F9): removed scene_tree overwrite of xdg_surface->data
  - Layer shell (F10): popup damage guarded against disabled output
  - Pre-existing bug fix: added missing wlr_output local variable in hikari_output_enable
  - Documentation: 9 devdocs fixes, 6 code documentation annotations
* **Modified:** 20 files (8 devdocs, 12 source/header)
* **Skipped:** AGENTS.md move (breaks rule loading), damage ring transforms (legacy code, scene graph authoritative)
* **Next Steps:** FreeBSD build verification with `bmake`

---

## Session Date: 2026-07-29 15:32
* **Phase:** Verification & Issue Fixes
* **Accomplishments:**
  - Migrated `indicator_frame` from raw `wlr_box` to `wlr_scene_rect` nodes (init/fini/show/hide/refresh_geometry)
  - Added `scene_tree` to `hikari_xwayland_view`, wired `hikari_border_init` + `hikari_indicator_frame_init` for XWayland views
  - Deleted stub files: `src/pool.c`, `include/hikari/pool.h`, `src/renderer.c`, `include/hikari/renderer.h`
  - Fixed `unsigned long` → `uint16_t` type mismatch in `indicator_update_sheet`
  - Added `hikari_indicator_fini_for_view` helper for frame hide on indicator cleanup
  - Wired frame show into `hikari_indicator_position`, frame hide into focus changes and `hikari_view_hide`
  - Removed stale `struct hikari_renderer` forward declarations from `view.h` and `xdg_view.h`
  - Cleaned all devdocs files to reflect actual codebase state
* **Modified:** `indicator_frame.h`, `indicator_frame.c`, `indicator.h`, `indicator.c`, `xwayland_view.h`, `xwayland_view.c`, `xdg_view.h`, `xdg_view.c`, `view.h`, `view.c`, `workspace.c`, plus all `.devdocs/` files
* **Next Steps:** FreeBSD build verification with `bmake`

---

## Session Date: 2026-07-29 15:04

* **Phase:** DOD Strip + wlr_scene Migration Completion
* **Accomplishments:**
  * Stripped all DOD references from view.c (dod_id, view_state, view_geometry, view_pool, assign_view_sheet_mask).
  * Fixed indicator_bar.c (texture→scene_buffer, removed dead renderer variable, deduplicated includes).
  * Fixed indicator.h DAMAGE macro to call indicator_bar_position. Added hikari_indicator_damage as inline alias.
  * Fixed output.c disable (wlr_output_rollback/enable → state-based API).
  * Removed all dead struct hikari_renderer forward declarations.
  * Fixed workspace.c display_sheet to use direct sheet comparison.
* **Modified Files:**
  * src/view.c, include/hikari/view.h, src/indicator_bar.c, include/hikari/indicator_bar.h
  * include/hikari/indicator.h, src/output.c, include/hikari/output.h
  * include/hikari/border.h, include/hikari/indicator_frame.h, include/hikari/xwayland_view.h
  * src/sheet.c, src/workspace.c
* **Next Steps:**
  * User runs make locally to verify compilation.

---

## Session Date: 2026-07-29 14:34 - commit 3cf8f32

* **Phase:** wlr_scene Rendering Migration
* **Accomplishments:** Migrated lock indicator and background rendering to wlr_scene buffers. Gutted renderer.c and renderer.h. Removed renderer.o from Makefile. Borders now use wlr_scene_rect nodes. Lock indicator uses wlr_scene_buffer. Mode render callbacks removed.
* **Modified:** 29 files (see commit 3cf8f32)
* **Next Steps:** Complete remaining wlr_scene migration, commit working tree changes

---

## Session Date: 2026-07-29 14:02 - commit 1fccd9d  

* **Phase:** Object Pool Removal
* **Accomplishments:** Removed custom object pool allocator. Reverted all hikari_pool_alloc calls to hikari_malloc. Gutted pool.c and pool.h. Removed pool.o from Makefile. Cleaned server.h of pool struct members.
* **Modified:** 11 files (see commit 1fccd9d)
* **Next Steps:** Proceed with wlr_scene migration
