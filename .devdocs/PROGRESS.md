# Macro Progress Tracking

*Last Updated:* 2026-07-29 13:46

## Overall Project Completion: 87.5% (Based on simple average of 8 phases)

| Phase | Description | Status | Completion % |
|-------|-------------|--------|--------------|
| Phase 1 | Initialization & AI Workspace Setup | Completed | 100% |
| Phase 2 | Product Documentation (`docs/`) & File-by-File Deep Audit | Completed | 100% |
| Phase 3 | FreeBSD Native Header Adaptations & Toolchain Flags | Completed | 100% |
| Phase 4 | Data-Oriented Design (DOD) Struct-of-Arrays (SoA) Refactoring | Completed | 100% |
| Phase 5 | Wlroots 0.18+ / 0.20 API Migration & FreeBSD Dependencies | Completed | 100% |
| Phase 6 | FreeBSD Build Verification & Integration Testing | Planned | 0% |
| Phase 7 | Exhaustive Codebase C Header & AGENTS.md Audit | Completed | 100% |
| Phase 8 | DOD Struct-of-Arrays (SoA) View Table Refactoring | Completed | 100% |

## Detailed Milestones

- [x] Audit existing repository files, build system, and code structure
- [x] Create `.devdocs` workspace tracking structure (`AGENTS.md` compliance)
- [x] Create `docs/` folder with complete FreeBSD setup, requirements, wiring, and DOD documentation (`freebsd_requirements.md`, `architecture_wiring.md`, `data_oriented_design.md`, `modernization_guide.md`)
- [x] Modernize build system (`Makefile`) with FreeBSD `epoll-shim` flags
- [x] Perform exhaustive file-by-file audit of all 65 headers and 56 source files
- [x] Replace `<linux/input-event-codes.h>` with FreeBSD native `<dev/evdev/input-event-codes.h>` header in `src/binding_config.c`, `src/configuration.c`, and `src/pointer_config.c`
- [x] Wlroots 0.18+ / 0.20 Migration: Bumped `Makefile`, removed obsolete `session` from `hikari_server`, updated `wlr_backend_autocreate`, and verified API compatibility.
- [x] Wlroots 0.18+ Layer Shell & Scene Graph: Migrated layer_shell map/unmap to surface->events, fixed damage_ring, and handled pool alloc failures.
- [x] FreeBSD Dependencies Audit: Verified `epoll-shim`, `libucl`, and `hikari-unlocker.c` PAM setups.
- [x] Implement $O(1)$ vector sheet bitmask lookups in `src/sheet.c`
- [x] Implement contiguous render quad batching in `src/renderer.c`
- [x] Phase A: Header Dependency Verification (standard library types injected)
- [x] Phase B: Source Implementation Verification
- [x] Phase C: AGENTS.md Compliance Sweep (checked scripts, conditions, loops comments)
- [x] Compilation Fixes: Resolved `pkg-config` paths for `wlroots-0.20`, added `wlr_cursor.h` backport header for `wlroots` 0.20, and fixed missing `pool.o` linker object.
