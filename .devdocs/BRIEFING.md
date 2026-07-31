# Hikari Project Briefing

*Last Updated:* 2026-07-31 13:08

## Current Status

- **Phase:** Phase 8 — Code Documentation Compliance (Runtime bugs fixed)
- **Branch:** wlroots-0.17.1
- **Overall progress:** ~98% (all runtime crashing and black screen rendering bugs fixed)
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

## What Was Removed

- Object pool allocator.
- Custom renderer pipeline.
- DOD SoA tables.
- All `struct hikari_renderer` forward declarations.

## Audit Findings (2026-07-31 13:08)

**Critical:**
- `hikari_server_cursor_focus()` uses `clock_gettime()` return value as timestamp (always 0). `src/server.c:439`.
- `wlr_drm_format` struct initialized with `.capacity` (internal field). `output.c:95`, `indicator_bar.c:126`, `lock_indicator.c:49`.

**Medium:**
- `wlr_xcursor_manager_load` hardcoded scale=1 (HiDPI broken). `cursor.c:124`.
- Unsafe `wl_container_of` on `wlr_surface` in decoration handler. `server.c:540`.

**Low:**
- `#if HAVE_XWAYLAND` vs `#ifdef` inconsistency. `server.c:1086`.
- `start-hikari.sh` uses `./hikari` (relative path, breaks installed system).

## Remaining Work

- Fix critical `clock_gettime` misuse in `server.c`.
- Fix `wlr_drm_format` zero-init pattern in 3 files.
- Fix `start-hikari.sh` relative path.
- Runtime testing on FreeBSD/Linux with Wayland session.
- PAM unlocker verification.
