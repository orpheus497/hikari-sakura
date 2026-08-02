# Granular Task List

*Last Updated:* 2026-08-02 13:25

## Active List

- [ ] **tmpfs/ZFS Resolution (P0):** Implement tmpfs mount for `XDG_RUNTIME_DIR` — `/var/run/user/1001` is on ZFS, `posix_fallocate()` will fail. Recommended: tmpfs at `/var/run/user` via `/etc/fstab` or `sudo zfs set canmount=noauto zroot/tmp`.
- [ ] **Build Validation:** Validate Phase 13-15 build compilation on FreeBSD via `bmake`.
- [ ] **PAM Verification:** Verify `hikari-unlocker` works correctly with OpenPAM setuid 4555 on a live FreeBSD Wayland session.
- [ ] **TC-FORMAT-01:** Run `clang-format` compliance check against `.clang-format` rules.
- [ ] **API Check:** Verify `wlr_output_effective_resolution()` exists in wlroots 0.20 headers (used in `src/layer_shell.c:177`).
- [ ] **Manual Cleanup:** Delete `.core` dump files from repo root.

## Recently Completed

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
