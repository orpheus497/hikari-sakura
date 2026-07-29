# Granular Task List (TODOS) & FreeBSD Execution Roadmap

*Last Updated:* 2026-07-29 04:42

---

## 1. Documentation & Architecture Foundations (`docs/`)
- [x] Create `docs/freebsd_requirements.md`: FreeBSD sysctl (`kern.evdev.rcpt_mask`), `moused`, `seatd`, `tmpfs` setup for `XDG_RUNTIME_DIR` (`posix_fallocate` ZFS issue), PAM unlocker setup (`hikari-unlocker` 4555), and package dependencies.
- [x] Create `docs/architecture_wiring.md`: Complete lifecycle, server struct breakdown, mode state machines (11 modes), surface types, and rendering pipeline.
- [x] Create `docs/data_oriented_design.md`: Struct-of-Arrays (SoA) layout specifications, SIMD 64-byte alignment, 64-bit vector sheet bitmasking, and contiguous render batching.
- [x] Create `docs/modernization_guide.md`: `wlroots` API migration path, POSIX 2008 standards, FreeBSD `clang` compiler flags, and `AGENTS.md` documentation standards.

---

## 2. Phase A: FreeBSD System Exclusivity Adaptation
- [x] Update `Makefile` to conditionally detect FreeBSD (`.if ${OS} == "FreeBSD"`) and query `epoll-shim` via `pkg-config`.
- [ ] Excise all `#ifdef __linux__` and fallback logic globally.
- [x] Replace `<linux/input-event-codes.h>` with FreeBSD native `<dev/evdev/input-event-codes.h>` header in `src/binding_config.c`.
- [x] Replace `<linux/input-event-codes.h>` with FreeBSD native `<dev/evdev/input-event-codes.h>` header in `src/configuration.c`.
- [x] Replace `<linux/input-event-codes.h>` with FreeBSD native `<dev/evdev/input-event-codes.h>` header in `src/pointer_config.c`.
- [ ] Verify `libucl` UCL parser compatibility strictly against the FreeBSD 13/14 ports tree definitions.
- [ ] Verify OpenPAM authentication flow in `hikari_unlocker.c` operates securely with setuid `4555`.

---

## 3. Phase B: Object Pool Allocator Engineering
- [x] Create `include/hikari/pool.h`: Define `struct hikari_pool` with `buffer`, `free_list`, `item_size`, `capacity`, and `count`.
- [x] Create `src/pool.c`: Implement `hikari_pool_init`, `hikari_pool_alloc`, `hikari_pool_free`, and `hikari_pool_destroy`.
- [x] Write logic in `pool.c` to track free blocks using a bitmask or stack embedded in the `free_list` block.

---

## 4. Phase C: Memory-Optimized Hybrid DOD Refactoring
- [x] Modify `include/hikari/server.h`: Add `view_pool`, `sheet_pool`, `workspace_pool`, and `tile_pool` to `struct hikari_server`.
- [x] Modify `src/server.c`: Call `hikari_pool_init()` for all pools in `hikari_server_start()`, ensuring capacities support heavy workloads (e.g., 512 views, 100 sheets).
- [x] Refactor `src/xdg_view.c`: Replace `hikari_malloc(sizeof(struct hikari_xdg_view))` with `hikari_pool_alloc(&hikari_server.view_pool)`.
- [x] Refactor `src/xwayland_view.c`: Replace `hikari_malloc(sizeof(struct hikari_xwayland_view))` with pool allocation.
- [x] Refactor `src/sheet.c`: Replace `hikari_malloc(sizeof(struct hikari_sheet))` with `hikari_pool_alloc(&hikari_server.sheet_pool)`.
- [x] Refactor `src/workspace.c`: Replace `hikari_malloc(sizeof(struct hikari_workspace))` with `hikari_pool_alloc(&hikari_server.workspace_pool)`.
- [x] Refactor `src/split.c` / `src/tile.c`: Replace `hikari_malloc` calls for tile structures with `hikari_pool_alloc(&hikari_server.tile_pool)`.
- [x] Modify destruction logic in `src/view.c`, `src/sheet.c`, etc., to use `hikari_pool_free()` instead of `hikari_free()`.
- [x] Ensure `wl_list` macro operations remain entirely untouched so Wayland `wlroots` signals do not segfault.

---

## 5. Phase D: DOD Struct-of-Arrays (SoA) View Table Refactoring
- [x] Create `include/hikari/dod.h` for SIMD 64-byte aligned SoA tables (`hikari_view_geometry_table` and `hikari_view_state_table`).
- [x] Update `struct hikari_server` in `include/hikari/server.h` to embed instances of the SoA tables.
- [x] Refactor `struct hikari_view` in `include/hikari/view.h` to use a `uint16_t id` index and remove legacy `flags` bitfields.
- [x] Route all visibility/flag checks through `hikari_server.view_state.flags[view->id]`.
- [x] Route all geometry macros/functions through `hikari_server.view_geometry`.
- [x] Vectorize geometry calculations in `src/sheet.c` and `src/renderer.c`.

---

## 6. Phase E: Build System & Compilation Verification
- [ ] Validate `Makefile` build output strictly with FreeBSD `bmake`.
- [ ] Execute runtime memory address audits to mathematically prove perfect contiguous memory alignment (DOD) inside the `wl_list` nodes.
- [ ] Test `hikari-unlocker` PAM authentication under production FreeBSD configurations.
