# Session Briefing - Hikari FreeBSD Modernization & Hybrid DOD (Object Pools)

*Timestamp:* 2026-07-29 03:44

---

## 1. Project Phase & Status
* **Current Phase:** Phase 2 (Comprehensive Audit & Strategy Alignment) - **Awaiting Final Approval**
* **Overall Progress:** 35%
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
* **Awaiting Final User Approval:** The strategic documentation, implementation plan, and tracking files have been expanded to exhaustive detail detailing the massive undertaking of building the Object Pool allocator and FreeBSD exclusivity. I require explicit permission to commence execution.

---

## 5. Next 3 Concrete Execution Steps (Awaiting User Approval)

1. **Step 1: FreeBSD System Exclusivity Adaptation (Phase A)**
   * *Action:* Replace `<linux/input-event-codes.h>` in `src/binding_config.c`, `src/configuration.c`, and `src/pointer_config.c` with `<dev/evdev/input-event-codes.h>`. Delete all Linux fallback `#ifdef` paths.
   * *Time Estimate:* ~10 mins.

2. **Step 2: Object Pool Allocator Engineering (Phase B)**
   * *Action:* Create `include/hikari/pool.h` and `src/pool.c` and implement the slab allocator logic (`buffer`, `free_list`, `hikari_pool_init`, `alloc`, `free`).
   * *Time Estimate:* ~30 mins.

3. **Step 3: Memory-Optimized Hybrid DOD Refactoring (Phase C)**
   * *Action:* Embed pools into `hikari_server`, initialize them in `server.c`, and replace all target `hikari_malloc` calls in `src/view.c`, `src/sheet.c`, etc. with `hikari_pool_alloc`.
   * *Time Estimate:* ~45 mins.
