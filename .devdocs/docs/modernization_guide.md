# Hikari Codebase Modernization Guide

## Overview
This guide documents the step-by-step modernization strategy for `hikari`. It covers `wlroots` API evolution, C standard compliance, FreeBSD toolchain integration, and code documentation standards enforcement.

---

## 1. `wlroots` API Modernization Plan

`hikari` was originally authored against `wlroots` 0.10.x - 0.16.x. 

### 0.17.x Migration Notes
* If migrating from older versions to 0.17.x, ensure legacy output interfaces are updated. (These are now obsolete in 0.18.0).

### Key Migration Patterns for 0.18+ (Current Supported Target):
The current supported target is `wlroots` 0.18.0 or newer, which introduces several breaking API changes:

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

## 3. Memory Allocation (Updated)

Hikari now utilizes standard heap allocation (`hikari_malloc`) across the codebase. The previous custom Object Pool slab allocator has been removed in favor of standard system `malloc`/`calloc` calls, simplifying the architecture and improving maintainability.

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
