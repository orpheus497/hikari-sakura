# Hikari Architectural Blueprint & Data-Oriented Design System

*Last Updated:* 2026-07-29 03:19

This project is exclusively targeted at FreeBSD. There is no Linux cross-compatibility, and no changes shall undermine the FreeBSD target.

---

## 1. Subsystem Architecture & Execution Flow

```
                                  +-----------------------+
                                  |        main.c         |
                                  +-----------+-----------+
                                              |
                                              v
                                  +-----------------------+
                                  |     src/server.c      |
                                  | (Server State / Loop) |
                                  +-----------+-----------+
                                              |
     +-------------------+--------------------+--------------------+-------------------+
     |                   |                    |                    |                   |
     v                   v                    v                    v                   v
+----------+       +-----------+        +-----------+        +-----------+       +-----------+
| src/     |       | src/      |        | src/      |        | src/      |       | src/      |
| output.c |       | view.c    |        | keyboard.c|        | pointer.c |       | layer_    |
| (Display |       | workspace.|        | (Keyboard |        | (Pointer  |       | shell.c   |
|  Render) |       | sheet.c)  |        |  Events)  |        |  Events)  |       | (Docks)   |
+----------+       +-----------+        +-----------+        +-----------+       +-----------+
```

---

## 2. Granular Modal State Machine Index

| Mode Enum | Target Handler | Purpose |
|-----------|----------------|---------|
| `HIKARI_MODE_NORMAL` | `src/normal_mode.c` | Core window management and default keybindings |
| `HIKARI_MODE_MOVE` | `src/move_mode.c` | Interactive view dragging and position updates |
| `HIKARI_MODE_RESIZE` | `src/resize_mode.c` | Interactive window edge/corner resizing |
| `HIKARI_MODE_LOCK` | `src/lock_mode.c` | Screen locking mode with PAM unlocker focus |
| `HIKARI_MODE_SHEET_ASSIGN` | `src/sheet_assign_mode.c` | Reassigning focused view to target sheet (0-9) |
| `HIKARI_MODE_GROUP_ASSIGN` | `src/group_assign_mode.c` | Assigning focused view to named window group |
| `HIKARI_MODE_MARK_ASSIGN` | `src/mark_assign_mode.c` | Binding view to quick-access character mark |
| `HIKARI_MODE_MARK_SELECT` | `src/mark_select_mode.c` | Jumping focus to tagged view mark |
| `HIKARI_MODE_LAYOUT_SELECT`| `src/layout_select_mode.c` | Selecting layout algorithm (stack, grid, queue) |
| `HIKARI_MODE_INPUT_GRAB` | `src/input_grab_mode.c` | Redirecting input to exclusive surface request |
| `HIKARI_MODE_GRAB_KEYBOARD`| `src/grab_keyboard_mode.c` | Raw keyboard event bypass for guest applications |

---

## 3. Data-Oriented Design (DOD) Memory & Data Structures

```c
/* SIMD 64-byte aligned spatial layout table */
struct hikari_view_geometry_table {
  int x[HIKARI_MAX_VIEWS];
  int y[HIKARI_MAX_VIEWS];
  int width[HIKARI_MAX_VIEWS];
  int height[HIKARI_MAX_VIEWS];
};
_Alignas(64) struct hikari_view_geometry_table view_geometry;

/* Packed bitfield visibility table for O(1) cache line checks */
struct hikari_view_state_table {
  uint16_t sheet_mask[HIKARI_MAX_VIEWS];
  uint8_t flags[HIKARI_MAX_VIEWS];
  uint16_t group_id[HIKARI_MAX_VIEWS];
};
_Alignas(64) struct hikari_view_state_table view_state;
```

---

## 4. Active Backlog & Implementation Registry

### Active Backlog Tasks:
* [x] **Item 1:** Create `docs/` technical documentation suite (`freebsd_requirements.md`, `architecture_wiring.md`, `data_oriented_design.md`, `modernization_guide.md`).
* [x] **Item 2:** Modernize `Makefile` with FreeBSD `epoll-shim` flags via `pkg-config`.
* [x] **Item 3:** Add evdev header compatibility (`<dev/evdev/input-event-codes.h>`) for FreeBSD builds.
* [x] **Item 4:** Complete `AGENTS.md` line-by-line documentation prefixes across remaining `src/` modules.

### Implementation Registry:
* **Item 1 Completed:** Created full `docs/` directory with 4 technical manuals (2026-07-29).
* **Item 2 Completed:** Updated `Makefile` with FreeBSD `epoll-shim` conditional check (2026-07-29).
* **Item 3 Completed:** Removed legacy Linux evdev headers across all source files in favor of native FreeBSD endpoints.
* **Item 4/5 Completed:** Designed and deployed a contiguous Slab object allocator (`hikari_pool_alloc`), adapting all dynamic allocations across the `hikari_server` into contiguous memory blocks while preserving `wl_list` invariants.
* **Item 6 Completed:** Implemented DOD Struct-of-Arrays (SoA) view geometry table / Slab allocator object pools in core headers and source.
* **Item 7 Completed:** Synchronized DOD Struct-of-Arrays refactoring status. Vectorized geometry calculations and routed visibility flags.
