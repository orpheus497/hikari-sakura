# Granular Task List

*Last Updated:* 2026-08-21 14:56

## Active List

### Phase 61: CRASH ROOT-CAUSED via core dump — NULL deref in `session_active_handler` (see DECISIONS_LOG Phase 61)

- [x] **Captured the first core dump in the project's history** (`/var/coredumps/hikari.27920.1001.core`, 14:51:15, signal 11). `gdb bt` puts frame #0 in `session_active_handler` at `+10`, reached from libseat -> wlroots -> `wl_signal_emit_mutable`.
- [x] **ROOT CAUSE:** `session_active_handler()` read `*(bool *)data`, but wlroots emits `session->events.active` with `data == NULL` (`backend/session/session.c:27` and `:33`). Unconditional NULL dereference on **every** VT switch / seat disable. **FIXED** — now reads `server->session->active`.
- [x] **Correction to Phases 53/57:** there were always two signatures. `/var/log/messages` shows SIGSEGV (11) at 13:59:15, 14:26:15, 14:51:15 alongside the SIGABRTs. The premise "SIGABRT, not SIGSEGV" that drove Phases 53-57 was half wrong.
- [x] **Correction to Phase 57's prediction:** the captured crash printed **no assertion message** and exited 139. Not a wlroots assert.
- [x] **Finding A FIXED — the incomplete refactor the user suspected.** `hikari_xwayland_unmanaged_evacuate()` updated `->workspace` but never moved `unmanaged_output_views` to the new output, unlike its managed twin `hikari_view_evacuate()` (`view.c:1610-1619`). On the *same* code path: wlroots destroys every output on session-deactivate, so `hikari_output_fini()` ran ~66ms before the segfault. Most probable source of the SIGABRT half.
- [x] Finding A hardening: link `wl_list_init`ed at init; remove-then-init in `unmap()`; `unmap()` idempotent; new `hikari_xwayland_unmanaged_detach()`; last-resort sweep in `hikari_output_fini()`; NULL-workspace safe-bails in map/unmap/commit.
- [x] **Finding B FIXED:** `override_redirect` was decided once at new-surface time and never revisited, so GTK/Chromium windows that flip the attribute (menus, tooltips, dropdowns) stayed the wrong view type for life. Added `hikari_server_adopt_xwayland_surface()` as the single adoption point, `set_override_redirect` listeners on both view types, and already-mapped adoption in both `_init`s. NULL-guarded `hikari_server.workspace`.
- [x] All five touched files pass `cc -fsyntax-only -Wall`.
- [ ] **P0 — USER-RUN, NEXT ACTION:** `sudo make clean && sudo make install`, then VT-switch away and back (`Ctrl+Alt+F<n>`). Previously fatal 100% of the time. Then open Firefox / VSCode / pavucontrol.
- [ ] **Step 3 (approved, not started):** always-on invariant checkers — Phase 55 item 1c (`view_assert_visible_consistent`) + Phase 54 W3 (`hikari_view_check_invariants`), as `wlr_log(WLR_ERROR)` + safe bail, NOT `assert()`. **Decision recorded:** `strings hikari` = 0 assert strings (release `-DNDEBUG`); `strings libwlroots-0.20.so` = 280. Every hikari assert written in the last 50 phases is dead code in the shipped binary.
- [ ] **Step 4 (approved, not started):** headless smoke test, with a VT-switch/output-destroy case holding a live override-redirect window, under `MALLOC_CONF=junk:true`.
- [ ] **NEW, unrelated, user-reported 14:51 — cursor pointer offset bug.** Pointer renders/hit-tests at an offset from its true position. Not yet investigated. Suspect the top bar's `usable_area` reservation vs. cursor layout coordinates.
- [ ] **NEW — orphaned `hikari-topbar` helpers.** Four alive at 14:56 from crashed sessions (`ps aux`). `bar.c` forks them; nothing reaps them when the compositor dies. Pre-existing, observed in Phase 53 too.
- [ ] **Pre-existing, now shown to be a live crash amplifier, not cosmetic:** `XDG_RUNTIME_DIR` on ZFS — `posix_fallocate()` unsupported, so `wl_shm` clients fail and disconnect abruptly. See the "tmpfs/ZFS Resolution" backlog item.

### Phase 58: Top-bar layout/opacity + always-on indicators — INVESTIGATED, awaiting approval (see DECISIONS_LOG Phase 58)

