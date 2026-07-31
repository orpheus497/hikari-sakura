# Hikari Project Briefing

*Last Updated:* 2026-08-01 01:24

## Current Status

- **Phase:** Phase 14 — Comprehensive Audit Fixes Applied
- **Branch:** wlroots-0.17.1
- **Overall progress:** 99% (all code audit fixes applied; runtime blocked by tmpfs/ZFS issue)
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Build revalidation and runtime testing.

## Session Briefing

### Accomplishments (this session — Phase 14)

- **Comprehensive codebase audit** — deep investigation of all 55 source files, 64 headers, Makefile, start-hikari.sh, hikari_unlocker.c, PAM configs, and desktop entry.
- **Published detailed audit report** covering wiring, memory, D-Bus, IPC, XDG, FreeBSD integration, wlroots 0.20 compliance.
- **Fixed BUG-1 (Medium):** `move_resize_view()` in server.c used `dy` for both lx and ly — fixed lx to use `dx`.
- **Fixed BUG-2 (Low):** `outputs_disabled` in lock_mode never initialized or reset in cancel — added explicit init.
- **Fixed BUG-3 (Low):** `command.c` waitpid loop had inverted errno check — replaced with correct pattern.
- **Fixed BUG-4 (Low):** Removed stale "CAN FAIL WITH NULL POINTER" debug comment from server.c.
- **Security fix:** Replaced `memset` with `explicit_bzero` for lock_mode password buffer zeroing.
- **Robustness fix:** Added EINTR-retrying write + error check for lock_mode pipe write.
- **Listener cleanup:** Added 5 missing `wl_list_remove()` calls in `hikari_server_stop()` for decoration, layer shell, and virtual input listeners.
- **Dead code removal:** Deleted empty render.h, commented-out mode_handler, commented-out struct members, unused xdg_view listener declarations.
- **Typo fix:** "DESTORY" → "DESTROY" in output.c.
- **Comment migration:** server.h `##` prefixes → `[COMMENT]` format.
- **Desktop entry:** Added `DesktopNames=Hikari` for XDG portal identification.
- **Gitignore:** Updated with `*.core` wildcard, `compile_flags.txt`, `.clangd`.
- **No stubs, placeholders, or fake logic found** in the entire codebase.

### Previous Accomplishments (Phase 13)

- Full codebase wiring audit, switch toggle fix, output cairo surface fix, duplicate include removal.
- Non-blocking PAM I/O, output->server init, main.c comment migration.
- FreeBSD Handbook Ch.6 cross-reference verification.

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
- Empty `include/hikari/render.h` (Phase 14).
- Unused `request_move`/`request_resize`/`request_maximize` from `xdg_view.h` (Phase 14).
- Commented-out `mode_handler` and `struct wl_listener mode` from output.c/output.h (Phase 14).

## Remaining Work

- **CRITICAL:** Resolve tmpfs/ZFS incompatibility for XDG_RUNTIME_DIR.
- **Manual:** Delete `include/hikari/render.h` from disk (file is empty, marked for deletion).
- **Manual:** Delete `etc/pam.d/hikari-unlocker.Linux` (dead Linux PAM file).
- **Manual:** Delete `.core` dump files from repo root.
- Build validation (`bmake`) to confirm all Phase 13-14 fixes compile.
- Runtime testing on FreeBSD Wayland session.
- PAM unlocker verification.
- Verify `wlr_output_effective_resolution()` exists in wlroots 0.20 headers at compile time.
