# Hikari Project Blueprint

*Last Updated:* 2026-07-31 15:53

## 1. Subsystem Architecture

```text
                                  +-----------------------+
                                  |     main() Entry      |
                                  +-----------+-----------+
                                              |
                                              v
                                  +-----------------------+
                                  |   src/server.c        |
                                  |   (Server Lifecycle)  |
                                  +-----------+-----------+
                                              |
           +----------------------------------+----------------------------------+
           |                                  |                                  |
           v                                  v                                  v
+---------------------+            +---------------------+            +---------------------+
|  src/output.c       |            |   src/view.c        |            |  src/keyboard.c     |
|    (wlr_scene)      |            |   src/workspace.c   |            |  src/pointer.c      |
|                     |            | (Views/Sheets/Tiles)|            |  (Input Events)     |
+---------------------+            +---------------------+            +---------------------+
```

## 2. Modal State Machine Index

| Mode | Handler File | Description |
|---|---|---|
| **Normal Mode** | `src/normal_mode.c` | Default state. Keybindings and mouse gestures dispatch window actions. |
| **Move Mode** | `src/move_mode.c` | Interactive view repositioning (via cursor drag or keybinding). |
| **Resize Mode** | `src/resize_mode.c` | Interactive view resizing (via cursor drag or keybinding). |
| **Lock Mode** | `src/lock_mode.c` | Screen locker overlay active; dispatches input to lock screen indicator. |
| **Sheet Assign Mode** | `src/sheet_assign_mode.c` | Modal prompt for reassigning focused view to target sheet (0-9). |
| **Group Assign Mode** | `src/group_assign_mode.c` | Modal prompt for assigning focused view to named group. |
| **Mark Assign Mode** | `src/mark_assign_mode.c` | Modal prompt for tagging focused view with quick-jump mark (a-z). |
| **Mark Select Mode** | `src/mark_select_mode.c` | Modal prompt for jumping focus to tagged view mark. |
| **Layout Select Mode** | `src/layout_select_mode.c` | Modal prompt for applying tile layout algorithms. |
| **Input Grab Mode** | `src/input_grab_mode.c` | Redirects input to explicit client surface request. |
| **Grab Keyboard Mode**| `src/grab_keyboard_mode.c` | Exclusively forwards raw keyboard events to target application. |

## 3. Implementation Registry

* **Standard Memory Allocation:** Migrated compositor allocation paths to `hikari_malloc`.
* **wlr_scene Rendering:** Migrated borders, lock indicator, backgrounds, and indicator bars to wlroots `wlr_scene` graph.
* **wlroots 0.20 Migration:** Resolved 13+ API-breaking changes across `cursor.c`, `output.c`, `server.c`, `switch.c`, and `xdg_view.c`.
* **FreeBSD Adaptations:** Native evdev headers adopted, Makefile points to wlroots 0.20 via pkg-config with epoll-shim conditional.
* **DOD Reverted:** Struct-of-Arrays (SoA) and Object Pool implementations were removed as they added unnecessary complexity and are incompatible with `wlr_scene` workflows.
* **Test Specifications:** Added build compilation (TC-BUILD-01), pkg-config dependencies (TC-PKG-01), and manual protocols for Evdev, Shared Memory, and PAM.

## 4. Test Specifications & Verification Framework

| Test Case ID | Test Target | Description | Expected Outcome | Status |
|--------------|-------------|-------------|------------------|--------|
| `TC-BUILD-01` | `Makefile` (`bmake`) | FreeBSD build compilation test (wlroots 0.20, Phase 6) | Clean compilation of `hikari` and `hikari-unlocker` | Passed ✓ (Phase 6 — pre-Phase 11 fixes; revalidation pending) |
| `TC-PKG-01` | `pkg-config` | Resolve dependencies: `wlroots-0.20`, `pango`, `cairo`, `pixman`, `xkbcommon`, `libinput`, `libucl`, `epoll-shim` | All CFLAGS and LIBS resolved without missing packages | Passed ✓ (Phase 6 — dependency set unchanged) |
| `TC-DOC-01` | `AGENTS.md` Linter | Source prefix audit: every script/source, standalone function, and specific action block has the exact `[COMMENT] Script function and purpose:`, `[COMMENT] Function purpose:`, or `[COMMENT] Action purpose:` prefix annotation | All modified files comply with the three defined prefixes | Pending |
| `TC-FORMAT-01` | `clang-format` | Code formatting compliance | Zero formatting diffs against `.clang-format` rules | Pending |

### FreeBSD Manual Verification Protocol

#### Test Protocol 1: Evdev Input Device Initialization

1. Verify `/etc/sysctl.conf` contains `kern.evdev.rcpt_mask=12` (or `3` if `moused` enabled).
2. Inspect `/dev/input/event*` nodes permissions.
3. Launch `hikari` and confirm single mouse pointer movement without duplicate cursor offset drift.

#### Test Protocol 2: Shared Memory & `XDG_RUNTIME_DIR` Allocation

1. Mount `tmpfs` at a dedicated test mountpoint (e.g. `mount -t tmpfs tmpfs /mnt/test-tmpfs`).
2. Set `export XDG_RUNTIME_DIR=/mnt/test-tmpfs/runtime-${USER}` and create the directory with restrictive permissions (`mkdir -p -m 0700 "$XDG_RUNTIME_DIR"`).
3. Launch Wayland client (e.g. `alacritty` or `firefox`).
4. Confirm surface buffer allocations succeed via `posix_fallocate`.
5. Add explicit cleanup to unmount the temporary filesystem after testing (`umount /mnt/test-tmpfs`).

#### Test Protocol 3: PAM Unlocker Security (`hikari-unlocker`)

1. Verify `/usr/local/etc/pam.d/hikari-unlocker` exists.
2. Identify the canonical absolute path for the `hikari-unlocker` binary. Verify that this exact binary has trusted package provenance, root ownership (e.g., `root:wheel`), and the expected permissions. Only apply mode 4555 (e.g. `chmod 4555 /usr/local/bin/hikari-unlocker`) after all these checks pass.
3. Trigger lock mode (`Meta+L`) in `hikari`.
4. Input user password and verify session unlocks cleanly upon PAM authentication success.

## 5. Resources Index

Read and cross-reference the content against the codebase.

* https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/Getting-started
* https://wayland-book.com/
* https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/tinywl
* https://drewdevault.com/blog/Writing-a-Wayland-compositor-1/
* https://drewdevault.com/blog/Writing-a-wayland-compositor-part-2/
* https://drewdevault.com/blog/Input-handling-in-wlroots/
* https://drewdevault.com/blog/Wayland-shells/
* https://emersion.fr/blog/2019/intro-to-damage-tracking/
* https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/Packaging-recommendations
* https://www.phoronix.com/news/wlroots-0.20-Released
