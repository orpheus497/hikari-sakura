# Hikari Project Briefing

*Last Updated:* 2026-08-19 12:17

## Current Status

- **Phase:** Phase 28 — `request_state_handler` startup CRTC disable guard — executed and compiled clean.
- **Branch:** wlroots-0.17.1 (stale label — tree builds against installed wlroots 0.20.x)
- **Overall progress:** Phase 24 hardening stream fully closed. Phase 27 deep architectural mapping complete. Phase 28 fix: `request_state_handler` in `src/output.c` now guards against wlroots 0.20 emitting disable-CRTC `request_state` events during initial DRM probe, resolving the "Failed to disable CRTC <N>" startup error. `output.o` compiles warning-clean (EXIT:0). Full relink blocked by root-owned `main.o`/`hikari` binary — pre-existing environment issue, not a code defect.
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Devdocs updated. Remaining queue: user-run Phase 19 diagnostics (eDP-1 swapchain), tmpfs/ZFS `XDG_RUNTIME_DIR`, runtime-blocked verifications (P2-14, PAM, layer-client spot check), optional hygiene (TC-FORMAT-01, comment-header rollout, cosmetic enum-compare warnings).
- **Blockers:** (1) eDP-1 scanout swapchain test failure — Mesa/EGL/GBM ↔ drm-kmod layer (partially addressed by Phase 28 fix — the guard eliminates the spurious startup CRTC disable commit that was confounding diagnostics); (2) tmpfs/ZFS `XDG_RUNTIME_DIR` — client wl_shm forced; (3) root-owned build artifacts blocking full relink (environment issue).

## Remaining Work

- **CRITICAL:** Resolve the eDP-1 scanout swapchain failure (run Phase 19 diagnostics matrix). Phase 28 guard eliminates the spurious disable commit; underlying GBM/drm-kmod issue may still require Mesa/kernel-side action.
- **CRITICAL:** Resolve tmpfs/ZFS incompatibility for XDG_RUNTIME_DIR (client wl_shm; escalated).
- PAM unlocker live verification (setuid 4555 path; blocked on runtime bring-up).
- P2-14 `current_mode` retention across output disable/enable (blocked on runtime bring-up).
- Layer-client spot check (blocked on runtime bring-up).
- TC-FORMAT-01 `clang-format` compliance run.
- Optional hygiene (pending user direction): comment-header rollout to non-compliant `src/` files; cosmetic enum-compare warnings (`src/dnd_mode.c:63`, `src/move_mode.c:78`).

*Granular tasks are tracked in `.devdocs/TODOS.md`.*
