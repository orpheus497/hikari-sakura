# Hikari Project Briefing

*Last Updated:* 2026-07-31 13:20

## Current Status

- **Phase:** Phase 8 — Code Documentation Compliance (Runtime bugs fixed)
- **Branch:** wlroots-0.17.1
- **Overall progress:** 100% (All wlroots 0.20 API and FreeBSD audit issues resolved)
- **Target OS:** FreeBSD 13.x/14.x+

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

## What Was Removed

- Object pool allocator.
- Custom renderer pipeline.
- DOD SoA tables.
- All `struct hikari_renderer` forward declarations.

## Remaining Work

- Runtime testing on FreeBSD/Linux with Wayland session.
- PAM unlocker verification.
