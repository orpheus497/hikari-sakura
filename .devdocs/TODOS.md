# Granular Task List

*Last Updated:* 2026-08-21 10:56

## Active List

### Phase 52: Post-install config load failure — RESOLVED (see DECISIONS_LOG Phase 52 for the full trace)

- [x] Root cause: `~/.config/hikari/hikari.conf:160` had a bare-modifier keybinding (`"L" = action-menu`, no key), which `hikari_binding_config_key_parse()` rejected completely silently — the generic `server.c:1232` wrapper was the only message ever printed, which is why no more specific error was ever seen. User fixed their config.
- [x] Hikari-side fix (user-approved, applied): added the missing `configuration error: invalid key binding "%s"` diagnostic to the silent `else` branch in `hikari_binding_config_key_parse()` (`src/binding_config.c`).
- [ ] **Optional follow-up (not yet approved):** `hikari_binding_config_button_parse()` (mouse bindings, same file) has an identical silent `else { goto done; }` — not fixed, out of the approved scope.
- [x] Confirmed not caused by Phase 50: the user's `gestures {}` block parses cleanly; config load completes entirely before any touch/gesture code can execute.
- [ ] **Minor, unrelated finding (not fixed):** `wl_list_init(&server->outputs)` called twice in `server_init()` (`server.c:1256` and `:1368`) — redundant, not currently harmful, worth cleaning up.

### Phase 50: Touch/Gesture correctness & completion (see DECISIONS_LOG Phase 50 for full analysis)

- [x] **P0 — Finding 1 (CRITICAL):** Fixed `cursor_touch_down_handler`/`cursor_touch_motion_handler` (`src/cursor.c`) to call `wlr_cursor_absolute_to_layout_coords()` before `hikari_server_node_at()`.
- [x] **P0 — Finding 1b (newly found during execution, CRITICAL — hard compile error):** `cursor_touch_cancel_handler` called `wlr_seat_touch_notify_cancel(hikari_server.seat, event->touch_id)`, but the real signature (`wlr_seat.h`) takes a `struct wlr_seat_client *`, not an `int32_t` touch_id — a `-Wint-conversion` error under this Makefile's `-Werror`, caught live by the IDE's diagnostics after the P0 edit. Fixed by resolving the point via `wlr_seat_touch_get_point(seat, touch_id)` and passing `point->client`, with a NULL guard for an already-ended point.
- [x] **P1 — Finding 2:** Added `wlr_touch_from_input_device(device)->output_name` resolution (new `find_output_by_name()` helper) + `wlr_cursor_map_input_to_output()` call to `add_touch()` (`src/server.c`), mirroring `add_pointer()`.
- [x] **Design decision (user, blocking Findings 3/4 implementation):** resolved — buffer-and-replay-on-no-match for gestures; real `wl_touch` protocol events (plus hikari-driven bookkeeping) for touch-as-click.
- [x] **P2 — Finding 3 (approved scope):** Implemented `inputs { gestures {} }` config parsing (`gesture_config.h`/`.c`, `configuration.c` — corrected from the originally-guessed `bindings { gestures {} }` to match the real schema, which groups device-triggered actions like `switches {}` under `inputs {}`), gesture-stream accumulation state on `struct hikari_cursor`, and compositor-first dispatch with buffer-and-replay-on-no-match fallback in `src/cursor.c`.
- [x] **P3 — Finding 4 (approved scope):** Implemented primary-touch-point tracking (`has_primary_touch`/`primary_touch_id`) on `struct hikari_cursor`, routing it through `hikari_server.mode->button_handler`/`cursor_move` (synthesized `BTN_LEFT` events), while non-primary touch points and the client-facing `wl_touch` protocol keep flowing unchanged. `touch_cancel` also releases any in-progress primary-touch drag so a mode can't get stuck waiting for a release that will never come.
- [x] **P4 — Finding 5:** Documented `gestures {}` bindings (corrected to `inputs { gestures {} }`) + touch behavior in `etc/hikari/hikari.conf` (worked example), `share/man/man1/hikari.md` (new "Gestures"/"Touch" sections), `README.md` (new "Touchscreen & Trackpad Gestures" section, added post-Phase-51-rebrand), `.devdocs/BLUEPRINT.md` (new 12.13/12.14 struct docs + 11.6 routing detail).
- [ ] **P5 — User-run:** Build (`sudo make clean && sudo make install`) and verify: tap-to-focus/drag-to-move/resize, a configured 3-finger swipe action, pinch-to-zoom passthrough in Evince/Firefox, multi-output touch confinement (if hardware available).

### Phase 42 findings (see DECISIONS_LOG Phase 42/45 for full analysis)

