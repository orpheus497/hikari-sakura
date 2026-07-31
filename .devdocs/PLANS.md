# Forward Strategy & Plans

*Last Updated:* 2026-07-31 16:34

## Implementations to be Fully Implemented

1. **PAM Verification Execution:**
   - Deploy `hikari` and `hikari-unlocker` to a native FreeBSD environment.
   - Identify the canonical absolute installed path for `hikari-unlocker` (e.g. `${PREFIX}/bin/hikari-unlocker`).
   - Verify the binary has trusted package provenance and existing `root:wheel` ownership.
   - Only after verification, apply mode `4555` to the absolute installed path (e.g. `chmod 4555 /usr/local/bin/hikari-unlocker`).
   - Launch compositor and invoke `Meta+L` to trigger the lock screen.
   - Verify input characters render correctly and PAM unlocks upon Enter.

2. **Final Build Validation:**
   - Execute `bmake` on the FreeBSD target to ensure all Phase 12 syntax and Makefile modifications compile correctly without warnings.

3. **Runtime Testing:**
   - Resolve tmpfs/ZFS incompatibility for XDG_RUNTIME_DIR.
   - Launch hikari on FreeBSD Wayland session.
   - Verify PAM non-blocking unlock (implemented — needs live test).

## Completed Implementations

1. **Non-Blocking PAM I/O Refactor:** ✅ Completed 2026-07-31.
   - Replaced blocking `read()` in `submit_password()` with `wl_event_loop_add_fd()`.
   - Added `locker_result_handler()` callback.
   - Added `locker_event_source` field to `struct hikari_lock_mode`.
   - Cleanup in both handler and `cancel()` path.