**Issue 1 — top bar (3 defects):**
- [x] **1a:** No centre lane exists. `struct hikari_bar_block` (`bar.h:23-29`) has only `align_right`; `hikari_bar_refresh()` (`bar.c:722-723`) computes exactly two origins. Centre is not representable.
- [x] **1b:** The "centred" WiFi/volume/backlight/battery group is an accident — a 400px spacer (`topbar.c:524`) with no `align`, followed by blocks with no `align`, all continuing the **left** lane. Not anchored to centre; would drift at another width.
- [x] **1c:** The clock is the only `"align":"right"` block (`topbar.c:550-552`) — occupying the slot the user wants for WiFi/etc.
- [x] **1d — opacity blocked by three hardcodes:** `hikari_color_convert()` forces `dst[3]=1.0` (`color.h:12`, so *no* config colour can be translucent); `bar.c:703` passes literal `1.0` discarding `bg[3]`; and the bar has no colour of its own, reusing `clear` (default `0x282C34` slate, `configuration.c:1878`).
- [x] Verified opacity is achievable: `CAIRO_FORMAT_ARGB32` (`bar.c:688`) + `DRM_FORMAT_ARGB8888` (`server.c:2252`), both premultiplied — they agree.
- [x] **Part A IMPLEMENTED (Phase 60):** `bool align_right` → `enum hikari_bar_align {LEFT,CENTER,RIGHT}`; `parse_line()` maps all three; measure pre-pass totals the centre run; `center_x = (width - center_width) / 2` added; layout loop dispatches per-run; cache key includes alignment. `topbar.c`: spacer deleted, clock → centre (emitted last), network/brightness/volume/battery → right in that reading order.
- [x] **Part B IMPLEMENTED (Phase 60, option 3 + bar colour):** alpha via quoted `"#RRGGBB"` / `"#RRGGBBAA"` strings (integers stay opaque RGB — a magnitude heuristic would misread any colour with red = 0); added `hikari_color_convert_rgba()`; shared `parse_color()` replaces nine duplicated blocks; `parse_hex_color()` in `bar.c` accepts 8 digits. **Plus** a dedicated `bar` colour — option 3 alone was insufficient because the bar painted from `clear`, so fading it would have faded the desktop too. `bar.c` now uses `bg[3]` and `CAIRO_OPERATOR_SOURCE` for the background paint.
- [x] Consumer audit: `indicator_bar.c`, `border.c`, `indicator_frame.c` were already alpha-correct (cairo RGBA / `wlr_scene_rect_set_color`), so no changes were needed there.
- [x] Docs updated: `etc/hikari/hikari.conf` + `share/man/man1/hikari.md` cover the `bar` key and the string colour form.
- [ ] **P0 — USER-RUN:** `sudo make clean && sudo make install`, then confirm bar layout and translucency. Note the shipped `hikari.conf` gained a `bar` key — a deployed `~/.config/hikari/hikari.conf` will keep the built-in default (`#282C34E6`) until the key is added there.

**Issue 2 — indicators shown permanently:**
- [x] Root cause: bars are scene nodes **created enabled and never disabled** (`indicator_bar.c:164-165`; no `set_enabled(false)` anywhere in the file, no show/hide API on `struct hikari_indicator_bar`), and `hikari_indicator_position()` (`indicator.c:161`) **unconditionally** calls `hikari_indicator_frame_show()`, reached from `hikari_indicator_update()` on every focus change (`workspace.c:451`).
- [x] The gate signal is present and correct — `update_mod_state()` (`keyboard.c:14-27`) tracks `WLR_MODIFIER_LOGO` into `mod_pressed`; `hikari_server_is_indicating()` returns it. **Nothing consumes it to hide.** `modifiers_handler()` (`normal_mode.c:168-176`) *shows* on both press and release; there is no hide branch.
- [x] Architectural cause: upstream gated indicator drawing per-frame in the render loop; the `wlr_scene` port turned that implicit gate into persistent nodes and never added the explicit enable/disable. Same shape as Phase 55 (`position()` carries a hidden visibility side effect).
- [x] **IMPLEMENTED (Phase 59):** added `visible` + show/hide to `hikari_indicator_bar`; `hikari_indicator_bar_update()` re-applies it to each recreated node; removed the unconditional `hikari_indicator_frame_show()` from `hikari_indicator_position()` (now geometry only); added `hikari_indicator_show/hide()`; `hikari_indicator_update()` re-asserts the Logo-key gate; `modifiers_handler()` drives show on press / hide on release. Five files, no diagnostics. **Not built or run.**
- [ ] **P0 — USER-RUN:** `sudo make clean && sudo make install`, then confirm the four indicator boxes and the frame appear only while Logo/Super is held.

### Phase 57: ROOT CAUSE — wlroots toplevel-listener assertion (see DECISIONS_LOG Phase 57)

