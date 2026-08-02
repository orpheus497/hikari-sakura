# Hikari Project Briefing

*Last Updated:* 2026-08-02 13:25

## Current Status

- **Phase:** Phase 15 — Review Fix: start-hikari.sh Binary Resolution
- **Branch:** wlroots-0.17.1
- **Overall progress:** 99% (all code audit fixes applied; runtime blocked by tmpfs/ZFS issue)
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Build revalidation and runtime testing.

## Session Briefing

### Accomplishments (this session — Phase 15)

- **Verified review finding** against current code: binary resolution block in `start-hikari.sh` used `command -v hikari` (PATH) first and `./hikari` (CWD-relative) as fallback. The `./hikari` fallback was fragile — resolved relative to caller's working directory, not the script's location.
- **Applied fix:** Added `SCRIPT_DIR` derivation using `$(cd -- "$(dirname -- "$0")" && pwd)`. Changed resolution order to: `${SCRIPT_DIR}/hikari` (sibling) → PATH → `./hikari` (legacy edge case).
- **Documentation audit:** Updated all devdocs and README for accuracy, comprehensiveness, and currency.

### Previous Accomplishments (Phase 14)

- Comprehensive codebase audit — deep investigation of all 55 source files, 64 headers, Makefile, start-hikari.sh, hikari_unlocker.c, PAM configs, and desktop entry.
- Fixed BUG-1 (Medium): `move_resize_view()` dx/dy confusion in server.c.
- Fixed BUG-2 (Low): `outputs_disabled` stale state in lock_mode.
- Fixed BUG-3 (Low): `command.c` waitpid infinite loop.
- Fixed BUG-4 (Low): Removed stale debug comment from server.c.
- Security: `explicit_bzero` for password buffer. Robustness: EINTR-retrying pipe write.
- Added 5 missing listener cleanups. Removed dead code. Updated desktop entry and gitignore.

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

- **CRITICAL:** Resolve tmpfs/ZFS incompatibility for XDG_RUNTIME_DIR.
- **Manual:** Delete `.core` dump files from repo root.
- Build validation (`bmake`) to confirm all Phase 13-15 fixes compile.
- Runtime testing on FreeBSD Wayland session.
- PAM unlocker verification.
- Verify `wlr_output_effective_resolution()` exists in wlroots 0.20 headers at compile time (used in `src/layer_shell.c:177`).
