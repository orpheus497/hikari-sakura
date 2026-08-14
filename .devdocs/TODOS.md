# Granular Task List

*Last Updated:* 2026-08-13 19:08

## Active List

- [ ] **Runtime diagnostics (user-run, Phase 19 matrix):** (1) `make DEBUG=YES` rebuild + rerun `./start-hikari.sh` — wlroots debug log names the exact swapchain failure step (release builds compile out `wlr_log_init(WLR_DEBUG)`, `main.c:236`); (2) `kldstat` + `dmesg | grep -Ei 'drm|i915|amdgpu'`; (3) `pkg info -x mesa drm-kmod wlroots` (mesa-dri coherence); (4) `ls -l /dev/dri`; (5) `drm_info` (IN_FORMATS for eDP-1 planes); (6) `eglinfo -B` (EGL_EXT_device_drm presence); (7) `LIBGL_DEBUG=verbose ./start-hikari.sh`.
- [ ] **Resolve eDP-1 scanout swapchain failure (blocked on the diagnostics above):** expected Mesa/GBM/drm-kmod layer (hypotheses H1/H2/H3 — DECISIONS_LOG Phase 19); not a hikari code defect.
- [ ] **tmpfs/ZFS Resolution (P0, escalated):** Implement tmpfs mount for `XDG_RUNTIME_DIR` — `/var/run/user/1001` is on ZFS, `posix_fallocate()` fails there. Escalated because the EGL device-query failure removes dmabuf device feedback, forcing clients onto wl_shm. Recommended: tmpfs at `/var/run/user` via `/etc/fstab` or `sudo zfs set canmount=noauto zroot/tmp`.
- [ ] **P2-14 runtime verification (blocked on runtime bring-up):** confirm wlroots retains `current_mode` across output disable/enable — `hikari_output_enable()` re-enables without setting a mode (`src/output.c`); if the mode was cleared on disable, lock-mode Ctrl+C leaves outputs dark. (Salvaged from the retired investigation report, Phase 22.)
- [ ] **PAM Verification (blocked on runtime bring-up):** Verify `hikari-unlocker` works correctly with OpenPAM setuid 4555 on a live FreeBSD Wayland session.
- [ ] **Layer-client spot check (blocked on runtime bring-up):** run a panel/bar (or swaybg) with a `WITH_LAYERSHELL=YES` build to exercise the new scene attachment.
- [ ] **TC-FORMAT-01:** Run `clang-format` compliance check against `.clang-format` rules.
- [ ] **Comment-header rollout (optional, deferred):** 48 of 55 `src/` files lack the `[COMMENT] Script function and purpose:` header mandated by AGENTS.md (Phase 8 claim amended 2026-08-13). Rollout awaits user direction.
- [ ] **Cosmetic:** silence enum-compare warnings at `src/dnd_mode.c:63` and `src/move_mode.c:78` (value-identical constants; harmless).

## Recently Completed

- [x] **Phase 26 P2/P3 hardening batch (2026-08-13):** CSD granular damage — whole-output early-outs removed from `hikari_view_damage_whole`/`hikari_view_damage_surface`; CSD main surface damaged by buffer extents (`src/view.c`). Fail-fast allocation policy — `hikari_malloc`/`hikari_calloc` sized diagnostic + `abort()` on NULL (`src/memory.c`, never-NULL contract in `include/hikari/memory.h`). Changelog `wloots`→`wlroots` (`CHANGELOG.md`). TC-BUILD-01/02 pass, 0 errors; edited files warning-clean. Phase 24 hardening stream closed at 7/7.

