# Granular Task List

*Last Updated:* 2026-07-31 16:45

## Active List

- [ ] **tmpfs/ZFS Resolution (P0):** Implement tmpfs mount for `XDG_RUNTIME_DIR` — `/var/run/user/1001` is on ZFS, `posix_fallocate()` will fail. Recommended: tmpfs at `/var/run/user` via `/etc/fstab`. Also harden `start-hikari.sh` to detect ZFS-backed runtime dirs.
- [ ] **Build Validation:** Validate Phase 13 build compilation on FreeBSD via `bmake`.
- [ ] **PAM Verification:** Verify `hikari-unlocker` works correctly with OpenPAM setuid 4555 on a live FreeBSD Wayland session.
- [ ] **TC-FORMAT-01:** Run `clang-format` compliance check against `.clang-format` rules.

## Recently Completed

- [x] **Duplicate includes removed:** `server.c` had duplicate `wlr_data_device.h` and `wlr_seat.h`.
- [x] **Blocking wait→waitpid:** `lock_mode.c:154` — `wait()` replaced with `waitpid(-1, &status, WNOHANG)`.
- [x] **output->server init:** `output.c` — `output->server = &hikari_server` now set inside `hikari_output_init()`.
- [x] **Switch Toggle Bug:** Fixed cascading if→else if in `src/switch.c` (both begin/end actions fired on every toggle).
- [x] **Output cairo surface check:** Fixed `output.c:85` checking wrong surface after `output_surface` creation.
- [x] **Phase 11 (Bug 6):** Non-blocking PAM I/O in lock mode — implemented via `wl_event_loop_add_fd`.
- [x] **main.c comment migration:** All `##` prefixes replaced with `[COMMENT]` format.
- [x] **FreeBSD Handbook cross-reference:** Ch.6 §6.1-6.4 verified — all requirements met.
