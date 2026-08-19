# Hikari Project Briefing

*Last Updated:* 2026-08-19 20:26

## Current Status

- **Phase:** Phase 36 in progress — XWayland unmanaged view listener wiring UB fix (P1), VT/session active guard on frame/state handlers (P2), and layer-shell popup depth guard (P3) implemented; awaiting build verification.
- **Branch:** wlroots-0.20
- **Overall progress:** All code changes for Phase 36 are written. Trial build running. Root-owned artifacts from prior sudo build may require `sudo make clean && sudo make install` for the user's full verification run.
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Build verification of Phase 36 changes; user to `sudo make clean && sudo make install` and run smoke tests (XWayland context menus/tooltips, VT switch, waybar).
- **Blockers:** (1) Root-owned build artifacts block standard local `make`; user must run `sudo make clean && sudo make install`. (2) eDP-1 scanout swapchain test failure — Mesa/EGL/GBM ↔ drm-kmod layer (pre-existing, not Phase 36 scope).

## Remaining Work

- **CRITICAL (pre-existing):** Resolve the eDP-1 scanout swapchain failure (run Phase 19 diagnostics matrix). Phase 28 guard eliminates the spurious disable commit; underlying GBM/drm-kmod issue may still require Mesa/kernel-side action.
- **Phase 36 verification (user-run):** `sudo make clean && sudo make install`; smoke test XWayland override-redirect windows (context menus, tooltips), VT switch, waybar with sub-menus.
- PAM unlocker live verification (setuid 4555 path; blocked on runtime bring-up).
- P2-14 `current_mode` retention across output disable/enable (blocked on runtime bring-up).
- Layer-client spot check (blocked on runtime bring-up).
- TC-FORMAT-01 `clang-format` compliance run.
- Optional hygiene (pending user direction): comment-header rollout to non-compliant `src/` files; cosmetic enum-compare warnings (`src/dnd_mode.c:63`, `src/move_mode.c:78`).

*Granular tasks are tracked in `.devdocs/TODOS.md`.*
