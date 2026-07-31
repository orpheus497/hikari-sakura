# Hikari Project Briefing

*Last Updated:* 2026-07-31 15:15

## Current Status

- **Phase:** Phase 11 — Startup Wiring Deep Investigation
- **Branch:** wlroots-0.17.1
- **Overall progress:** 99% (startup fixes applied; awaiting FreeBSD build validation)
- **Target OS:** FreeBSD 13.x/14.x+
- **Current step:** Verifying 7 bug fixes related to seatd/session, output mode, D-Bus, and PAM unlocker.

## Session Briefing

### Accomplishments (this session)
- Consolidated devdocs (merged SUMMARIES.md, TESTS.md, and reources.md into AGENTS.md compliant structure).
- Synchronized all devdocs timestamps and phase statuses to Phase 11.
- Analyzed startup wiring and fixed 5 bugs (session double-free, output mode, desktop file, stat stderr, Makefile install).

### Blockers
- Build validation is temporarily blocked by a local terminal environment error.
- BUG-6 non-blocking PAM I/O requires a future architectural change.

### Recent Decisions
- Devdocs structure explicitly enforces 7 core files. Extraneous files merged and deprecated.
- Session is not separately destroyed; backend owns it.
- Output mode selection uses EDID-preferred mode instead of the first available.

### Next Steps
1. Perform build validation of the compositor on FreeBSD.
2. Runtime testing on a Wayland session.
3. Test PAM unlocker (`hikari-unlocker`).

## What Works

- FreeBSD evdev headers done.
- Standard `hikari_malloc` allocation for compositor allocation paths.
- `wlr_scene` rendering for borders (`wlr_scene_rect`), lock indicator (`wlr_scene_buffer`), backgrounds (`wlr_scene_buffer`), indicator bars (`wlr_scene_buffer`), and indicator frames (`wlr_scene_rect`).
- XDG views and XWayland views both have `scene_tree` with border and indicator frame nodes.
- Makefile targets wlroots-0.20 via pkg-config.
- Stub files (`pool.c`, `pool.h`, `renderer.c`, `renderer.h`) deleted.
- AGENTS.md code documentation compliance prefixes added to modified source files (`cursor.c`, `output.c`, `server.c`, `switch.c`, `xdg_view.c`).
- **Clean build:** Both `hikari` and `hikari-unlocker` compile and link successfully against wlroots 0.20.

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
- `start-hikari.sh` uses absolute path with `./` fallback.
- **wlroots 0.20 initial_commit lifecycle:** commit listener moved from `map()` to `hikari_xdg_view_init()` (new_toplevel time); added `initial_commit` handler calling `wlr_xdg_toplevel_set_size(0,0)` to set `initialized = true`; added popup `initial_commit` handler; guarded `request_fullscreen_handler` with `initialized` check (`src/xdg_view.c`)

## What Was Removed

- Object pool allocator.
- Custom renderer pipeline.
- DOD SoA tables.
- All `struct hikari_renderer` forward declarations.

## Remaining Work

- Runtime testing on FreeBSD Wayland session (crash fix applied — needs revalidation).
- PAM unlocker verification.
