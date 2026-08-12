# Hikari Project Briefing

*Last Updated:* 2026-08-13 05:41 (environment clock, corroborated by build mtimes)

## Current Status

- **Phase:** Phase 18b — Remediation complete; awaiting live runtime test
- **Branch:** wlroots-0.17.1
- **Overall progress:** All 15 investigation defects + 3 build-discovered defects fixed and build-validated. TC-BUILD-01 (default) and TC-BUILD-02 (full-feature) pass from clean trees. See `.devdocs/INVESTIGATION_RUNTIME_FAILURE.md` §9 register.
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Live TTY runtime test via `start-hikari` (seatd up). Backend-start failure now produces a loud stderr diagnostic instead of a black zombie session. tmpfs/ZFS `XDG_RUNTIME_DIR` remains the environmental blocker for client shm.

## Session Briefing

### Accomplishments (this session — Phase 18 + 18b)

- **Investigation (Phase 18):** full static root-cause analysis of the post-login crash / black-screen+dead-input symptoms — 4 P0 + 3 P1 + 8 P2 defects with file:line evidence (report `.devdocs/INVESTIGATION_RUNTIME_FAILURE.md`).
- **Remediation (Phase 18b, user-approved):** all plan steps applied and annotated. Headliners: `xkb_keymap_new_from_names` restored; `wlr_backend_start()` now checked with a fatal diagnostic; `wlr_headless_backend_create(server->event_loop)` type corrected; default `etc/hikari/hikari.conf` created (parser-verified); layer-shell surfaces attached to the scene graph; xwayland 0.20 lifecycle (`associate`-deferred map/unmap, `xcb_size_hints_t`); popup geometry migrated to `popup->current.geometry`.
- **Build validation:** `make clean && make` — 0 errors. Full-feature build (XWAYLAND+LAYERSHELL+SCREENCOPY+GAMMACONTROL+VIRTUAL_INPUT) — 0 errors, clean link. First time the feature configurations have ever compiled in this tree.
- **Devdocs corrected:** TC-BUILD-01 re-established on evidence; prior "93–99% wired" claims superseded by the verified register.

### Previous Accomplishments (Phase 18 investigation)

- **User-reported symptoms:** post-login either (A) crash/fail, or (B) black screen with dead keypresses and frozen mouse.
- **Deep static investigation** of the full startup/render/input path (30+ files read end-to-end; remainder verified via call sites). Full report: `.devdocs/INVESTIGATION_RUNTIME_FAILURE.md`.
- **P0-1 (hallucinated API):** `src/keyboard_config.c:354` calls nonexistent `xkb_map_new_from_names` (real: `xkb_keymap_new_from_names`; removed in libxkbcommon ≥ 1.0) — clean build cannot link; deployed binary must predate the tree. TC-BUILD-01 "passed" claim untenable.
- **P0-2 (symptom-B machine):** `src/server.c:1054` discards `wlr_backend_start()` result — on backend failure the compositor runs headless-blind forever: no outputs, no input devices, no cursor. Black + dead input + frozen mouse, exactly.
- **P0-3 (hallucinated comment + wrong type):** `src/server.c:857` passes `server->display` to `wlr_headless_backend_create()` (API takes `wl_event_loop *`); adjacent comment asserts the opposite of the real API and contradicts this file's own Phase-4 fix record. UB on every launch.
- **P0-4 (phantom assets):** `etc/hikari/hikari.conf` and `share/backgrounds/hikari/hikari_wallpaper.png` are referenced by `make install`/`dist` but absent from the tree — installs abort mid-rule; missing config → exit at startup; empty config → zero bindings (dead keys).
- **P1:** xkb-file keymap type-tag lie (`keyboard_config.c:112`, union reinterpreted → crash/corruption); numeric mouse bindings parsed but never stored (`binding_config.c:136-148`); layer-shell surfaces never attached to the scene graph (`layer_shell.c` — entire shell renders nothing).
- **Verified-real (contrast):** server init order, first-output workspace migration, scene frame loop, XDG 0.20 initial_commit handling, view state machine, lock-mode non-blocking PAM IPC, unlocker, scene widgets (borders/indicator/lock/background), modal set, action table. See report §5.

### Previous Accomplishments (Phases 14–17)

- **Verified review finding 1** against current docs: `.devdocs/SESSION_HANDOFF.md` Phase 16 Modified Files table embedded unescaped literal pipes inside code spans (the `||` guard and `mount | grep`), which GFM/markdownlint parse as column separators. Escaped both; repo-wide sweep confirmed no other offending cells.
- **Verified review finding 2** against current docs: `README.md` tmpfs troubleshooting blamed a `zfs` result solely on step 1. Rewritten — `/tmp` is still ZFS-backed; users are directed to re-check all steps, including the `/etc/fstab` entry and reboot.
- **Build state clarified:** the sandbox `bmake` failure (`libucl` missing from pkg-config) was an environment artifact; user-confirmed `make` builds fine on the FreeBSD target.

### Previous Accomplishments (Phases 14–16)

- **Phase 16:** Fatal error guard on `SCRIPT_DIR` derivation in `start-hikari.sh`; README tmpfs verification switched to portable `mount | grep`.
- **Phase 15:** `SCRIPT_DIR`-based three-tier binary resolution in `start-hikari.sh`; full documentation audit.

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
- Runtime testing on FreeBSD Wayland session.
- PAM unlocker verification.
- Verify `wlr_output_effective_resolution()` exists in wlroots 0.20 headers at compile time (used in `src/layer_shell.c:177`).
