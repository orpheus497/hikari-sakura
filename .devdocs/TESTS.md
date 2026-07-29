# Test Specifications & Verification Framework

*Last Updated:* 2026-07-29 03:19

---

## 1. Automated Test Specifications

| Test Case ID | Test Target | Description | Expected Outcome | Status |
|--------------|-------------|-------------|------------------|--------|
| `TC-BUILD-01` | `Makefile` (`bmake`) | FreeBSD build compilation test | Clean compilation of `hikari` and `hikari-unlocker` | Pending |
| `TC-PKG-01` | `pkg-config` | Resolve dependencies: `wlroots`, `pango`, `cairo`, `pixman`, `xkbcommon`, `libinput`, `libucl`, `epoll-shim` | All CFLAGS and LIBS resolved without missing packages | Pending |
| `TC-DOC-01` | `AGENTS.md` Linter | Header & Source prefix audit | Every script, function, loop, condition, step, and error block has exact prefix annotation | Pending |
| `TC-FORMAT-01` | `clang-format` | Code formatting compliance | Zero formatting diffs against `.clang-format` rules | Pending |

---

## 2. FreeBSD Manual Verification Protocol

### Test Protocol 1: Evdev Input Device Initialization
1. Verify `/etc/sysctl.conf` contains `kern.evdev.rcpt_mask=12` (or `3` if `moused` enabled).
2. Inspect `/dev/input/event*` nodes permissions.
3. Launch `hikari` and confirm single mouse pointer movement without duplicate cursor offset drift.

### Test Protocol 2: Shared Memory & `XDG_RUNTIME_DIR` Allocation
1. Verify `/tmp` is mounted as `tmpfs` (`mount -t tmpfs tmpfs /tmp`).
2. Set `export XDG_RUNTIME_DIR=/tmp/runtime-${USER}`.
3. Launch Wayland client (e.g. `alacritty` or `firefox`).
4. Confirm surface buffer allocations succeed via `posix_fallocate`.

### Test Protocol 3: PAM Unlocker Security (`hikari-unlocker`)
1. Verify `/usr/local/etc/pam.d/hikari-unlocker` exists.
2. Confirm `hikari-unlocker` binary permissions: `chown root:wheel`, `chmod 4555`.
3. Trigger lock mode (`Meta+L`) in `hikari`.
4. Input user password and verify session unlocks cleanly upon PAM authentication success.