- [x] **Found the actual crash.** `request_fullscreen` is registered on `xdg_surface->toplevel->events.request_fullscreen` but removed in `destroy_handler`, which is bound to `xdg_surface->events.destroy`. wlroots destroys the toplevel role object first and `destroy_xdg_toplevel()` asserts all ten toplevel signals have empty listener lists → `abort()`/SIGABRT on **every** window close, three lines before hikari's removal runs.
- [x] **Correction to Phase 53:** the binary is a release `-DNDEBUG` build (zero assert strings, no `!NDEBUG` printf markers) — hikari's assertions are compiled OUT. The aborting assertion is in `libwlroots-0.20.so`, which is built WITH assertions. Phase 53's "DEBUG=YES, asserts live" inference from `file` output was wrong and sent the investigation the wrong way.
- [x] **Correction re Phase 56:** the visibility refactor **was** in the binary that crashed at 13:46:57 (installed 13:46:10, session started 13:46:31). It fixed a real separate latent defect class but was not this crash.
- [x] Fix applied: `toplevel_destroy` listener on `xdg_toplevel->events.destroy` releases `request_fullscreen` and itself; `destroy_handler` removals kept as safe no-ops.
- [x] Audited the other assertions on the same paths (`set_title`, `new_popup`, never-mapped views) — all safe, no further gaps.
- [ ] **P0 — USER-RUN:** `sudo make clean && sudo make install`, then close a window.
- [ ] If any crash survives: `sudo mkdir -p /var/coredumps && sudo chmod 1777 /var/coredumps` first. Note SDDM writes session stderr to `~/.local/share/sddm/wayland-session.log` but **truncates it on next login** — copy it before logging back in.

### Phase 55: Root-cause architecture analysis + Single-Writer Visibility refactor (see DECISIONS_LOG Phase 55, PLANS.md item -6)

