# Test Specifications & Verification Framework

*Last Updated:* 2026-07-31 06:34

---

## 1. Automated Test Specifications

| Test Case ID | Test Target | Description | Expected Outcome | Status |
|--------------|-------------|-------------|------------------|--------|
| `TC-BUILD-01` | `Makefile` (`bmake`) | FreeBSD build compilation test | Clean compilation of `hikari` and `hikari-unlocker` | Passed ✓ |
| `TC-PKG-01` | `pkg-config` | Resolve dependencies: `wlroots-0.20`, `pango`, `cairo`, `pixman`, `xkbcommon`, `libinput`, `libucl`, `epoll-shim` | All CFLAGS and LIBS resolved without missing packages | Passed ✓ |
| `TC-DOC-01` | `AGENTS.md` Linter | Header & Source prefix audit | Every script, function, loop, condition, action, and error block has exact prefix annotation | Pending |
| `TC-FORMAT-01` | `clang-format` | Code formatting compliance | Zero formatting diffs against `.clang-format` rules | Pending |

---

## 2. FreeBSD Manual Verification Protocol

### Test Protocol 1: Evdev Input Device Initialization
1. Verify `/etc/sysctl.conf` contains `kern.evdev.rcpt_mask=12` (or `3` if `moused` enabled).
2. Inspect `/dev/input/event*` nodes permissions.
3. Launch `hikari` and confirm single mouse pointer movement without duplicate cursor offset drift.

### Test Protocol 2: Shared Memory & `XDG_RUNTIME_DIR` Allocation
1. Mount `tmpfs` at a dedicated test mountpoint (e.g. `mount -t tmpfs tmpfs /mnt/test-tmpfs`).
2. Set `export XDG_RUNTIME_DIR=/mnt/test-tmpfs/runtime-${USER}` and create the directory with restrictive permissions (`mkdir -p -m 0700 "$XDG_RUNTIME_DIR"`).
3. Launch Wayland client (e.g. `alacritty` or `firefox`).
4. Confirm surface buffer allocations succeed via `posix_fallocate`.
5. Add explicit cleanup to unmount the temporary filesystem after testing (`umount /mnt/test-tmpfs`).

### Test Protocol 3: PAM Unlocker Security (`hikari-unlocker`)
1. Verify `/usr/local/etc/pam.d/hikari-unlocker` exists.
2. Identify the canonical absolute path for the `hikari-unlocker` binary. Verify that this exact binary has trusted package provenance, root ownership (e.g., `root:wheel`), and the expected permissions. Only apply mode 4555 (e.g. `chmod 4555 /usr/local/bin/hikari-unlocker`) after all these checks pass.
3. Trigger lock mode (`Meta+L`) in `hikari`.
4. Input user password and verify session unlocks cleanly upon PAM authentication success.
