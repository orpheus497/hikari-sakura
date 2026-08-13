# Hikari Project Briefing

*Last Updated:* 2026-08-13 17:08

## Current Status

- **Phase:** Phase 24 — Deep architecture/wiring audit recorded into devdocs (docs-only): startup/output/input/mode/config/session integration re-verified; no fake subsystems found; six actionable code-quality/risk items captured with a remediation plan.
- **Branch:** wlroots-0.17.1 (stale label — tree builds against installed wlroots 0.20.x)
- **Overall progress:** Devdocs consolidated with zero repetition: launcher/session architecture → BLUEPRINT §6; corrected eDP-1 failure analysis → BLUEPRINT §5 (Phase-20 draft had misattributed the failure to `wlr_backend_start`; live evidence places it at the output-commit swapchain test, `src/output.c:350`); P2-14 → TODOS; P2-15 → BLUEPRINT known limitations. All claims re-verified against the codebase. Runtime blocker remains below hikari in the Mesa/EGL/GBM ↔ drm-kmod layer.
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Execute the newly added Phase 24 code-hardening backlog (unknown-output-key strict fail, lock-helper exec failure semantics, switches parser iterator cleanup, output-commit loud diagnostics, CSD damage granularity, allocation-policy hardening) while keeping the Phase 19 runtime diagnostics queue active.
- **Blockers:** (1) eDP-1 scanout swapchain test failure — Mesa/EGL/GBM ↔ drm-kmod layer; (2) tmpfs/ZFS `XDG_RUNTIME_DIR` — client wl_shm, escalated because Error 1 kills dmabuf feedback and forces clients onto shm.

## Session Briefing

### Latest Session (Phase 23 — Review-Findings Verification & Remediation)

- **Verify-first pass over 10 review findings:** 6 still-valid fixed (see PROGRESS Phase 23); 4 skipped as stale — no future-dated timestamps exist (system clock 16:50 > all stamps), `INVESTIGATION_RUNTIME_FAILURE.md` retired in Phase 22, PLANS/BRIEFING API-verification duplicates already removed, SESSION_HANDOFF line references stale (records are past-dated, sequential, provenance-noted).
- **Code changes:** version.h regenerates every build via temp file + atomic rename; numeric mouse bindings reject junk via strtol end pointer; layer popup damage offsets use flat `base->geometry`; XWayland init bails cleanly if `wlr_scene_tree_create` fails; wallpaper PNG (1920x1080 8-bit, `0x282C34` gradient) now exists and installs unconditionally; 17 function-purpose comment headers added.
- **Validation:** default + full-feature clean builds pass with 0 errors; wallpaper install rehearsed. (Shell exports `DEBUG=release`, which arms the Makefile's `-Werror` DEBUG branch and trips the pre-existing documented enum-compare cosmetic item — unrelated to these changes.)

### Current Session (Phase 24 — Deep Wiring Audit Capture into Devdocs)

- **Scope covered:** deep static audit of docs + code wiring across startup lifecycle, output/scene paths, input/mode dispatch, config/action parser, FreeBSD launcher/PAM/unlocker/session integration.
- **Implementation verdict:** no simulated/fake subsystem wiring found in core compositor paths; all major components are concretely wired.
- **Intentional no-op verdict:** empty callbacks concentrated in mode handlers are predominantly intentional input-suppression hooks (modal behavior), not missing implementations.
- **Actionable findings captured:** (1) unknown `outputs` keys log but do not fail parse; (2) `parse_switches` lacks iterator free; (3) lock helper child uses `exit(0)` after failed `execl`; (4) output commit failure path can remain too quiet; (5) two TODO-tagged CSD damage paths still over-damage whole outputs; (6) allocation wrappers are pass-through with many unchecked callers.
- **Documentation drift captured:** `CHANGELOG.md` contains `wloots` typo entries; build/docs otherwise consistently target wlroots 0.20.

### Previous Session (Phase 22 — Devdocs Consolidation)

- **Consolidated to the AGENTS.md 7-file structure:** archived runtime investigation content was redistributed (the Phase-20 analysis artifact had already been merged away). Still-valid content redistributed with zero repetition — launcher/session architecture → BLUEPRINT §6; corrected eDP-1 failure analysis → BLUEPRINT §5; P2-14 → TODOS active list; P2-15 → BLUEPRINT known limitations; the fixed-defect catalog remains in the Phase 18/18b ledger entries.
- **Codebase verification pass:** mlock/munlock (`src/lock_mode.c:522/542`), double-fork+setsid exec (`src/command.c:14-21`), layer-shell exclusive zones (`src/layer_shell.c:88-172`), 26-mark registry (`src/mark.c:10-50`), sheet array (`include/hikari/workspace.h:22`) — all BLUEPRINT claims confirmed; the Phase-20 §5 draft was found factually wrong and corrected.
- **Dangling references fixed** in all living trackers; historical ledger entries keep their as-written context with the supersession declared in the Phase 22 entries.

### Prior Sessions (Phases 1–21)

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

- **CRITICAL:** Resolve the eDP-1 scanout swapchain failure (diagnostics → fix; expected in the Mesa/GBM/drm-kmod layer, not this tree).
- **CRITICAL:** Resolve tmpfs/ZFS incompatibility for XDG_RUNTIME_DIR (client wl_shm; escalated — Error 1 removed dmabuf feedback, forcing clients onto shm).
- **HIGH:** Enforce strict parse failure for unknown `outputs` keys (`src/configuration.c`).
- **HIGH:** Fix lock helper exec-failure exit semantics in `src/lock_mode.c` child path.
- **MEDIUM:** Add explicit diagnostic on failed output modeset commit path (`src/output.c:350-353`).
- **MEDIUM:** Free `parse_switches` iterator in `src/configuration.c`.
- **MEDIUM:** Replace CSD whole-output damage fallback with granular damage in `src/view.c` TODO paths.
- **MEDIUM:** Decide and implement allocation failure policy (fail-fast wrappers or caller checks).
- PAM unlocker live verification (setuid 4555 path; blocked on runtime bring-up).
- Optional hardening: loud stderr diagnostic on the failed output-commit early return (`src/output.c:350-353`).

*Granular tasks: `.devdocs/TODOS.md`. Closed/stale items previously listed here (`.core` cleanup, `wlr_output_effective_resolution` check, runtime test) were resolved 2026-08-13.*
