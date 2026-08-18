# Hikari Project Briefing

*Last Updated:* 2026-08-13 19:08

## Current Status

- **Phase:** Phase 27 — Deep Architecture, Wiring, and Documentation Cross-Reference Audit (Iterative Refinement) completed. Exhaustive codebase trace mapped into `BLUEPRINT.md`.
- **Branch:** wlroots-0.17.1 (stale label — tree builds against installed wlroots 0.20.x)
- **Overall progress:** Phase 24 hardening stream fully closed (P0/P1 in Phase 25, P2/P3 in Phase 26) — the agent-side code backlog is clear. Deep mapping requested by user is complete. Remaining queue is runtime/environmental (user-run Phase 19 diagnostics; eDP-1 swapchain below hikari; tmpfs/ZFS `XDG_RUNTIME_DIR`), runtime-blocked verifications (P2-14, PAM, layer-client spot check), and optional hygiene (TC-FORMAT-01, comment-header rollout, cosmetic enum-compare warnings).
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Await the user-run Phase 19 diagnostics matrix (eDP-1 swapchain) and tmpfs/ZFS resolution; await user feedback on the updated `BLUEPRINT.md` deep trace; agent-side next actions (TC-FORMAT-01, optional comment-header rollout, cosmetic enum-compare warnings) pending user direction.
- **Blockers:** (1) eDP-1 scanout swapchain test failure — Mesa/EGL/GBM ↔ drm-kmod layer; (2) tmpfs/ZFS `XDG_RUNTIME_DIR` — client wl_shm, escalated because Error 1 kills dmabuf feedback and forces clients onto shm.



## Remaining Work

- **CRITICAL:** Resolve the eDP-1 scanout swapchain failure (diagnostics → fix; expected in the Mesa/GBM/drm-kmod layer, not this tree). The Phase 25 output-commit diagnostic now names the failed output on stderr.
- **CRITICAL:** Resolve tmpfs/ZFS incompatibility for XDG_RUNTIME_DIR (client wl_shm; escalated — Error 1 removed dmabuf feedback, forcing clients onto shm).
- PAM unlocker live verification (setuid 4555 path; blocked on runtime bring-up).
- P2-14 `current_mode` retention across output disable/enable (blocked on runtime bring-up).
- Layer-client spot check (blocked on runtime bring-up).
- TC-FORMAT-01 `clang-format` compliance run.
- Optional hygiene (pending user direction): comment-header rollout to non-compliant `src/` files; cosmetic enum-compare warnings (`src/dnd_mode.c:63`, `src/move_mode.c:78`).

*Granular tasks are tracked in `.devdocs/TODOS.md`.*
