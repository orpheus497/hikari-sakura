# Forward Strategy & Plans

*Last Updated:* 2026-07-31 15:53

## Implementations to be Fully Implemented

1. **PAM Verification Execution:**
   - Deploy `hikari` and `hikari-unlocker` to a native FreeBSD environment.
   - Identify the canonical absolute installed path for `hikari-unlocker` (e.g. `${PREFIX}/bin/hikari-unlocker`).
   - Verify the binary has trusted package provenance and existing `root:wheel` ownership.
   - Only after verification, apply mode `4555` to the absolute installed path (e.g. `chmod 4555 /usr/local/bin/hikari-unlocker`).
   - Launch compositor and invoke `Meta+L` to trigger the lock screen.
   - Verify input characters render correctly and PAM unlocks upon Enter.

2. **Non-Blocking PAM I/O Refactor:**
   - Architect a pipe-based communication channel between `hikari` (parent) and `hikari-unlocker` (child).
   - Use `wl_event_loop_add_fd` to listen for the authentication result without blocking Wayland rendering.
   - Update lock indicator UI dynamically while waiting for authentication to complete.

3. **Final Build Validation:**
   - Execute `bmake` on the FreeBSD target to ensure all Phase 11 syntax and Makefile modifications compile correctly without warnings.
