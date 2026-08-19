# Hikari Project Briefing

*Last Updated:* 2026-08-19 16:48

## Current Status

- **Phase:** Phase 33 complete — Wayland clients crashing (posix_fallocate on ZFS) and background rendering issues in wlroots 0.20 have been resolved natively within Hikari.
- **Branch:** wlroots-0.20
- **Overall progress:** Phase 33 fix: `wlr_linux_dmabuf_v1_create_with_renderer` added to `server.c` to advertise hardware buffer protocol (bypassing SHM ZFS limitations). Custom `wlr_buffer` implementation added to `output.c` to handle CPU-rendered Cairo pixels for backgrounds, bypassing the `wlr_allocator` mapping limitations.
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** User verification of the fixes. Remaining queue: user-run Phase 19 diagnostics (eDP-1 swapchain), runtime-blocked verifications (P2-14, PAM, layer-client spot check), optional hygiene (TC-FORMAT-01, comment-header rollout, cosmetic enum-compare warnings).
- **Blockers:** (1) eDP-1 scanout swapchain test failure — Mesa/EGL/GBM ↔ drm-kmod layer (partially addressed by Phase 28 fix); (2) root-owned build artifacts blocking full relink (environment issue).

## Remaining Work

- **CRITICAL:** Resolve the eDP-1 scanout swapchain failure (run Phase 19 diagnostics matrix). Phase 28 guard eliminates the spurious disable commit; underlying GBM/drm-kmod issue may still require Mesa/kernel-side action.
- PAM unlocker live verification (setuid 4555 path; blocked on runtime bring-up).
- P2-14 `current_mode` retention across output disable/enable (blocked on runtime bring-up).
- Layer-client spot check (blocked on runtime bring-up).
- TC-FORMAT-01 `clang-format` compliance run.
- Optional hygiene (pending user direction): comment-header rollout to non-compliant `src/` files; cosmetic enum-compare warnings (`src/dnd_mode.c:63`, `src/move_mode.c:78`).

*Granular tasks are tracked in `.devdocs/TODOS.md`.*
