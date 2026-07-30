# Architectural and Structural Decisions Log

*Note: Most recent entries are listed at the top.*

---

## [2026-07-31 04:59] Decision: wlr_scene_output Initialization Order
* **Context:** The wlr_scene migration resulted in a black screen and seemingly unresponsive inputs because `wlr_output_schedule_frame` was silently skipped on the first layout add due to a missing `scene_output` reference.
* **Decision:** In `hikari_output_init`, `wlr_scene_output_create` must be called *before* `wlr_output_layout_add`. This ensures `output->scene_output` is available when the layout emits `events.change`, which subsequently triggers background loading and first-frame damage scheduling.

## [2026-07-30 01:45] Decision: Preserve `xdg_surface->data = scene_tree` Convention
* **Context:** During code review, the assignment `xdg_surface->data = xdg_view->scene_tree` appeared to overwrite the `xdg_view` back-reference. Cross-referencing against tinywl (wlroots master) revealed this is the standard wlroots popup parenting convention: `xdg_surface->data` stores the scene_tree so `wlr_scene_xdg_surface_create` can find the parent scene node for popups via `parent->data`.
* **Decision:** Reverted the removal. `scene_tree->node.data = xdg_view` (for view lookup) and `xdg_surface->data = scene_tree` (for popup parenting) are on different objects and serve different purposes. Both are required.

---

## [2026-07-30 01:45] Decision: Track Manual Damage Ring Calls as Migration Debt
* **Context:** `hikari_output_damage_whole()` and `hikari_output_add_effective_surface_damage()` reach into `scene_output->damage_ring` directly. Verified against tinywl and labwc — neither uses manual damage ring calls when using `wlr_scene`. The scene graph handles damage tracking internally via `wlr_scene_output_commit`.
* **Decision:** Retain the manual calls for now (hikari is mid-migration, some damage sources may not be scene-managed). Tracked in TODOS.md for removal once all visual elements are scene graph nodes.

---

## [2026-07-29 15:16] Decision: Revert DOD SoA Tables and Object Pool Allocator
* **Context:** The custom object pool allocator and DOD SoA view state/geometry tables added complexity without proven benefit. The wlr_scene migration made the custom renderer (which DOD optimized for) obsolete.
* **Decision:** Removed pool.c/pool.h, reverted view flags to local struct field, removed all dod_id/view_state indirection.

---

## [2026-07-29 15:16] Decision: Migrate Rendering to wlr_scene Graph
* **Context:** wlroots 0.18+ provides wlr_scene for automatic damage tracking and composition, eliminating the need for manual renderer passes.
* **Decision:** Gutted renderer.c, migrated borders to wlr_scene_rect nodes, lock indicator and backgrounds to wlr_scene_buffer nodes. Scene graph handles damage tracking and output composition automatically.

---

## [2026-07-29 05:56] Decision: Continuous Quad Batch Rendering
* **Context:** The `struct hikari_render_batch` was introduced to allow O(1) cache-aligned bulk drawing of borders and indicator frames rather than context-switching matrices continuously.
* **Decision:** We inverted the rendering logic for borders and indicators. Instead of intersecting damage and immediately dispatching `wlr_render_quad_with_matrix`, we batch the geometry via `hikari_render_batch_add`. A unified flush `hikari_render_batch_flush` handles scissor intersection in a tighter loop, improving CPU utilization and decoupling the geometry scene pass from the rendering pipeline.

---

## [2026-07-29 05:51] Decision: Hybrid DOD Geometry and Flag Synchronization
* **Context:** The Phase 8 DOD refactoring required moving `view->flags` and `view->geometry` to cache-aligned SoA tables `view_state.flags` and `view_geometry`.
* **Decision:** To avoid rewriting the entire Wayland API interaction surface and risking cascading breakage, a Hybrid DOD approach is used. We retain `struct wlr_box geometry` inside `struct hikari_view`, but intercept mutations in `hikari_view_refresh_geometry` to synchronize `server.view_geometry`. We entirely replaced `view->flags` with `server.view_state.flags[view->dod_id]` which natively stores the `FLAG()` macro bits, allowing immediate O(1) checks.

---

## [2026-07-29 04:47] Decision: Sheet Pool Capacity & Array Contiguity
* **Context:** `hikari_workspace` allocates its 10 sheets simultaneously via `calloc(HIKARI_NR_OF_SHEETS, sizeof(struct hikari_sheet))` to hold them in a contiguous array format. Our Slab allocator traditionally manages single instances per block.
* **Decision:** To guarantee array contiguity without modifying `struct hikari_workspace` pointer mechanics or breaking `wl_list`, the `sheet_pool` `item_size` in `src/server.c` is initialized to `HIKARI_NR_OF_SHEETS * sizeof(struct hikari_sheet)`. A single allocation from the pool yields the contiguous block necessary for the workspace arrays.

---
## [2026-07-29 03:15] Decision: Data-Oriented Design (DOD) Orientation & FreeBSD Primary Target
* **Context:** The user requested modernizing the `hikari` Wayland compositor with primary focus on FreeBSD compatibility, thorough documentation inside `docs/`, and adoption of Data-Oriented Design (DOD) principles.
* **Decision:**
  1. Structure core data layouts (views, sheets, groups, outputs, tiles) into cache-aligned contiguous arrays / struct-of-arrays (SoA) where appropriate to minimize pointer chasing during render/layout loops.
  2. Isolate FreeBSD platform integration requirements (`evdev`, `epoll-shim`, `tmpfs` `/tmp` `posix_fallocate`, PAM unlocker, `seatd`) in system setup documentation (`.devdocs/docs/freebsd_requirements.md`) and build definitions (`Makefile`).
  3. Strict adherence to `AGENTS.md` operational cycle: Ask → Explain → Justify → Wait for Approval → Execute.

---

## [2026-07-29 03:15] Decision: Devdocs Separation of Concerns
* **Context:** `AGENTS.md` mandates absolute separation of AI tracking docs (`.devdocs/`) from product documentation (`docs/`) and code in root.
* **Decision:** Keep all operational and tracking files inside `.devdocs/` and user/product technical documentation inside `.devdocs/docs/`.
