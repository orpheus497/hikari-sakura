# Hikari Project Briefing

*Last Updated:* 2026-08-19 17:55

## Current Status

- **Phase:** Phase 35 complete — XDG and Server Decoration lifecycle assertions in wlroots 0.20 have been resolved, preventing compositor crashes on terminal/browser launches.
- **Branch:** wlroots-0.20
- **Overall progress:** Phase 35 fixes: Deferred `wlr_xdg_toplevel_decoration_v1_set_mode` until `initial_commit` in `src/decoration.c`. Fixed listener leak on `wlr_server_decoration` destruction in `src/server.c` and `src/view.c`.
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** User compilation of the fixes (sudo required) and subsequent verification. Remaining queue: user-run Phase 19 diagnostics (eDP-1 swapchain), runtime-blocked verifications (P2-14, PAM, layer-client spot check), optional hygiene.
- **Blockers:** (1) root-owned build artifacts (`main.o`, `hikari`) block standard local compilation; user must run `sudo make clean && sudo make install`. (2) eDP-1 scanout swapchain test failure — Mesa/EGL/GBM ↔ drm-kmod layer.

## Remaining Work

- **CRITICAL:** Resolve the eDP-1 scanout swapchain failure (run Phase 19 diagnostics matrix). Phase 28 guard eliminates the spurious disable commit; underlying GBM/drm-kmod issue may still require Mesa/kernel-side action.
- PAM unlocker live verification (setuid 4555 path; blocked on runtime bring-up).
- P2-14 `current_mode` retention across output disable/enable (blocked on runtime bring-up).
- Layer-client spot check (blocked on runtime bring-up).
- TC-FORMAT-01 `clang-format` compliance run.
- Optional hygiene (pending user direction): comment-header rollout to non-compliant `src/` files; cosmetic enum-compare warnings (`src/dnd_mode.c:63`, `src/move_mode.c:78`).

*Granular tasks are tracked in `.devdocs/TODOS.md`.*
