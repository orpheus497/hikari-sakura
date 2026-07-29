# Exhaustive Implementation Plan: Hikari FreeBSD Modernization & Hybrid DOD

This document serves as the comprehensive architectural blueprint for the Hikari compositor modernization project. This is a massive undertaking requiring precision engineering, strictly targeting FreeBSD 13.x/14.x+, and replacing dynamic heap allocation with a Data-Oriented Object Pool architecture.

## 1. Architectural Philosophy & Constraints

1.  **Strict FreeBSD Exclusivity:** Hikari will be a premier FreeBSD Wayland compositor. All Linux legacy code (`#ifdef __linux__`, specific Linux `input-event-codes.h`, generic fallback behaviors) will be completely excised. We depend solely on the FreeBSD `evdev` translation layer (`libepoll-shim`) and native kernel capabilities.
2.  **Hybrid Data-Oriented Design (DOD):** A pure Struct-of-Arrays (SoA) rewrite would completely sever `wlroots` integration, which relies on intrusive `wl_list` pointers. Instead, we implement a **Slab/Object Pool Allocator**. 
    *   **The Mechanism:** Pre-allocate massive, contiguous memory blocks (slabs) for core objects at server startup.
    *   **The Result:** Objects (Views, Sheets, etc.) maintain their `wl_list` hooks, satisfying `wlroots`, but because they are physically allocated sequentially in RAM, list traversal becomes highly cache-localized, eliminating heap fragmentation.

---

## 2. Phase A: FreeBSD System Exclusivity & Foundation

### A1. Header Standardization
Remove conditional Linux includes and hardcode FreeBSD paths.

*   **Files Affected:**
    *   `src/binding_config.c`
    *   `src/configuration.c`
    *   `src/pointer_config.c`
*   **Action:** 
    *   Find and remove `#if defined(__FreeBSD__)` wrappers around `<dev/evdev/input-event-codes.h>`.
    *   Delete the `#elif defined(__linux__)` blocks completely.
    *   Force `#include <dev/evdev/input-event-codes.h>` globally.

### A2. Build System Consolidation
Ensure the build process is tailored for FreeBSD `bmake`.

*   **Files Affected:**
    *   `Makefile`
*   **Action:**
    *   Remove `.if ${OS} == "Linux"` conditions.
    *   Strictly mandate `pkg-config --cflags --libs epoll-shim`.
    *   Ensure `libucl` and `wlroots` linkage is statically defined for FreeBSD port conventions.

---

## 3. Phase B: Object Pool Allocator Engineering

### B1. The Allocator Subsystem
Develop a contiguous, pre-allocated memory pool for fixed-size structs.

*   **Files Created:**
    *   `include/hikari/pool.h`
    *   `src/pool.c`
*   **Struct Design:**
    ```c
    struct hikari_pool {
        void *buffer;           // Contiguous slab memory
        uint8_t *free_list;     // Bitmask or stack tracking free slots
        size_t item_size;       // Size of each struct (e.g. sizeof(struct hikari_view))
        size_t capacity;        // Max items in the pool
        size_t count;           // Current allocated items
    };
    ```
*   **API Exposure:**
    *   `void hikari_pool_init(struct hikari_pool *pool, size_t capacity, size_t item_size);`
    *   `void *hikari_pool_alloc(struct hikari_pool *pool);`
    *   `void hikari_pool_free(struct hikari_pool *pool, void *ptr);`
    *   `void hikari_pool_destroy(struct hikari_pool *pool);`

---

## 4. Phase C: Memory-Optimized Hybrid DOD Refactoring

### C1. Server Initialization
Pre-allocate the pools inside the central compositor state.

*   **Files Affected:**
    *   `include/hikari/server.h`
    *   `src/server.c`
*   **Action:**
    *   Extend `struct hikari_server` with pool instances:
        *   `struct hikari_pool view_pool` (Capacity: 512)
        *   `struct hikari_pool sheet_pool` (Capacity: 100)
        *   `struct hikari_pool workspace_pool` (Capacity: 16)
        *   `struct hikari_pool tile_pool` (Capacity: 1024)
    *   Call `hikari_pool_init()` during `hikari_server_start()`.

### C2. View Subsytem Overhaul
Migrate `hikari_view` from `malloc` to the `view_pool`.

*   **Files Affected:**
    *   `src/view.c`
    *   `src/xdg_view.c`
    *   `src/xwayland_view.c`
*   **Action:**
    *   Locate all instances of `hikari_malloc(sizeof(struct hikari_xdg_view))` and `hikari_malloc(sizeof(struct hikari_xwayland_view))`.
    *   Replace with `hikari_pool_alloc(&hikari_server.view_pool)`. (Note: Since XDG and XWayland views embed `hikari_view` or inherit from it, we must size the pool to the largest view subclass, or maintain separate pools for each subclass).
    *   Update `hikari_view_destroy()` to use `hikari_pool_free()`.

### C3. Sheet & Workspace Refactoring
Migrate `hikari_sheet` and `hikari_workspace` to their respective pools.

*   **Files Affected:**
    *   `src/sheet.c`
    *   `src/workspace.c`
*   **Action:**
    *   Replace dynamic allocations of Sheets with pool allocations.
    *   Ensure the Workspaces (0-9) are initialized completely contiguously.

### C4. Group & Tile Reallocation
Optimize spatial tiling and window groupings.

*   **Files Affected:**
    *   `src/group.c`
    *   `src/tile.c`
    *   `src/split.c`
*   **Action:**
    *   Replace node/tile allocations (`hikari_malloc(sizeof(struct hikari_split_container))`, etc.) with `tile_pool` allocations to ensure layout algorithms iterate over contiguous cache lines.

---

## 5. Verification & Validation Protocol

1.  **Build Verification:** Execute `bmake clean && bmake`. The compilation must complete without any missing `<linux/...>` header errors on a FreeBSD 13.x/14.x system.
2.  **Memory Coherence Audit:** Inject a debug module during runtime that computes the memory address delta between consecutive elements in the `wl_list` of `hikari_server.visible_views`. The delta must match `item_size` exactly, proving flawless DOD memory contiguity.
3.  **Stress Testing:** Spawn maximum capacity (e.g., 500+ views) to trigger `hikari_pool` exhaustion, verifying that the allocator gracefully handles Out-Of-Memory (OOM) states without crashing `wlroots`.

## User Approval Request
Please review this exhaustive structural blueprint. The changes are massive, but carefully scoped to protect the Wayland event loop. If this level of detail and direction is approved, Phase A execution will commence immediately.
