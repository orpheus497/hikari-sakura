# Granular Task List

*Last Updated:* 2026-08-13 05:41 (environment clock, corroborated by build mtimes)

## Active List

- [ ] **Live TTY runtime test:** `start-hikari` with seatd running — expect a working session or a loud stderr diagnostic (P0-2 killed the silent zombie path). Verify keyboard bindings (default config), cursor movement, client launch, lock/unlock.
- [ ] **tmpfs/ZFS Resolution (P0):** Implement tmpfs mount for `XDG_RUNTIME_DIR` — `/var/run/user/1001` is on ZFS, `posix_fallocate()` will fail. Recommended: tmpfs at `/var/run/user` via `/etc/fstab` or `sudo zfs set canmount=noauto zroot/tmp`.
- [ ] **PAM Verification:** Verify `hikari-unlocker` works correctly with OpenPAM setuid 4555 on a live FreeBSD Wayland session.
- [ ] **Layer-client spot check:** run a panel/bar (or swaybg) with a `WITH_LAYERSHELL=YES` build to exercise the new scene attachment.
- [ ] **Wallpaper asset:** `share/backgrounds/hikari/hikari_wallpaper.png` still absent — install rule is guarded and skips with a warning; restore the PNG or drop the outputs section from `etc/hikari/hikari.conf`.
- [ ] **TC-FORMAT-01:** Run `clang-format` compliance check against `.clang-format` rules.
- [ ] **Comment-header rollout (optional, deferred):** 48 of 55 `src/` files lack the `[COMMENT] Script function and purpose:` header mandated by AGENTS.md (Phase 8 claim amended 2026-08-13). Rollout awaits user direction.
- [ ] **Cosmetic:** silence enum-compare warnings at `src/dnd_mode.c:63` and `src/move_mode.c:78` (value-identical constants; harmless).

## Recently Completed

- [x] **Phase 18b remediation (2026-08-13):** P0-1 xkb symbol, P0-2 backend-start check, P0-3 headless-create argument, P0-4 default config + install guards, P1-5 keymap type tag, P1-6 numeric mouse keycode, P1-7 layer-shell scene attach, P2-8 global list re-init, P2-9/P2-10 diagnostics, P2-11 dead focus params, P2-12 version.h rule, P2-13 comment prefixes — all applied and annotated (register: INVESTIGATION_RUNTIME_FAILURE.md §9).
- [x] **Build-discovered fixes (2026-08-13):** P1-16 popup geometry → `popup->current.geometry`; P1-17 `xcb_size_hints_t`; P1-18 xwayland associate/dissociate lifecycle.
- [x] **TC-BUILD-01 revalidated (2026-08-13):** `make clean && make` — 0 errors, both binaries linked.
- [x] **TC-BUILD-02 (2026-08-13):** full-feature clean build (`WITH_XWAYLAND/LAYERSHELL/SCREENCOPY/GAMMACONTROL/VIRTUAL_INPUT=YES`) — 0 errors, clean link. First time feature configs compiled in this tree.

- [x] **API check closed (2026-08-13):** `wlr_output_effective_resolution()` at `src/layer_shell.c:177` compiles and links against installed wlroots 0.20 — existence verified by the successful user build.
- [x] **Manual cleanup closed (2026-08-13):** no `.core` files exist in the repo root — item obsolete.
- [x] **SESSION_HANDOFF table pipe escaping (Phase 17):** Escaped literal pipes in Phase 16 Modified Files table; markdownlint column miscount resolved.
- [x] **README tmpfs troubleshooting clarity (Phase 17):** `zfs` result now means `/tmp` still ZFS-backed; users re-check all steps incl. fstab + reboot.
- [x] **Build validation (2026-08-13):** User-confirmed `make` builds clean on the FreeBSD target. (Agent-sandbox `bmake` failure was a missing `libucl` pkg-config entry — environment artifact.)
- [x] **start-hikari.sh SCRIPT_DIR resolution (Phase 15):** Added `SCRIPT_DIR` derivation for sibling binary lookup. Three-tier: `${SCRIPT_DIR}/hikari` → PATH → `./hikari`.
- [x] **ZFS detection in start-hikari.sh (Phase 12):** Warns user when `XDG_RUNTIME_DIR` is on ZFS.
- [x] **XDG_RUNTIME_DIR validation (Phase 12):** Checks ownership (current user) and permissions (0700) for all paths.
- [x] **PAM config fix (Phase 12):** `auth include passwd` → `auth include system` on FreeBSD.
- [x] **Non-blocking PAM I/O (Phase 12):** `read()` → `wl_event_loop_add_fd()` in lock_mode.c.
- [x] **BUG-1 move_resize_view dx fix (Phase 14):** `+ dy` → `+ dx` for lx calculation.
- [x] **BUG-2 outputs_disabled init (Phase 14):** Added init in `hikari_lock_mode_init()` and reset in `cancel()`.
- [x] **BUG-3 command.c waitpid fix (Phase 14):** Fixed inverted errno check.
- [x] **Security: explicit_bzero (Phase 14):** Replaced `memset` for password buffer zeroing.
- [x] **5 missing listener cleanups (Phase 14):** Added `wl_list_remove()` in `hikari_server_stop()`.
- [x] **Dead code removal (Phase 14):** render.h deleted, mode_handler removed, unused struct members cleaned.
- [x] **Duplicate includes removed (Phase 13):** `server.c` had duplicate `wlr_data_device.h` and `wlr_seat.h`.
- [x] **Blocking wait→waitpid (Phase 13):** `lock_mode.c:154` — `wait()` replaced with `waitpid(-1, &status, WNOHANG)`.
- [x] **output->server init (Phase 13):** `output.c` — `output->server = &hikari_server` now set inside `hikari_output_init()`.
- [x] **Switch Toggle Bug (Phase 13):** Fixed cascading if→else if in `src/switch.c`.
- [x] **Output cairo surface check (Phase 13):** Fixed `output.c:85` checking wrong surface.
- [x] **main.c comment migration (Phase 13):** All `##` prefixes replaced with `[COMMENT]` format.
- [x] **FreeBSD Handbook cross-reference (Phase 13):** Ch.6 §6.1-6.4 verified — all requirements met.