- [x] **Root cause named:** "is this view visible" is stored in **six** independent representations (hidden flag, `workspace->views`, `hikari_server.visible_views`, `group->visible_views`, `group->visible_server_groups` aggregate, scene-node enabled bit) with **no single writer**. Entry is split across `increase_group_visiblity()` + `place_visibly_above()`; exit is the single `hide()` — asymmetric. `hikari_view_lower()` re-implements the whole linkage a third time inline.
- [x] **Ownership consequence identified:** `detach_from_group()` frees the group but unlinks only `group_views`; `hikari_group_fini()` never unlinks `visible_server_groups`. Safe only via an unwritten, unasserted invariant. Violation ⇒ `hikari_server.visible_groups` holds a node in freed heap ⇒ delayed UAF matching the observed SIGABRT-with-no-core.
- [x] **A violating path exists in-tree:** `hikari_view_unmap()`'s `forced && !hidden` branch sets the hidden *flag* without performing the *transition*, then falls through to `detach_from_group()` — a simultaneous three-list + freed-group UAF. Either dead code or a guaranteed UAF; nothing in the codebase decides which.
- [x] Two further asymmetries confirmed: `decrease_group_visibility()` omits the `wl_list_init` after remove (violating the file's own convention); `hikari_view_init()` initialises 1 of 7 links.
- [x] **Ruled out** (do not re-investigate): popup/subsurface `fini` dispatch (sound); `pointer_gestures` NULL (created + guarded, `server.c:1370`); `gesture_binding_configs` uninitialised (`configuration.c:1876` inits unconditionally); double `wl_list_remove` of sheet/output links (benign); `activate()`/`resize()` on destroyed toplevel (guarded by `initialized`, cleared before destroy signal).
- [x] **Verdict:** bounded refactor warranted — "Single-Writer Visibility Transitions". Explicitly **not** the Phase 44 DOD/SoA rewrite (that rejection stands); allocation strategy is unchanged, only *who writes visibility state*.
- [x] **APPROVED and IMPLEMENTED** (Phase 56) — Steps 0-2 applied. No build run (IDE-only directive).
- [x] Step 0 — all 7 links initialised in `hikari_view_init()`; `wl_list_init(&group->visible_server_groups)` added to `hikari_group_init()` (**extra finding — the aggregate link was never initialised either**); `wl_list_remove` of it added to `hikari_group_fini()`; remove-then-init convention restored throughout.
- [x] Step 0b — decided **no change**: the two redundant `wl_list_init`s in `hikari_view_configure()` are harmless and unreachable-when-linked; deleting them unbuilt was needless risk. Not annotated in-code per AGENTS.md; rationale in DECISIONS_LOG Phase 56.
- [x] Step 1 — added `view_link_visible_at()` / `view_link_visible()` / `view_unlink_group_visible()` / `view_unlink_visible()` / `move_to_bottom()`. Deleted `increase_group_visiblity()`, `decrease_group_visibility()`, `hide()`, `place_visibly_above()`.
- [x] Step 2 — 11 call sites rewired, incl. **the root-cause fix in `hikari_view_unmap()`**: the branch that set the hidden flag without unlinking is gone. `hikari_view_lower()`'s seven inline remove/insert pairs replaced by the shared writer. `assert(wl_list_empty(&group->visible_views))` added to `detach_from_group()`.
- [ ] **Deferred: plan item 1c** — `view_assert_visible_consistent()` six-way checker. Held back deliberately: the user's binary is `DEBUG=YES` with asserts live and is already aborting; a new untested assert could manufacture a fresh abort mid-diagnosis. Add after the build is confirmed good.
- [ ] **P0 — USER-RUN, NEXT ACTION:** `sudo make clean && sudo make install`, then test closing a window and clicking a popup button. Nothing has been compiled or run.
- [ ] Step 3 — `BLUEPRINT.md` "View Visibility State" section.
- [ ] Step 4 — headless virtual-pointer smoke test under `MALLOC_CONF=junk:true`, wired to a `make` target.

### Phase 54: View-teardown ownership-graph hardening — PLAN ONLY, awaiting approval (see DECISIONS_LOG + PLANS.md item -5)

- [x] Measured the actual scope: 7 `wl_list` links + 6 owning pointers on `struct hikari_view`, 65 link/unlink/iterate sites, 5 teardown entry points across 3 view types converging on 2 hand-sequenced functions.
- [x] **Found a live latent defect while analysing:** `hikari_view_init()` initialises only `children` of the seven list links; four others hold `hikari_malloc` garbage until `hikari_view_map()` inserts them. Currently safe *only* because `hikari_view_fini()`'s `if (view->sheet != NULL)` guard skips them on the paths that can reach it — an unwritten, unchecked invariant guarding a `wl_list_remove()` through garbage pointers.
- [x] Confirmed the apparent double `wl_list_remove()` of `sheet_views`/`output_views` (unmap then fini) is benign, not a bug. Recorded so it isn't re-investigated.
- [x] Confirmed existing `assert()`s cover only scalar flags — none check any list link or owning pointer — and are stripped under `NDEBUG`.
- [x] Confirmed W4 feasibility: `HAVE_VIRTUAL_INPUT=1` (`Makefile:141`) + nested headless/X11 backends both already work, so unattended input-driven teardown testing is achievable.
- [ ] **AWAITING USER DECISION** on three questions before any code is written (scope/appetite, W3 release-build policy, W4 priority) — see PLANS.md item -5.
- [ ] W1 — Document the ownership graph in `BLUEPRINT.md` (docs only, zero risk).
- [ ] W2 — `wl_list_init()` all seven links in `hikari_view_init()` (~7 lines; closes the latent write above).
- [ ] W3 — Add `enum hikari_view_lifecycle` + `hikari_view_check_invariants()` called at every teardown boundary.
- [ ] W4 — Headless virtual-pointer smoke test for teardown sequences, run under `MALLOC_CONF=junk:true`, wired to a `make` target (replaces the `test.mk` stub).

### Phase 53: Close-window / popup-button crash — investigated, empirical repro needed (see DECISIONS_LOG Phase 53 for the full trace)

- [x] Re-audited the Phase 42/44/45 popup/subsurface `hikari_view_child.fini` dispatch fix against the real wlroots 0.20 signal-emission order (traced `wlr_compositor.c`, `wlr_xdg_surface.c`, `wlr_xdg_popup.c`, `wlr_layer_shell_v1.c` directly) — confirmed sound for both the client-unmap and full-destroy teardown orderings. Ruled out as the cause of the current crash.
- [x] Re-verified `hikari_view_unmap()`/`hikari_view_fini()`'s apparent double `wl_list_remove()` on `sheet_views`/`output_views` — confirmed a benign no-op (removing an already-`wl_list_init()`-reset self-referencing node), not a bug. Logged so it isn't re-flagged.
- [x] Re-verified focus-clear/hide/detach ordering in `hikari_view_hide()`/`hikari_view_unmap()` — sound, no stale-list or stale-pointer access.
- [x] Audited `src/xwayland_unmanaged_view.c` (override-redirect X11 popups) associate/dissociate/map/unmap/destroy lifecycle — no gap found.
- [x] **Live-system forensics (new — first time any phase had shell access to the actual FreeBSD target):** `ps aux` showed no running `hikari` process (already crashed) with orphaned `hikari-topbar` helpers still alive. `/var/log/messages`/`dmesg` showed 4 crashes today, all **signal 6 (SIGABRT)**, not SIGSEGV — 3 of them after the current fully-patched binary was installed (byte-identical to the repo build via `cmp`). `file` on the installed binary showed **`with debug_info, not stripped`** — built with `DEBUG=YES`, so all `assert()`s are live, not compiled out.
- [ ] **P0 — User-run, empirical (see PLANS.md item -4 for full steps):** create `/var/coredumps` (currently missing, so all crashes have silently produced no core dump), reproduce once with `./start-hikari.sh 2>&1 | tee <logfile>`, and report back either the captured assert/abort message or a `gdb bt full` from the resulting core file. This determines the actual Phase 54 fix — no code change is proposed yet because there isn't a specific line identified.

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


