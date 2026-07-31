# Forward Strategy & Plans

*Last Updated:* 2026-07-31 15:27

## Implementations to be Fully Implemented

1. **PAM Verification Execution:**
   - Deploy `hikari` and `hikari-unlocker` to a native FreeBSD environment.
   - Run `chown root:wheel hikari-unlocker` and `chmod 4555 hikari-unlocker`.
   - Launch compositor and invoke `Meta+L` to trigger the lock screen.
   - Verify input characters render correctly and PAM unlocks upon Enter.

2. **Non-Blocking PAM I/O Refactor:**
   - Architect a pipe-based communication channel between `hikari` (parent) and `hikari-unlocker` (child).
   - Use `wl_event_loop_add_fd` to listen for the authentication result without blocking Wayland rendering.
   - Update lock indicator UI dynamically while waiting for authentication to complete.

3. **Final Build Validation:**
   - Execute `bmake` on the FreeBSD target to ensure all Phase 11 syntax and Makefile modifications compile correctly without warnings.
