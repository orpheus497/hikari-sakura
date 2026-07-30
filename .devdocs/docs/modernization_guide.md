# Hikari Codebase Modernization Guide

## Overview
This guide documents the step-by-step modernization strategy for `hikari`. It covers `wlroots` API evolution, C standard compliance, FreeBSD toolchain integration, and code documentation standards enforcement.

---

## 1. `wlroots` API Modernization Plan

`hikari` was originally authored against `wlroots` 0.10.x - 0.16.x. 

### Modern Target: `wlroots` 0.20
The codebase is fully updated and verified against `wlroots` 0.20:

### Key Migration Patterns for 0.20+ (Current Supported Target):
The current supported target is `wlroots` 0.20 or newer, which introduces several breaking API changes:

1. **Pointer Axis Events:**
   * `wlr_seat_pointer_notify_axis` requires 7th argument (`enum wl_pointer_axis_relative_direction relative_direction`), passed from `event->relative_direction`.

2. **Headless Backend:**
   * `wlr_headless_backend_create` takes `struct wl_event_loop *` instead of `struct wl_display *`. Created using `wl_display_get_event_loop(server->display)`.

3. **Output Layout:**
   * `wlr_output_layout_create` requires `struct wl_display *display` parameter.

4. **Input Devices & Signals:**
   * `destroy` signal moved from individual input structures (e.g., `wlr_switch->events.destroy`) to base device (`wlr_switch->base.events.destroy`).

5. **XDG Surface Geometry:**
   * `wlr_xdg_surface_get_geometry()` replaced by direct struct member access `xdg_surface->geometry`.

6. **XDG Surface Map/Unmap Signals:**
   * `map` and `unmap` signals moved from `wlr_xdg_surface->events` to base surface `wlr_xdg_surface->surface->events`.

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

Hikari now utilizes standard heap allocation (`hikari_malloc`) across the codebase for compositor allocation paths. The previous custom Object Pool slab allocator has been removed in favor of standard system `malloc`/`calloc` calls (with `hikari_unlocker.c` directly utilizing standard `calloc` and `strdup`), simplifying the architecture and improving maintainability.

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
