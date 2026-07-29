# Hikari Codebase Modernization Guide

## Overview
This guide documents the step-by-step modernization strategy for `hikari`. It covers `wlroots` API evolution, C standard compliance, FreeBSD toolchain integration, and code documentation standards enforcement.

---

## 1. `wlroots` API Modernization Plan

`hikari` was originally authored against `wlroots` 0.10.x - 0.17.x. The supported target is `wlroots` 0.18.0 or newer, which introduced several breaking API changes:

### Key Migration Patterns for 0.18+:

1. **Allocators & Renderers:**
   * Modern `wlroots` requires `wlr_allocator_autocreate(backend, renderer)` during backend startup.
   * `wlr_backend_autocreate` signature updates (`wl_display` parameter handling).

2. **XDG Shell & Surface Handling:**
   * `wlr_xdg_shell_create` takes `wl_display*` directly.
   * Surface commit listeners use updated `wlr_surface` event structures.

3. **Output Layout & Damage:**
   * `wlr_output_layout_add_auto` and `wlr_output_damage` interface updates.

---

## 2. FreeBSD Toolchain & Build System Integration

### FreeBSD Compiler & Flags
Modern FreeBSD utilizes `clang` as the default system compiler.

* **Build Tool:** `bmake` (FreeBSD native `make`).
* **Flags:**
  * `-Wall -Wextra -Werror=implicit-function-declaration`
  * `-D_POSIX_C_SOURCE=200809L`
  * Includes `-I/usr/local/include` and library links from `pkg-config`.

### `epoll-shim` Integration
FreeBSD uses `kqueue` instead of Linux `epoll`. Linux epoll compatibility is provided via `epoll-shim`. `Makefile` includes `UCL_CFLAGS` and `epoll-shim` dependencies via `pkg-config`.

---

## 3. Data-Oriented Design (DOD) Memory Pools

To avoid dynamic heap fragmentation, Hikari utilizes contiguous Object Pools for Wayland entities:

* **Slab Allocator:** Implemented via `include/hikari/pool.h` and `src/pool.c`. Provides $O(1)$ object allocation/deallocation via an embedded intrusive free list.
* **Wayland Objects:** Allocations for `hikari_xdg_view`, `hikari_xwayland_view`, `hikari_sheet`, `hikari_workspace`, and `hikari_tile` are redirected to `hikari_pool_alloc()` / `hikari_pool_free()`.
* **Array Contiguity Workaround:** For components like `hikari_workspace` that require multiple `hikari_sheet` instances in a contiguous array, the memory pool's `item_size` is sized to accommodate the entire array as a single block (e.g., `HIKARI_NR_OF_SHEETS * sizeof(struct hikari_sheet)`).

---

## 4. Code Documentation Standards Compliance (`AGENTS.md`)

All modified or new source and header files MUST incorporate exact line-by-line documentation prefixes before function, block, and logic definitions:

| Prefix | Applied To |
|--------|------------|
| `##Script function and purpose: [Explanation]` | Header / Source file top banner |
| `##Class purpose: [Explanation]` | Structures / Types |
| `##Method purpose: [Explanation]` | Class/Object methods |
| `##Function purpose: [Explanation]` | Standalone functions |
| `##Step purpose: [Explanation]` | Logical code execution blocks |
| `##Action purpose: [Explanation]` | Specific commands or actions |
| `##Condition purpose: [Explanation]` | `if` / `switch` branching statements |
| `##Loop purpose: [Explanation]` | `for` / `while` iteration loops |
| `##Error purpose: [Explanation]` | Error handling and assert blocks |
