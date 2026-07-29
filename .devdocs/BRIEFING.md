# Session Briefing - Hikari FreeBSD Modernization & Hybrid DOD (Object Pools)

*Timestamp:* 2026-07-29 05:56

---

## 1. Project Phase & Status
* **Current Phase:** Phase 6 (FreeBSD Build Verification & Integration Testing)
* **Overall Progress:** 80%
* **Target Operating System:** FreeBSD 13.x / 14.x+ (Strictly Exclusive)

---

## 2. File-by-File Codebase Audit & Exhaustive Modernization Matrix

### A. Subsystem Core & Entrypoints
* `main.c`: CLI parser, path resolution (`get_config_path`), security check (`geteuid() != 0`). *Modernization Plan:* FreeBSD non-root privilege assertion.
* `hikari_unlocker.c`: PAM unlocker daemon using OpenPAM (`/usr/local/etc/pam.d/hikari-unlocker`). *Modernization Plan:* FreeBSD PAM setuid `4555` mode.

### B. Server & Event Subsystems
* `server.h` / `server.c`: Central compositor server state (`hikari_server`). *Modernization Plan:* FreeBSD `epoll-shim` event loop integration. Crucially, embed new Hybrid DOD Object Pools (`view_pool`, `sheet_pool`, `workspace_pool`, `tile_pool`) initialized at startup to guarantee contiguous memory for core structs.
* `output.h` / `output.c` / `output_config.c`: Displays and refresh rates.
* `view.h` / `view.c` / `xdg_view.c` / `xwayland_view.c`: Base view object, window geometry, surface bindings. *Modernization Plan:* Replace all internal `hikari_malloc` calls with custom `hikari_pool_alloc` to pull from the pre-allocated server Object Pools. `wl_list` integration remains entirely intact to prevent breaking `wlroots` signals.

### C. Spatial & Layout Hierarchy
* `workspace.h` / `workspace.c` & `sheet.h` / `sheet.c`: Virtual desktops (Sheets 0-9). *Modernization Plan:* Eliminate heap fragmentation by migrating these structures to the `workspace_pool` and `sheet_pool`.
* `group.h` / `group.c` & `tile.h` / `tile.c` & `split.c`: Tiling engines. *Modernization Plan:* Shift allocations to the `tile_pool` for cache-friendly iterations.

### D. Input & Device Handling
* `keyboard.h`, `pointer.h`, `binding_config.c`, `configuration.c`, `pointer_config.c`: Action dispatch tables and keybindings. *Modernization Plan:* Eradicate all `#ifdef __linux__` logic. Hardcode FreeBSD native `<dev/evdev/input-event-codes.h>` header integration.

---

## 3. FreeBSD-Specific Architectural Particulars

1. **Input System:** FreeBSD `sysctl kern.evdev.rcpt_mask=12` (or `3` with `moused`). Native header `<dev/evdev/input-event-codes.h>`.
2. **Shared Memory (`XDG_RUNTIME_DIR`):** `/tmp` mounted on `tmpfs` to support `posix_fallocate` (unsupported by ZFS shared memory descriptors).
3. **Privilege & Session:** `seatd` daemon integration and setuid PAM unlocker binary (`hikari-unlocker`, `4555` permissions).
4. **Build System:** FreeBSD `bmake`, `clang`, `epoll-shim` linking via strict `pkg-config`.
5. **Memory Contiguity (DOD):** Pre-allocated slabs via `hikari_pool` subsystem.

---

## 4. Current Blockers
* **Pending Compilation:** We must verify syntax, compilation, and `wl_list` invariants under FreeBSD `bmake`.

---

1. **Step 1: Code Verification & Build Testing (Phase 6)**
   * *Action:* We must execute `bmake` against the FreeBSD ports tree to flush out syntax missing headers, verify our Hybrid DOD, Wlroots 0.18+ (using `wlroots-0.18.pc`), and `hikari_render_batch` optimizations compile, and confirm the Slab Allocator offsets. (Blocked on FreeBSD environment).
   * *Time Estimate:* ~15 mins.

2. **Step 2: Deployment and End-to-End Validation**
   * *Action:* Validate functionality in a real FreeBSD Wayland environment.
   * *Time Estimate:* ~30 mins.
