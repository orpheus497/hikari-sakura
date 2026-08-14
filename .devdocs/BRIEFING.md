# Hikari Project Briefing

*Last Updated:* 2026-08-13 19:08

## Current Status

- **Phase:** Phase 26 — Phase 24 hardening backlog completed (7/7): P2 CSD granular damage (`src/view.c`), P2 fail-fast allocation policy (`src/memory.c`, `include/hikari/memory.h`), P3 changelog `wloots` typos (`CHANGELOG.md`). TC-BUILD-01/02 pass, 0 errors; edited files warning-clean.
- **Branch:** wlroots-0.17.1 (stale label — tree builds against installed wlroots 0.20.x)
- **Overall progress:** Phase 24 hardening stream fully closed (P0/P1 in Phase 25, P2/P3 in Phase 26) — the agent-side code backlog is clear. Remaining queue is runtime/environmental (user-run Phase 19 diagnostics; eDP-1 swapchain below hikari; tmpfs/ZFS `XDG_RUNTIME_DIR`), runtime-blocked verifications (P2-14, PAM, layer-client spot check), and optional hygiene (TC-FORMAT-01, comment-header rollout, cosmetic enum-compare warnings).
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Await the user-run Phase 19 diagnostics matrix (eDP-1 swapchain) and tmpfs/ZFS resolution; agent-side next actions (TC-FORMAT-01, optional comment-header rollout, cosmetic enum-compare warnings) pending user direction.
- **Blockers:** (1) eDP-1 scanout swapchain test failure — Mesa/EGL/GBM ↔ drm-kmod layer; (2) tmpfs/ZFS `XDG_RUNTIME_DIR` — client wl_shm, escalated because Error 1 kills dmabuf feedback and forces clients onto shm.

## Session Briefing

### Latest Session (Phase 26 — Phase 24 Hardening Backlog Completed)

- **P2 (CSD damage granularity):** both TODO-marked whole-output fallbacks removed — `hikari_view_damage_whole` and `hikari_view_damage_surface` (`src/view.c`) now compute granular per-surface boxes for CSD exactly as for SSD; the CSD main surface is damaged by its buffer extents (client decorations/shadows live inside the client buffer) rather than the absent server border box.
- **P2 (allocation policy — decision resolved fail-fast):** `hikari_malloc`/`hikari_calloc` (`src/memory.c`) emit a sized `error:` diagnostic and `abort()` on failure — NULL can never reach the dozens of unchecked callsites. `abort()` (not `exit()`) yields SIGABRT/core dump and skips atexit on a half-valid heap; the never-NULL contract is documented in `include/hikari/memory.h`.
- **P3 (docs hygiene):** `CHANGELOG.md` `wloots` → `wlroots` (2 sites).
- **Documentation:** `src/memory.c` and `include/hikari/memory.h` gained the AGENTS.md-mandated comment headers; edited `src/view.c` functions annotated.
- **Validation:** TC-BUILD-01 (default) and TC-BUILD-02 (full-feature) clean builds pass with 0 errors under `env -u DEBUG`; edited files warning-clean (pre-existing `xwayland_unmanaged_view.c` unused-function warnings unchanged).

### Previous Session (Phase 25 — Phase 24 Hardening P0/P1 Batch Executed)

- **P0 (config strictness):** unknown `outputs` keys now fail the parse (`goto done` in `parse_output_config`, `src/configuration.c`) — previously they logged but the configuration loaded successfully, silently ignoring typo'd rules.
- **P1 (resource lifecycle):** `parse_switches` now frees its UCL iterator at the `done:` label (`src/configuration.c`) — fixes a per-load/SIGHUP-reload leak; matches all sibling parsers.
- **P1 (lock helper semantics):** the child path after a failed `execl("hikari-unlocker")` emits `error: could not execute hikari-unlocker` on stderr and `_exit(EXIT_FAILURE)`s (`src/lock_mode.c`) — no more `exit(0)` masking the failure.
- **P1 (output failure observability):** the failed initial modeset commit in `hikari_output_init` now prints `error: failed to commit initial mode for output "<name>"` before the early return (`src/output.c`); `<stdio.h>` added explicitly.
- **Validation:** TC-BUILD-01 (default) and TC-BUILD-02 (full-feature) clean builds pass with 0 errors under `env -u DEBUG`; the three edited files compile warning-clean. Pre-existing documented warnings (enum-compare cosmetic TODO, xwayland_unmanaged unused handlers) unchanged.

### Prior Sessions (Phases 1–24)

- **Phase 24 (Deep Wiring Audit Capture into Devdocs):** exhaustive static audit ingested into devdocs — no simulated/fake subsystem wiring found in core compositor paths; modal no-op handlers classified as intentional input suppression; 6-item remediation backlog captured (items 1–4 remediated in Phase 25, items 5–6 in Phase 26); changelog `wloots` drift noted (fixed in Phase 26).

