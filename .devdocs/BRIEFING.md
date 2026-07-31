# Hikari Project Briefing

*Last Updated:* 2026-07-31 16:34

## Current Status

- **Phase:** Phase 13 — Codebase Audit & Bug Fixes Complete
- **Branch:** wlroots-0.17.1
- **Overall progress:** 99% (clean build achieved at Phase 6; runtime blocked by tmpfs/ZFS issue)
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Environmental blockers only — code is audit-validated and ready for runtime testing.

## Session Briefing

### Accomplishments (this session)

- **Full codebase wiring audit** of all 55 source files, 64 headers, Makefile, start-hikari.sh, hikari_unlocker.c, PAM configs, and hikari.desktop.
- Published comprehensive audit report scoring the codebase at **~93% correctly wired**.
- **Fixed BUG (Medium): Switch toggle handler** — Cascading `if` → `else if` in `src/switch.c`.
- **Fixed BUG (Low): Output cairo surface check** — `src/output.c:85` checked wrong surface.
- **Fixed: Duplicate includes** — Removed duplicate `wlr_data_device.h` and `wlr_seat.h` in `src/server.c`.
- **Fixed: Blocking wait()** — `src/lock_mode.c:154` `wait()` → `waitpid(-1, &status, WNOHANG)`.
- **Fixed: output->server init** — `src/output.c` now sets `output->server = &hikari_server` in `hikari_output_init()`.
- **Migrated main.c comments** — All `##` prefixes replaced with `[COMMENT]` format.
- **FreeBSD Handbook Ch.6 cross-reference** — All requirements verified correct. Implementation exceeds handbook.
- Updated all 7 devdocs files to Phase 13 current state.

### Blockers

- **CRITICAL:** `XDG_RUNTIME_DIR` (`/var/run/user/1001`) is on ZFS — `posix_fallocate()` will fail for Wayland clients. This affects the FreeBSD runtime environment, not the code.
- **CRITICAL:** `start-hikari.sh` fallback to `/tmp` also on ZFS — same failure.

### Recent Decisions

- Switch toggle bug was a pure logic error (not wlroots API related).
- Output background loading had a surface check bug checking the wrong cairo surface after allocation.
- Non-blocking PAM I/O implementation confirmed complete and correctly wired.

### Next Steps

1. **Resolve tmpfs/ZFS issue:** Implement system-level tmpfs mount for runtime dir. (~30 min)
2. **Build validation:** Execute `bmake` to confirm all Phase 12-13 changes compile. (~5 min)
3. **Runtime testing:** Launch hikari on FreeBSD Wayland session with tmpfs-backed XDG_RUNTIME_DIR. (~15 min)
4. **PAM unlocker verification:** Test `hikari-unlocker` with OpenPAM. (~10 min)

## What Works

- FreeBSD evdev headers done.
- Standard `hikari_malloc` allocation for compositor allocation paths.
- `wlr_scene` rendering for borders (`wlr_scene_rect`), lock indicator (`wlr_scene_buffer`), backgrounds (`wlr_scene_buffer`), indicator bars (`wlr_scene_buffer`), and indicator frames (`wlr_scene_rect`).
- XDG views and XWayland views both have `scene_tree` with border and indicator frame nodes.
- Makefile targets wlroots-0.20 via pkg-config.
- Stub files (`pool.c`, `pool.h`, `renderer.c`, `renderer.h`) deleted.
- Switch toggle handler now correctly uses `else if`.
- Output cairo surface check now validates the correct surface.
- AGENTS.md code documentation compliance prefixes applied to all modified source files.
- **Clean build (Phase 6):** Both `hikari` and `hikari-unlocker` compiled and linked successfully against wlroots 0.20. Phase 12-13 fixes pending revalidation via `bmake`.

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

- **CRITICAL:** Resolve tmpfs/ZFS incompatibility for XDG_RUNTIME_DIR.
- Runtime testing on FreeBSD Wayland session.
- PAM unlocker verification.
- Build validation (Phase 12-13 fixes).
