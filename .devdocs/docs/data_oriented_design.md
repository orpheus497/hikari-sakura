# Data-Oriented Design (DOD) Specifications

## Overview
This document outlines the Data-Oriented Design (DOD) transformation strategy for `hikari`. The goal of DOD is to organize core program state around CPU memory cache hierarchy performance (L1/L2/L3 cache lines), transformation pipelines, and contiguous memory access, rather than pointer-heavy Object-Oriented object graphs.

---

## 1. Motivation & Performance Rationale

Legacy C compositors frequently store objects (views, tiles, outputs, seats) as doubly-linked lists (`wl_list`) holding heap-allocated pointers (`struct hikari_view*`). 

### Performance Issues with Pointer Graph Structures:
* **Cache Line Invalidation:** Dereferencing pointers across non-contiguous heap allocations incurs L1/L2 cache misses.
* **Instruction Stalls:** Iterating view stacks during 120Hz-240Hz frame render updates stalls the CPU pipeline while waiting for main RAM fetches.
* **High Memory Overhead:** Linked list nodes (`wl_list` structs) add 16 bytes per node of metadata overhead.

---

## 2. Struct-of-Arrays (SoA) View Table Transformation

Instead of an Array-of-Structures (AoS) or linked list of individual heap allocations, view spatial geometry, visibility state, and sheet memberships are transformed into flat, cache-aligned Struct-of-Arrays (SoA):

### Legacy AoS Pointer Structure (Before):
```c
struct hikari_view {
  struct wlr_surface *surface;
  struct hikari_output *output;
  struct hikari_sheet *sheet;
  struct hikari_group *group;
  int x, y, width, height;
  bool is_hidden;
  bool is_floating;
  // ... pointer links ...
};
```

### Modern DOD Struct-of-Arrays Layout (After):
```c
#define HIKARI_MAX_VIEWS 512

/* 64-byte aligned spatial bounds array for SIMD/Vectorized geometry operations */
struct hikari_view_geometry_table {
  _Alignas(64) int x[HIKARI_MAX_VIEWS];
  int y[HIKARI_MAX_VIEWS];
  int width[HIKARI_MAX_VIEWS];
  int height[HIKARI_MAX_VIEWS];
};

/* Packed bitfield visibility table fits in a minimal cache footprint (2560 bytes) */
struct hikari_view_state_table {
  _Alignas(64) uint16_t sheet_mask[HIKARI_MAX_VIEWS]; /* Bitmask for sheets 0-9 */
  uint8_t flags[HIKARI_MAX_VIEWS];      /* 0x1: hidden, 0x2: floating, 0x4: max, 0x8: pinned */
  uint16_t group_id[HIKARI_MAX_VIEWS];
};
```

---

## 3. Constant-Time Visibility Checking

Visibility checking for a single view against the active sheet mask is optimized to a bitmask operation:

```c
/* Checks visibility of a single view in a constant-time operation. Evaluating all views still requires iterating the collection unless a packed index is used. */
static inline bool
hikari_view_is_visible_dod(uint16_t view_sheet_mask, uint16_t active_sheet_mask)
{
  /* Sheet 0 bit (0x1) is always visible; active sheet bit matches active sheet mask */
  return (view_sheet_mask & (active_sheet_mask | 0x0001)) != 0;
}
```

---

## 4. Contiguous Render Batching

During screen composition in `src/renderer.c`, quad geometry, border colors, and title bar textures are accumulated into a contiguous vertex/quad render buffer (`hikari_render_batch`) before issuing drawing commands:

```c
struct hikari_render_quad {
  float x, y, w, h;
  float color[4];
};

struct hikari_render_batch {
  struct hikari_render_quad quads[HIKARI_MAX_VIEWS * 4];
  size_t count;
};
```

### Benefits of Contiguous Render Batching:
1. Eliminates per-view rendering context state changes.
2. Enables single-pass GPU/Pixman buffer updates.
3. Reduces rendering overhead on low-power FreeBSD embedded or integrated GPU platforms by minimizing draw calls.

---

## 5. Contiguous Object Pool (Slab) Architecture

To complement the SoA structures, all dynamically generated core Wayland objects (`hikari_xdg_view`, `hikari_sheet`, `hikari_workspace`, `hikari_tile`) are allocated from pre-initialized Contiguous Object Pools (`struct hikari_pool`).

### Memory Contiguity & wl_list Integration:
* Pools (`view_pool`, `sheet_pool`, `workspace_pool`, `tile_pool`) are embedded inside `struct hikari_server`.
* All dynamic heap `malloc` calls in `src/view.c`, `src/workspace.c`, `src/sheet.c`, etc., are replaced with $O(1)$ `hikari_pool_alloc` lookups.
* This retains exact pointer references required for Wayland `wlroots` native `wl_list` integration, avoiding breaking signal events while still ensuring cache-friendly memory localization.