- [x] **Phase 25 P0/P1 hardening batch (2026-08-13):** unknown `outputs` keys now fail the parse (`src/configuration.c`); `parse_switches` UCL iterator freed (`src/configuration.c`); lock-helper child `_exit(EXIT_FAILURE)` + stderr diagnostic after failed `execl` (`src/lock_mode.c`); loud output-commit diagnostic naming the output (`src/output.c`, explicit `<stdio.h>`). Also closes the older "output-commit failure diagnostic (optional hardening)" item. TC-BUILD-01/02 pass, 0 errors; edited files warning-clean.
- [x] **Review-findings batch (2026-08-13, Phase 23):** version.h FORCE + atomic regeneration; strtol end-pointer rejection for numeric mouse bindings; layer popup offsets via flat `base->geometry`; xwayland `wlr_scene_tree_create` NULL bailout; 17 function-purpose comment headers. 4 further review findings verified stale and skipped (evidence in SESSION_HANDOFF Phase 23).
- [x] **Wallpaper asset restored (2026-08-13, Phase 23):** `share/backgrounds/hikari/hikari_wallpaper.png` generated (1920x1080 8-bit RGB gradient on the config's `0x282C34` background) and now installed unconditionally by `make install`.
- [x] **Devdocs consolidation (2026-08-13, Phase 22):** archived runtime investigation content redistributed; launcher analysis → BLUEPRINT §6, corrected eDP-1 analysis → BLUEPRINT §5, P2-14 → active list above, P2-15 → BLUEPRINT known limitations. 7-file AGENTS.md structure restored.
- [x] **Launcher-architecture analysis & report validity audit (2026-08-13, Phase 21):** User question (why `start-hikari` *and* `hikari`; shouldn't the compositor natively resolve dbus/seatd/PAM/XDG/portals) answered with file:line evidence — consolidated into BLUEPRINT §6 in Phase 22. Residual open set tracked above (P2-14 added from the archived investigation).
- [x] **Live TTY runtime test (2026-08-13, Phase 19):** Executed via `start-hikari` with seatd up. Session → backend start → renderer → allocator → connector probe all verified live; startup halts at the eDP-1 scanout swapchain test (Mesa/GBM/drm-kmod layer — not hikari). Full triage: SESSION_HANDOFF Phase 19.
- [x] **Phase 18b remediation (2026-08-13):** P0-1 xkb symbol, P0-2 backend-start check, P0-3 headless-create argument, P0-4 default config + install guards, P1-5 keymap type tag, P1-6 numeric mouse keycode, P1-7 layer-shell scene attach, P2-8 global list re-init, P2-9/P2-10 diagnostics, P2-11 dead focus params, P2-12 version.h rule, P2-13 comment prefixes — all applied and annotated (register: SESSION_HANDOFF Phase 18b; standalone report retired in Phase 22).
- [x] **Build-discovered fixes (2026-08-13):** P1-16 popup geometry → `popup->current.geometry`; P1-17 `xcb_size_hints_t`; P1-18 xwayland associate/dissociate lifecycle.
- [x] **TC-BUILD-01 revalidated (2026-08-13):** `make clean && make` — 0 errors, both binaries linked.
- [x] **TC-BUILD-02 (2026-08-13):** full-feature clean build (`WITH_XWAYLAND/LAYERSHELL/SCREENCOPY/GAMMACONTROL/VIRTUAL_INPUT=YES`) — 0 errors, clean link. First time feature configs compiled in this tree.

- [x] **API check closed (2026-08-13):** `wlr_output_effective_resolution()` at `src/layer_shell.c:177` compiles and links against installed wlroots 0.20 — existence verified by the successful user build.
- [x] **Manual cleanup closed (2026-08-13):** no `.core` files exist in the repo root — item obsolete.
- [x] **SESSION_HANDOFF table pipe escaping (Phase 17):** Escaped literal pipes in Phase 16 Modified Files table; markdownlint column miscount resolved.
- [x] **README tmpfs troubleshooting clarity (Phase 17):** `zfs` result now means `/tmp` still ZFS-backed; users re-check all steps incl. fstab + reboot.
- [x] **Build validation (2026-08-13):** User-confirmed `make` builds clean on the FreeBSD target. (Agent-sandbox `bmake` failure was a missing `libucl` pkg-config entry — environment artifact.)
- [x] **start-hikari.sh SCRIPT_DIR resolution (Phase 15):** Added `SCRIPT_DIR` derivation for sibling binary lookup. Three-tier: `${SCRIPT_DIR}/hikari` → PATH → `./hikari`.
- [x] **ZFS detection in start-hikari.sh (Phase 12):** Warns user when `XDG_RUNTIME_DIR` is on ZFS.
- [x] **XDG_RUNTIME_DIR validation (Phase 12):** Checks ownership (current user) and permissions (0700) for all paths.
- [x] **PAM config fix (Phase 12):** `auth include passwd` → `auth include system` on FreeBSD.
- [x] **Non-blocking PAM I/O (Phase 12):** `read()` → `wl_event_loop_add_fd()` in lock_mode.c.
- [x] **BUG-1 move_resize_view dx fix (Phase 14):** `+ dy` → `+ dx` for lx calculation.
- [x] **BUG-2 outputs_disabled init (Phase 14):** Added init in `hikari_lock_mode_init()` and reset in `cancel()`.
- [x] **BUG-3 command.c waitpid fix (Phase 14):** Fixed inverted errno check.
- [x] **Security: explicit_bzero (Phase 14):** Replaced `memset` for password buffer zeroing.
- [x] **5 missing listener cleanups (Phase 14):** Added `wl_list_remove()` in `hikari_server_stop()`.
- [x] **Dead code removal (Phase 14):** render.h deleted, mode_handler removed, unused struct members cleaned.
- [x] **Duplicate includes removed (Phase 13):** `server.c` had duplicate `wlr_data_device.h` and `wlr_seat.h`.
- [x] **Blocking wait→waitpid (Phase 13):** `lock_mode.c:154` — `wait()` replaced with `waitpid(-1, &status, WNOHANG)`.
- [x] **output->server init (Phase 13):** `output.c` — `output->server = &hikari_server` now set inside `hikari_output_init()`.
- [x] **Switch Toggle Bug (Phase 13):** Fixed cascading if→else if in `src/switch.c`.
- [x] **Output cairo surface check (Phase 13):** Fixed `output.c:85` checking wrong surface.
- [x] **main.c comment migration (Phase 13):** All `##` prefixes replaced with `[COMMENT]` format.
- [x] **FreeBSD Handbook cross-reference (Phase 13):** Ch.6 §6.1-6.4 verified — all requirements met.
