# Hikari Project Briefing

*Last Updated:* 2026-07-31 15:53

## Current Status

- **Phase:** Phase 12 — XDG/tmpfs/ZFS Resolution & Runtime Validation
- **Branch:** wlroots-0.17.1
- **Overall progress:** 99% (clean build achieved at Phase 6; runtime blocked by tmpfs/ZFS issue)
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Resolve XDG_RUNTIME_DIR tmpfs/ZFS incompatibility before runtime validation.

## Session Briefing

### Accomplishments (this session)

- Deep online research into XDG_RUNTIME_DIR/tmpfs/ZFS compatibility on FreeBSD.
- Confirmed system state: FreeBSD 15.1-RELEASE, full ZFS root (`zroot`), `/var/run/user/1001` on ZFS, `/tmp` on ZFS (`zroot/tmp`), wlroots 0.20.2 installed via pkg.
- Discovered critical blocker: `posix_fallocate()` returns `EINVAL` on ZFS — all Wayland shared memory operations will fail.
- Verified `pam_xdg.so` is active in `/etc/pam.d/system` — creates `/var/run/user/1001` but does NOT mount tmpfs.
- Read every file in the codebase (120+ source files, headers, configs, devdocs).
- Confirmed all 13+ wlroots 0.20 API breaking changes are resolved.
- Confirmed `start-hikari.sh` fallback to `/tmp/hikari-runtime-$UID` also fails because `/tmp` is ZFS.
- Produced comprehensive research report with 4 solution options.

### Blockers

- **CRITICAL:** `XDG_RUNTIME_DIR` (`/var/run/user/1001`) is on ZFS — `posix_fallocate()` will fail for Wayland clients. This affects the FreeBSD build runtime environment, not the compilation itself.
- **CRITICAL:** `start-hikari.sh` fallback to `/tmp` also on ZFS — same failure.
- BUG-6 non-blocking PAM I/O requires a future architectural change.

### Recent Decisions

- Research confirmed: XDG on tmpfs with ZFS needs to be re-addressed — cannot rely on current system configuration.
- wlroots 0.20.2 installation verified correct via pkg-config, library, and headers.
- Four solution options identified (tmpfs on `/var/run/user` recommended).

### Next Steps

1. **Resolve tmpfs/ZFS issue:** Implement system-level tmpfs mount or update `start-hikari.sh` to detect ZFS and use a tmpfs-backed path. (~30 min)
2. **Build validation:** Execute `bmake` to confirm Phase 11 changes compile. (~5 min)
3. **Runtime testing:** Launch hikari on FreeBSD Wayland session with tmpfs-backed XDG_RUNTIME_DIR. (~15 min)
4. **PAM unlocker verification:** Test `hikari-unlocker` with OpenPAM. (~10 min)

## What Works

- FreeBSD evdev headers done.
- Standard `hikari_malloc` allocation for compositor allocation paths.
- `wlr_scene` rendering for borders (`wlr_scene_rect`), lock indicator (`wlr_scene_buffer`), backgrounds (`wlr_scene_buffer`), indicator bars (`wlr_scene_buffer`), and indicator frames (`wlr_scene_rect`).
- XDG views and XWayland views both have `scene_tree` with border and indicator frame nodes.
- Makefile targets wlroots-0.20 via pkg-config.
- Stub files (`pool.c`, `pool.h`, `renderer.c`, `renderer.h`) deleted.
- AGENTS.md code documentation compliance prefixes added to modified source files (`cursor.c`, `output.c`, `server.c`, `switch.c`, `xdg_view.c`).
- **Clean build (Phase 6):** Both `hikari` and `hikari-unlocker` compiled and linked successfully against wlroots 0.20. Phase 11 fixes pending revalidation via `bmake`.

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
- Build validation (Phase 11 fixes).
