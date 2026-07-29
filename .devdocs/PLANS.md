# Strategic Implementation Plans & Architecture Roadmap

*Last Updated:* 2026-07-29 03:44

---

## 1. Master Strategic Plan: FreeBSD Modernization & Hybrid DOD (Object Pools)

### Context & Goals
`hikari` is a stacking Wayland compositor written in C. To modernize the compositor for FreeBSD releases (13.x/14.x+) and contemporary hardware architecture:
1. **FreeBSD Exclusivity:** All Linux compatibility layers (`#ifdef __linux__`) must be permanently excised. The compositor will rely solely on the FreeBSD `evdev` translation layer via `libepoll-shim`, natively supporting `/dev/input/eventX` routing on FreeBSD.
2. **Hybrid Data-Oriented Design (DOD):** Dynamic allocation of core structures (`hikari_view`, `hikari_sheet`, `hikari_workspace`) via standard `malloc` fragments memory across the heap. This prevents cache lines from effectively loading multiple structures. We will migrate to a **Contiguous Object Pool (Slab) Architecture**. We will pre-allocate bulk contiguous memory slabs for core structs at server startup, while maintaining their `wl_list` pointers so that Wayland (`wlroots`) event loop integration remains perfectly intact.
3. **Documentation Integrity:** The entire codebase must be annotated with exact line-by-line documentation prefixes per [AGENTS.md](../AGENTS.md).

---

## 2. Phase Breakdown & Execution Strategy

### Phase A: FreeBSD System Exclusivity Adaptation (IN PROGRESS)
- **Goal:** Unify the codebase around the FreeBSD API surface.
- **Actions:**
  - Modernize `Makefile` with strict `bmake` semantics and `pkg-config --cflags --libs epoll-shim`.
  - Strip `<linux/input-event-codes.h>` globally, substituting strictly with `<dev/evdev/input-event-codes.h>`.
  - Validate OpenPAM authentication flow securely in the setuid environment.

### Phase B: Object Pool Allocator Engineering (COMPLETED)
- **Goal:** Develop a custom, zero-fragmentation memory allocator.
- **Actions:**
  - Create `include/hikari/pool.h` and `src/pool.c`.
  - Implement a contiguous memory slab (`void *buffer`) initialized at startup with fixed `capacity` and `item_size`.
  - Provide $O(1)$ allocation/deallocation via a `free_list` stack/bitmask.

### Phase C: Memory-Optimized Hybrid DOD Refactoring (COMPLETED)
- **Goal:** Migrate `hikari` core loops to utilize the custom Object Pool.
- **Actions:**
  - Embed `view_pool`, `sheet_pool`, `workspace_pool`, and `tile_pool` into `hikari_server`.
  - Refactor all dynamic allocations in `src/xdg_view.c`, `src/xwayland_view.c`, `src/sheet.c`, and `src/workspace.c` to use `hikari_pool_alloc`.
  - Retain existing `wl_list` macro operations so that `wlroots` logic and signal propagation does not segfault, while gaining the performance of contiguous memory.

### Phase D: Build System & Compilation Verification (COMPLETED)
- **Goal:** Prove the mathematical exactness of the DOD refactoring and FreeBSD compilation.
- **Actions:**
  - Execute strict FreeBSD compilation runs.
  - Implement runtime memory audits to calculate byte deltas between struct instances, proving cache-line optimization.
  - Test `hikari-unlocker` PAM authentication under production environments.

### Phase E: Codebase Documentation Compliance (ONGOING)
- **Goal:** Annotate every C source and header file with mandatory comment prefixes according to `AGENTS.md`.
- **Actions:** Completed `main.c`, `hikari_unlocker.c`, `include/hikari/server.h`, `include/hikari/view.h`. Pending remaining files.

---

## 3. Exhaustive File-by-File Audit & Resolution Plan

This plan was consolidated from the root directory to ensure `AGENTS.md` compliance. The sole purpose of this audit is to identify and resolve genuine C syntax errors (like missing standard library headers) and `AGENTS.md` compliance violations. 

This plan absolutely preserves the FreeBSD exclusivity of the project. No Wayland/wlroots IDE false-positives will be treated as real errors, and no FreeBSD-specific dependencies (`epoll-shim`, `bmake`, `<dev/evdev/input-event-codes.h>`) will be touched.

### Phase A: Header Dependency Verification (`include/hikari/*.h`)
Many IDE cascades occur because foundational headers define types but fail to `#include` the standard C library that provides them. I will examine every header to guarantee:
- `size_t` is backed by `<stddef.h>`.
- `uint32_t`, `uint16_t`, etc., are backed by `<stdint.h>`.
- `bool` is backed by `<stdbool.h>`.
- *Note:* If a header includes `<wayland-server-core.h>`, it inherits these types automatically. I will only patch headers that are isolated.

### Phase B: Source Implementation Verification (`src/*.c`)
I will read the source files matching the types above to ensure they include the correct standard libraries if they don't inherit them from their respective `hikari/*.h` headers. 
- *Completed:* `src/pool.c` (`<stddef.h>`) and `src/sheet.c` (`<stdint.h>`, `<stdbool.h>`) have already been fixed.
- *Pending Audit:* `src/binding_config.c`, `src/configuration.c`, `src/indicator_bar.c`, `src/memory.c`, `src/layer_shell.c`, etc.

### Phase C: `AGENTS.md` Compliance Sweep
I will parse every file modified during the DOD (Data-Oriented Design) refactoring and ensure the exact required prefixes exist:
- `##Script function and purpose: ...` at the top of the file.
- `##Class purpose: ...` before every struct.
- `##Function purpose: ...` before every function.
- `##Step purpose: ...`, `##Condition purpose: ...`, `##Loop purpose: ...` within the logic.