- **Phase 23 (Review-Findings Verification & Remediation):** verify-first pass over 10 review findings — 6 still-valid fixed (version.h atomic regeneration, strtol end-pointer validation, flat `base->geometry` popup offsets, xwayland scene-tree NULL bailout, wallpaper PNG restored + unconditional install, 17 function-purpose comments); 4 verified stale and skipped with evidence. Default + full-feature clean builds pass.
- **Phase 22 (Devdocs Consolidation):** restored the AGENTS.md 7-file structure; redistributed the archived investigation (launcher architecture → BLUEPRINT §6, corrected eDP-1 analysis → BLUEPRINT §5, P2-14 → TODOS, P2-15 → BLUEPRINT known limitations); corrected the factually-wrong Phase-20 §5 draft against live evidence.
- **Phase 21 (Validity Audit & Launcher Analysis):** answered "why `start-hikari` *and* `hikari`?" with file:line evidence — duality confirmed as deliberate architecture; 2026-07-31 12:47 `setup_env()` revert re-affirmed. Consolidated into BLUEPRINT §5/§6 in Phase 22.
- **Phase 20 (Codebase Audit):** Exhaustive read-only audit of `src/` and `include/`; system architecture synthesized directly into `.devdocs/BLUEPRINT.md`.
- **Phase 19 (Runtime Triage):** Live TTY runtime test executed. Error 1: `eglQueryDeviceStringEXT` fails (non-fatal, kills dmabuf feedback). Error 2: `Swapchain for output 'eDP-1' failed test` (fatal to output). Verified that seatd, backend start, renderer, allocator, and connector probe work correctly.
- Canonical history lives in `.devdocs/SESSION_HANDOFF.md` (reverse-chronological); the Phase 18 defect catalog (4 P0 + 3 P1 + 8 P2, all remediated; TC-BUILD-01/02 pass) is recorded in the Phase 18/18b ledger entries there and in DECISIONS_LOG. Not duplicated here.

## wlroots 0.20 API Fixes Applied

- `wlr_seat_pointer_notify_axis` — added 7th `relative_direction` argument (`src/cursor.c`)
- `wlr_headless_backend_create` — now takes `wl_event_loop *` via `wl_display_get_event_loop()` (`src/server.c`)
- `wlr_output_layout_create` — now requires `wl_display *` argument (`src/server.c`)
- `wlr_switch->events.destroy` — moved to `wlr_switch->base.events.destroy` (`src/switch.c`)
- `wlr_xdg_surface_get_geometry()` — removed; replaced with direct `surface->geometry` access (`src/xdg_view.c`, 4 sites)
- `xdg_surface->events.map/unmap` — moved to `xdg_surface->surface->events.map/unmap` (`src/xdg_view.c`)
- Missing `wlr_output` variable declaration in `hikari_output_enable` (`src/output.c`)
- `clock_gettime` return value misuse fixed (`src/server.c`)
- `wlr_drm_format` `.capacity` field access removed, using zero-init (`src/output.c`, `src/indicator_bar.c`, `src/lock_indicator.c`)
- `wlr_xcursor_manager_load` hardcoded scale fixed (`src/cursor.c`)
- Unsafe `wl_container_of` on `wlr_surface` replaced with `wlr_xdg_surface_try_from_wlr_surface` (`src/server.c`)
- `#if HAVE_XWAYLAND` inconsistency fixed (`src/server.c`)
- `start-hikari.sh` resolves hikari via SCRIPT_DIR sibling, PATH, then `./hikari` fallback.
- **wlroots 0.20 initial_commit lifecycle:** commit listener moved from `map()` to `hikari_xdg_view_init()` (new_toplevel time); added `initial_commit` handler calling `wlr_xdg_toplevel_set_size(0,0)` to set `initialized = true`; added popup `initial_commit` handler; guarded `request_fullscreen_handler` with `initialized` check (`src/xdg_view.c`)

## What Was Removed

- Object pool allocator.
- Custom renderer pipeline.
- DOD SoA tables.
- All `struct hikari_renderer` forward declarations.
- Empty `include/hikari/render.h` (Phase 14 — deleted).
- Unused `request_move`/`request_resize`/`request_maximize` from `xdg_view.h` (Phase 14).
- Commented-out `mode_handler` and `struct wl_listener mode` from output.c/output.h (Phase 14).
- Linux PAM file `etc/pam.d/hikari-unlocker.Linux` (deleted — project is FreeBSD-only).
- glibc-specific `_GNU_SOURCE`/`_DEFAULT_SOURCE` defines and redundant `explicit_bzero` prototype from `hikari_unlocker.c`.

## Remaining Work

- **CRITICAL:** Resolve the eDP-1 scanout swapchain failure (diagnostics → fix; expected in the Mesa/GBM/drm-kmod layer, not this tree). The Phase 25 output-commit diagnostic now names the failed output on stderr.
- **CRITICAL:** Resolve tmpfs/ZFS incompatibility for XDG_RUNTIME_DIR (client wl_shm; escalated — Error 1 removed dmabuf feedback, forcing clients onto shm).
- PAM unlocker live verification (setuid 4555 path; blocked on runtime bring-up).
- P2-14 `current_mode` retention across output disable/enable (blocked on runtime bring-up).
- Layer-client spot check (blocked on runtime bring-up).
- TC-FORMAT-01 `clang-format` compliance run.
- Optional hygiene (pending user direction): comment-header rollout to non-compliant `src/` files; cosmetic enum-compare warnings (`src/dnd_mode.c:63`, `src/move_mode.c:78`).

*Granular tasks: `.devdocs/TODOS.md`. Closed/stale items previously listed here (`.core` cleanup, `wlr_output_effective_resolution` check, runtime test) were resolved 2026-08-13. The Phase 24 hardening stream is closed at 7/7: unknown-`outputs`-key strict fail, `parse_switches` iterator free, lock-helper exec semantics, output-commit diagnostic (Phase 25); CSD granular damage, fail-fast allocation policy, changelog typos (Phase 26).*
