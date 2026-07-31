# Granular Task List

*Last Updated:* 2026-07-31 15:46

## Active List

- [ ] **tmpfs/ZFS Resolution (P0):** Implement tmpfs mount for `XDG_RUNTIME_DIR` — `/var/run/user/1001` is on ZFS, `posix_fallocate()` will fail. Recommended: tmpfs at `/var/run/user` via `/etc/fstab`. Also harden `start-hikari.sh` to detect ZFS-backed runtime dirs.
- [ ] **Build Validation:** Validate Phase 11 build compilation on FreeBSD via `bmake`.
- [ ] **PAM Verification:** Verify `hikari-unlocker` works correctly with OpenPAM setuid 4555 on a live FreeBSD Wayland session.

## Backlog

- [ ] **Phase 11 (Bug 6):** Address non-blocking PAM I/O in lock mode.
