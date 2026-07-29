# Strategic Implementation Plans & Architecture Roadmap

*Last Updated:* 2026-07-29 03:44

---

## 1. Master Strategic Plan: FreeBSD Modernization & Hybrid DOD (Object Pools)

### Context & Goals
`hikari` is a stacking Wayland compositor written in C. To modernize the compositor for FreeBSD releases (13.x/14.x+) and contemporary hardware architecture:
1. **FreeBSD Exclusivity:** All Linux compatibility layers (`#ifdef __linux__`) must be permanently excised. The compositor will rely solely on the FreeBSD `evdev` translation layer via `libepoll-shim`, natively supporting `/dev/input/eventX` routing on FreeBSD.
2. **Hybrid Data-Oriented Design (DOD):** Dynamic allocation of core structures (`hikari_view`, `hikari_sheet`, `hikari_workspace`) via standard `malloc` fragments memory across the heap. This prevents cache lines from effectively loading multiple structures. We will migrate to a **Contiguous Object Pool (Slab) Architecture**. We will pre-allocate bulk contiguous memory slabs for core structs at server startup, while maintaining their `wl_list` pointers so that Wayland (`wlroots`) event loop integration remains perfectly intact.
3. **Documentation Integrity:** The entire codebase must be annotated with exact line-by-line documentation prefixes per [AGENTS.md](file:///home/droid/Documents/Projects/hikari/AGENTS.md).

---

## 2. Phase Breakdown & Execution Strategy

### Phase A: FreeBSD System Exclusivity Adaptation (IN PROGRESS)
- **Goal:** Unify the codebase around the FreeBSD API surface.
- **Actions:**
  - Modernize `Makefile` with strict `bmake` semantics and `pkg-config --cflags --libs epoll-shim`.
  - Strip `<linux/input-event-codes.h>` globally, substituting strictly with `<dev/evdev/input-event-codes.h>`.
  - Validate OpenPAM authentication flow securely in the setuid environment.

### Phase B: Object Pool Allocator Engineering (PLANNED)
- **Goal:** Develop a custom, zero-fragmentation memory allocator.
- **Actions:**
  - Create `include/hikari/pool.h` and `src/pool.c`.
  - Implement a contiguous memory slab (`void *buffer`) initialized at startup with fixed `capacity` and `item_size`.
  - Provide $O(1)$ allocation/deallocation via a `free_list` stack/bitmask.

### Phase C: Memory-Optimized Hybrid DOD Refactoring (PLANNED)
- **Goal:** Migrate `hikari` core loops to utilize the custom Object Pool.
- **Actions:**
  - Embed `view_pool`, `sheet_pool`, `workspace_pool`, and `tile_pool` into `hikari_server`.
  - Refactor all dynamic allocations in `src/xdg_view.c`, `src/xwayland_view.c`, `src/sheet.c`, and `src/workspace.c` to use `hikari_pool_alloc`.
  - Retain existing `wl_list` macro operations so that `wlroots` logic and signal propagation does not segfault, while gaining the performance of contiguous memory.

### Phase D: Build System & Compilation Verification (PLANNED)
- **Goal:** Prove the mathematical exactness of the DOD refactoring and FreeBSD compilation.
- **Actions:**
  - Execute strict FreeBSD compilation runs.
  - Implement runtime memory audits to calculate byte deltas between struct instances, proving cache-line optimization.
  - Test `hikari-unlocker` PAM authentication under production environments.

### Phase E: Codebase Documentation Compliance (ONGOING)
- **Goal:** Annotate every C source and header file with mandatory comment prefixes according to `AGENTS.md`.
- **Actions:** Completed `main.c`, `hikari_unlocker.c`, `include/hikari/server.h`, `include/hikari/view.h`. Pending remaining files.