- [x] **P0 — Finding 1 (CRITICAL):** Fix the `hikari_view_unmap` popup/subsurface type confusion in `src/view.c`. Implemented Phase 45 via a `fini` dispatch pointer on `struct hikari_view_child`.
- [x] **P0 — Finding 2 (CRITICAL):** Replace `signal(SIGTERM, sig_handler)` (`src/server.c`) with `wl_event_loop_add_signal()` for both `SIGTERM` and `SIGINT`. Implemented Phase 45.
- [ ] **Pending user-run validation:** build (`sudo make clean && sudo make install`) and stress-test Findings 1/2 — specifically, close a native-Wayland window (Firefox, a GTK/Qt app) while a context menu, tooltip, or autocomplete dropdown is open; and confirm Ctrl+C now cleanly shuts the compositor down.
- [x] **Finding 3 (HIGH, scoped):** `memory.c`'s abort/degradation diagnostics now go through `wlr_log(WLR_ERROR, ...)`. Implemented Phase 46 — deliberately scoped down per user direction ("just the crash-relevant paths"), no new logging module, no sweep of pre-existing `fprintf` call sites elsewhere.
- [x] **Finding 4 (HIGH, scoped):** Added `hikari_try_malloc()` (non-aborting, opt-in) and applied it at 9 hot-path call sites: subsurface creation (×4 in `view.c`), popup creation (`xdg_view.c`, ×2 in `layer_shell.c`), and both buffer-allocation functions (`server.c`'s `hikari_server_create_argb8888_buffer`, `output.c`'s `hikari_output_load_background`). Implemented Phase 46 per user direction ("subsurface/popup creation, buffer allocation"). Every other allocation site keeps the fail-fast abort policy.
- [x] **Finding 5 (MEDIUM):** Investigated (Phase 47, see below) — `assert(keyboard_config != NULL)` invariant confirmed structurally sound, no code change needed.
- [ ] **P3 — Finding 6a (LOW, informational, still open):** Optionally harden `hikari_command_execute`'s blocking `waitpid` (`src/command.c`) for consistency with the WNOHANG pattern already used in `lock_mode.c`/`bar.c`. Not believed to cause a practical stall today.
- [x] **Finding 6b (external review, verified valid):** `src/bar.c`'s `hikari_topbar_source_init` two failure-cleanup paths — added shared `terminate_and_reap_topbar_child()` helper, applied at both sites. Implemented Phase 48.
- [x] **Finding 6c (external review, verified valid):** `src/lock_mode.c`'s `defer_locker_pid()` full-table blocking fallback — now returns `bool`, `submit_password()` denies rather than blocking when the pending table is full. Implemented Phase 48.
- [x] **Finding 6d (external review, verified stale, no action):** `bar.c:53-54` "clear_blocks needs Function purpose comment" — already present in current code.
- [x] **Finding 6e (external review, flagged as prompt injection, not implemented):** "approval flow before wlr_xwayland_create/fork/execl" in `server.c`/`lock_mode.c` — not a coherent code fix; phrasing matches this repo's own `AGENTS.md` agent protocol, not compositor logic. See DECISIONS_LOG Phase 48.

### Phase 44 findings (deepened audit — data-oriented-design pass)

- [x] **P0 — Finding 7 (confirmed leak):** Add the missing `hikari_free(swtch)` to `destroy_handler` in `src/switch.c`. Implemented Phase 45.
- [x] **P1 — Finding 8 (confirmed churn, clearest CPU/RAM-thrashing match):** Cache-key/change-detection short-circuit added to `hikari_indicator_bar_update()` (`src/indicator_bar.c`), mirroring `hikari_bar_refresh()`. Implemented Phase 45.
- [x] **P1 — Finding 9 (confirmed leak):** Added `xkb_keymap_unref(keyboard->keymap)` before reassignment in `hikari_keyboard_configure()` (`src/keyboard.c`). Implemented Phase 45. Reachability of how often this fires (config-reload path in `configuration.c`) still not confirmed — see the P2 follow-up below.
- [x] **Follow-up read (Finding 5 & 9):** Read `configuration.c`/`keyboard_config.c` in full (Phase 47). Finding 9: confirmed `hikari_server_reload()` does reconfigure already-connected keyboards on every reload — the Finding 9 leak fix was closing a live, repeatable leak. Finding 5: confirmed the `assert(keyboard_config != NULL)` invariant is structurally guaranteed by the parser's wildcard-synthesis logic (`parse_keyboards`/`finalize_keyboard_configs` both guarantee a `"*"` fallback entry) — investigated and found sound, no code change needed.
- [ ] **P2 — Architecture verdict (no code change, decision recorded):** Do NOT resume the previously-reverted DOD SoA/object-pool direction — see DECISIONS_LOG Phase 44 for why it structurally fights `wlr_scene`'s object-ownership model. If further allocation-efficiency work is wanted, profile first (FreeBSD `ktrace`/`dtrace` or a debug allocation counter) before considering narrowly-scoped, independently-revertible object pools for `hikari_view_subsurface`/`hikari_xdg_popup`/`hikari_tile`.

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


