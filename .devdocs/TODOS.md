# Granular Task List

*Last Updated:* 2026-08-20 (session context date; `date` not executed this session)

## Active List

### Phase 38 follow-up verification (newly unblocked — windows now render)

- [ ] **Border / indicator-frame placement:** confirm they draw at the correct position. Phase 38 switched both to parent-relative coordinates (`src/border.c`, `src/indicator_frame.c`); they were previously double-offset. Reasoned from wlroots scene semantics, not visually confirmed.
- [ ] **Window close teardown:** confirm closing a window neither crashes nor leaks. `destroy_handler` in `src/xdg_view.c` now destroys the hikari-owned scene tree; wlroots destroys `surface_tree` itself beforehand.
- [ ] **Lock/unlock end to end:** the unlocker is now launched via a compile-time absolute `HIKARI_UNLOCKER_PATH` (`${PREFIX}/bin/hikari-unlocker`) rather than a PATH lookup through `/bin/sh -c`. A helper installed anywhere else will silently fail to launch.
- [ ] **Multi-output indicator bar:** `hikari_indicator_bar_position` now adds `output->geometry` (`src/indicator_bar.c`); untested on an output not at layout origin (0,0).
- [ ] **XWayland override-redirect smoke test:** context menus, tooltips, dropdowns (Phase 36 associate/dissociate fix, still unverified at runtime).
- [ ] **VT switch verification:** `Ctrl+Alt+F2` → wait → `Ctrl+Alt+F1` (Phase 36 session guard, still unverified at runtime).

### Pre-existing backlog

- [ ] **Runtime diagnostics (user-run, Phase 19 matrix):** (1) `make DEBUG=YES` rebuild + rerun `./start-hikari.sh` for the full `WLR_DEBUG` log naming the exact swapchain failure step (note: since Phase 36, release builds do initialise logging at `WLR_INFO`, so fatal errors are no longer silenced — `DEBUG=YES` is still needed for the verbose trace); (2) `kldstat` + `dmesg | grep -Ei 'drm|i915|amdgpu'`; (3) `pkg info -x mesa drm-kmod wlroots` (mesa-dri coherence); (4) `ls -l /dev/dri`; (5) `drm_info` (IN_FORMATS for eDP-1 planes); (6) `eglinfo -B` (EGL_EXT_device_drm presence); (7) `LIBGL_DEBUG=verbose ./start-hikari.sh`.
- [ ] **Resolve eDP-1 scanout swapchain failure (blocked on the diagnostics above):** expected Mesa/GBM/drm-kmod layer (hypotheses H1/H2/H3 — DECISIONS_LOG Phase 19); not a hikari code defect.
- [ ] **tmpfs/ZFS Resolution (P0, escalated):** Implement tmpfs mount for `XDG_RUNTIME_DIR` — `/var/run/user/1001` is on ZFS, `posix_fallocate()` fails there. Escalated because the EGL device-query failure removes dmabuf device feedback, forcing clients onto wl_shm. Recommended: tmpfs at `/var/run/user` via `/etc/fstab` or `sudo zfs set canmount=noauto zroot/tmp`.
- [ ] **P2-14 runtime verification:** confirm wlroots retains `current_mode` across output disable/enable — `hikari_output_enable()` re-enables without setting a mode (`src/output.c`); if the mode was cleared on disable, lock-mode Ctrl+C leaves outputs dark. (Salvaged from the retired investigation report, Phase 22.)
- [ ] **PAM Verification:** Verify `hikari-unlocker` works correctly with OpenPAM setuid 4555 on a live FreeBSD Wayland session.
- [ ] **Layer-client spot check:** run a panel/bar (or swaybg) with a `WITH_LAYERSHELL=YES` build to exercise the new scene attachment.
- [ ] **TC-FORMAT-01:** Run `clang-format` compliance check against `.clang-format` rules.
- [ ] **Comment-header rollout (optional, deferred):** 48 of 55 `src/` files lack the `[COMMENT] Script function and purpose:` header mandated by AGENTS.md (Phase 8 claim amended 2026-08-13). Rollout awaits user direction.
- [ ] **Cosmetic:** silence enum-compare warnings at `src/dnd_mode.c:63` and `src/move_mode.c:78` (value-identical constants; harmless).


