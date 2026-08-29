# Granular Task List

*Last Updated:* 2026-08-29 11:35

## Active List

### Phase 96: Cross-screen window motion — PLANNED, TWELVE RULINGS TAKEN, FULLY RULED; NOTHING IMPLEMENTED, awaiting approval

*(Re-verified against the tree 2026-08-29 11:35: **every file-and-line citation below resolves exactly and all six code items are unstarted.** Verification table in `DECISIONS_LOG.md` at 11:35.)*

*(Analysis: `DECISIONS_LOG.md` at 2026-08-29 11:19. Sequencing: `PLANS.md` item -22. Opened by the user's report after installing and rebooting the P-1 tree: most previously known problems resolved, but **moving or dragging a window from one screen to the other causes bad screen tearing**. **Nothing was built, installed, tagged or run; no `sudo`, no `make`, no `git`.** Every build, install and hardware test is the USER'S.)*

**Measured, not assumed — the live topology.** A read-only Wayland probe bound `wl_output` and `zxdg_output_v1` against the running session: **eDP-1 1920x1200 @ 60.026 Hz at logical (0,0)**, **DP-3 1920x1080 @ 60.000 Hz at logical (1920,0)**, both reporting `wl_output.geometry` as `(0,0)`. This **confirms Phase 94's assumption that eDP-1 holds layout x = 0** by a means that assumes nothing — the thing struck `L-V1` was reaching for.

**Measured, not assumed — the live configuration.** `layout { auto = true }` (`hikari.conf:261`) and `ui { animation { enabled = true, duration = 120, easing = ease-out } }` (`:155-168`). **Both were false in every prior phase's analysis.** Every window on a sheet is now tiled and every compositor-driven move is interpolated, which is why these defects surfaced on this reboot and not before.

**Proven NOT to be DRM tearing.** `grep -rn tearing src/ include/` returns two product lines — the include at `src/server.c:44` and `wlr_tearing_control_manager_v1_create()` at `:1696`. No listener on the manager's `new_object` signal; `wlr_output_state.tearing_page_flip` set nowhere; `wlr_scene_output_state_options` (`wlr_scene.h:598-613`) has no tearing field and hikari passes NULL anyway (`src/output.c:391`). **Every page flip hikari performs is vblank-synchronised.**

**RULINGS TAKEN 2026-08-29 11:19 — Q1 snap across screens / glide within one · Q2 keep the grab point · Q3 follow `layout { on-insert }` · Q4 follow `reflow-on-close` · Q5 spill while dragging, clip otherwise, plus an always-spill key · Q6 floating stays floating wherever dropped · Q7 alignment configurable with edge tuneables and an auto-centre form · Q8 stop announcing the tearing protocol · Q9 tiling manipulation before screen configuration, both required · Q10 tag after documentation, port against the tag.**

**RULINGS TAKEN 2026-08-29 11:35 — Q11 clip tiled windows only, never clip a floating one, always-spill key overrides · R-4 change the shipped `layout { auto }` default to `true` so the template matches what is tested. Phase 96 now has NO open questions.**

**T-1 — the animation state is screen-local and is never re-based when a window changes screen. THIS IS THE REPORTED DEFECT (CRITICAL)**

- [x] **T-1a** `include/hikari/animation.h:57-79` documents `from_*`, `to_*` and `drawn_*` as **output-local**. `hikari_animation_tick()` places at `current_x + output->geometry.x` (`src/animation.c:276-278`); `hikari_animation_cancel()` at `to_x + view->output->geometry.x` (`:303-305`). The animation is reset in only two places — `hikari_view_init()` (`src/view.c:632`) and the unmap path (`:1457-1458`). **`hikari_view_migrate()` (`:2741`) changes `view->output` via `migrate_view()` (`:2726`) and never touches the animation.** Reset it there.
- [x] **T-1b** `hikari_view_evacuate()` (`src/view.c:2191`) has the identical omission. Same fix, same place in the sequence.
- [x] **T-1c — Q1.** A cross-screen placement must be **instant**. Snap the arrival rather than interpolating it, so no window is ever in flight across the seam.
- [x] **T-1d — the arithmetic, recorded so the fix can be checked against it.** Window at eDP-1-local x = 1911 migrates to DP-3 at local x ≈ 91. `from_x = drawn_x = 1911` (eDP-1's space); the first tick places at `1911 + 1920 = ` **layout x 3831**, the far right edge of DP-3, then eases 120 ms back to 2011. **The window whips across the whole external monitor.**
- [x] **T-1e — reachability, and why this was invisible before.** The *synchronous* `queue_reset()` path (`src/view.c:591-599`) is safe: the view is still flagged hidden by `view_unlink_visible()` (`:346-357`), so `may_animate()` (`src/animation.c:143-153`) is false and the move snaps. The *asynchronous* path — sizes differ, i.e. **every tiled or maximized view**, and every XWayland view through `view->move_resize` — commits from the client's ack, after `hikari_view_show()` has run. **With `auto = true` the severe path is the only path the user has.**

**T-2 — the animation is driven per-output while a crossing window is drawn on two (collapses to a guard under Q1)**

- [x] **T-2a** `frame_handler()` calls `hikari_animation_tick(output, …)` and reschedules only that output (`src/output.c:386-388`); `hikari_animation_tick()` walks `output->views` (`src/animation.c:245`) and a view is in exactly one list; `hikari_animation_move()` schedules a frame on `view->output` alone (`:210`). **Verify this is correct BY CONSTRUCTION once T-1c lands** — if no window is ever in flight across the seam, the single-output driver is right. **Guard and assert; do not restructure.**

**T-3 — 60.026 Hz against 60.000 Hz: the floor, no code, recorded so it is never re-investigated**

- [x] **T-3a — MEASURED, no work.** The vblanks drift with a beat period of ~**38 seconds**, so the inter-screen phase offset sweeps the full 16.67 ms frame interval and back. Any window spanning the seam has its halves sampled that far apart. **This is inherent to independent per-output page flips and cannot be removed in software.** Under Q5's default it is visible only while a drag is in progress; the step is bounded by one frame of travel (~25 px at a brisk 1500 px/s drag).

**T-4 — move mode drops the grab anchor at the crossing (Q2)**

- [x] **T-4a** `src/move_mode.c` `cursor_move()` subtracts `move_mode->anchor_x/anchor_y` on the same-output branch and passes raw `lx, ly` on the other, reaching `hikari_view_migrate(view, sheet, lx - output->geometry.x, ly - output->geometry.y, center)` (`src/server.c:2451-2472`). **The window's top-left corner teleports under the cursor on every crossing**, and the next motion event returns it. Subtract the anchor at the call site — the migrate path uses `lx`/`ly` only as a position, never as a pointer location, so this is minimal and local.
- [x] **T-4b** Recorded: this is **Phase 91's own fix applied to the branch it missed** — *"move mode put the window's top-left corner on the pointer every motion ... so a window grabbed anywhere else jumped away."* Third instance in this tree of a fix landing on one branch of a two-branch dispatch.

**T-5 — a cross-screen move untiles the window and re-tiles neither sheet (R-1, Q3, Q4, Q6)**

- [x] **T-5a — Q6.** A **floating** window stays floating and keeps the position it was dropped at, straddling the seam permitted.
- [x] **T-5b — R-1 + Q3.** A **tiled** window adopts the destination sheet's layout, entering it per the existing `layout { on-insert }` preference. Today `queue_reset()` calls `cancel_tile()` and `hikari_tile_detach()` (`src/view.c:583-589`) and the window arrives **floating on top of the destination's layout**.
- [x] **T-5c — Q4.** The **source** sheet closes its hole per the existing `reflow-on-close` preference. `hikari_reflow_schedule()` is called from map (`src/view.c:1335`), unmap (`:1468`), sheet display (`src/workspace.c:209`) and the layout-change handler (`src/server.c:1305`) — **and from nowhere on the migrate path.**
- [x] **T-5d** No new configuration keys: Q3 and Q4 both reuse preferences that already exist, so there is one knob per behaviour rather than two.

**T-6 — nothing clips a window to its own screen (Q5) — UNBLOCKED 2026-08-29 11:35, Q11 ruled; lands in Phase 96**

- [x] **T-6a** `hikari_geometry_constrain_relative()` (`src/geometry.c:91-117`) permits `x` up to `usable_area.x + width - gap`, where `gap = gap*2 - border` = 5·2−1 = **9**. A window can be dragged until only 9 px remain on its own screen, with the rest painted on a panel showing a **different sheet**.
- [x] **T-6b — Q5 + Q11.** Clip a resting **tiled** window to its own screen; lift the clip for the duration of a move/resize drag. **A floating window is never clipped** (Q11).
- [x] **T-6c — Q5.** New configuration key to force always-spill. Name to be ruled; `ui { spill = always | drag | never }` proposed, with `drag` as the default.
- [x] **T-6d — DESIGN NOTE.** `wlr_scene_subsurface_tree_set_clip()` (`wlr_scene.h:693-699`) clips a **surface tree**; a view's border and indicator-frame rects (`src/border.c`, `src/indicator_frame.c`) are separate `wlr_scene_rect` nodes in the same tree and need their own handling. Scope this before writing code.
- [x] **T-6e — CLOSED 2026-08-29 11:35, Q11 RULED.** Q5 (clip at rest) and Q6 (a floating window rests where it was dropped) collided: together they meant **a floating window dropped across the seam loses half of itself**. **Ruled: clip tiled windows, never clip floating ones, and let T-6c's key override the whole behaviour** — the recommended reading. The two rulings were never in conflict about intent, only about a case neither had been asked about: Q5 exists to stop a window resting half-painted over a panel showing a different sheet, and a tiled window is placed by the compositor, so clipping it takes nothing from the user; Q6 exists so a window the user dropped stays dropped, which is only meaningful for the one kind of window the user positions by hand.

**T-7 — mismatched screen heights leave a band that belongs to no screen (Q7 — guard here, keys in Phase 99)**

- [x] **T-7a** eDP-1 is 1200 tall and DP-3 1080, both at y = 0 by `wlr_output_layout_add_auto()`. Layout rows 1080–1200 at x ≥ 1920 are on no physical output and `wlr_output_layout_output_at()` returns NULL there — `move_mode.c` `cursor_move()` returns silently (the drag freezes) and `move_view()` (`src/server.c`) falls back to a same-output move. **Refuse to place a window's origin in the band.** One guard.
- [ ] **T-7b — Phase 99.** The alignment tuneables Q7 rules — an edge-alignment key that holds regardless of screen size, plus an auto-adjust/centre form — belong with **P-4**, not here.

**T-8 / T-9 — deferred to Phase 98 by Q8 and by adjacency**

- [ ] **T-8 — Q8.** Stop announcing `wp_tearing_control_v1`: delete `src/server.c:44` and `:1696`. Possible later implementation behind a build switch. **Not the reported symptom — it is the proof the reported symptom is not scanout tearing.**
- [ ] **T-9** `hikari_server.track_damage` is written (`src/server.c:1532`), toggled by the bound action `hikari_server_toggle_damage_tracking()` (`:2673`), and **read nowhere**. Wire it or delete the action.

**Q11 — RULED 2026-08-29 11:35. No open questions remain in Phase 96.**

- [x] **Q11 — CLOSED.** Q5 said clip at rest; Q6 said a floating window rests where it was dropped, straddling permitted. Together: **drop a floating window across the seam and half of it vanishes.** Neither ruling intended that. **Ruled: clip tiled windows only; never clip a floating window; T-6c's always-spill key overrides the whole behaviour.** T-6 is unblocked and lands in Phase 96.

**R-4 — NEW 2026-08-29 11:35. The shipped default and the tested configuration disagree; found by cross-referencing the trackers against the tree**

- [x] **R-4a** `etc/hikari/hikari.conf:266` ships **`layout { auto = false }`**; the live `~/.config/hikari/hikari.conf:261` has **`auto = true`**. **The whole Phase 96 analysis rests on the live value** — T-1e records that the severe asynchronous path is every tiled or maximized view, which under `auto = true` is every window the user has. **The divergence was on no tracker.** A maintainer reproducing Phase 96 from a fresh checkout would run the shipped template, would not have a tiled sheet, would not reach T-1's severe path, and would conclude the analysis was wrong. Same failure shape as the struck `L-V1` and as FB-4's ~60-phase survival: **a recorded fact whose preconditions were never recorded with it.**
- [x] **R-4b — RULED by the user.** Change the shipped default to **`auto = true`**, so the template matches the configuration actually being developed and tested against. **Total Feature Retention is intact:** `auto = false` remains available, documented and unchanged in behaviour, and `insert` and `reflow-on-close` are untouched.
- [x] **R-4c — the documentation moves with the default, and this is NOT deferrable to Phase 102.** `etc/hikari/hikari.conf:250-263` describes `auto = false` as "the historical behaviour" **in prose**, and `hikari(1)` states no-auto-insert as design intent. Both become false the instant the value flips. **A configuration file that contradicts itself is worse than either default alone.** Fix the comment block and the man page text in the same change as R-4b.
- [ ] **R-4d — sequencing, open.** R-4 is independent of every T-item and touches no compositor source. It can travel with Phase 96 or stand alone; it is listed here rather than in Phase 102 because R-4c makes it self-contained.

**User-run verification, after the build**

- [ ] **T-V1** Move a **tiled** window across to the other screen with the keyboard. It must arrive **instantly** and **in the destination layout** — no travel across the external monitor, no floating window on top of the tiling.
- [ ] **T-V2** Drag a **floating** window across with the pointer, grabbed away from its corner. It must keep the grab point through the crossing — **no corner-jump at the seam**.
- [ ] **T-V3** Check the **source** sheet after T-V1: the remaining windows must have closed the gap, per `reflow-on-close`.
- [ ] **T-V4 — re-run M-V2 (R-3).** Hold `L` and drag a floating window; it must track the pointer with no lag. The 11:19 observation was **provisional** — T-1 and T-4 both perturbed the drag, so it was not a clean reading.
- [ ] **T-V5 — L-2c-iv, still outstanding from Phase 95.** Hover a layer surface on `DP-3` with no click, then `printf 'state\n' | nc -U $XDG_RUNTIME_DIR/hikari.sock`. It must report `output DP-3`. *(A socket read at 10:52 did return `output DP-3`, but the pointer's location was not controlled, so that is corroboration and not the test.)*

**Status changes to earlier trackers, recorded here so no phase reasons from the stale list**

- [x] **V1-1 and V1-4 — CLOSED ON HARDWARE.** `/usr/local/bin/hikari` is dated 10:27 and matches the in-tree build; the user installed and rebooted, and reports the previously known problems resolved.
- [x] **V1-2 — CLOSED.** Phase 92's M-1/M-2 have now been run. **M-V2 is provisional (R-3)** and is re-listed above as T-V4.
- [x] **V1-3 — CLOSED AS DEFERRED, not as fixed (R-2).** The user's ruling: *"install-user for the wallpaper is not significant and should be permanently deferred until v1 tagging time."* **It leaves the v1-blocker list** and becomes a Phase 103 item.
- [ ] **V1-5 — still open.** C-1/C-2, a missing dependency fails incomprehensibly. Phase 101.
- [x] **D-1 — remains struck** (2026-08-29 08:57). **L-V1 — remains struck**, and the assumption it was reaching for is now measured directly (see the topology above).

### Phase 95 SS-P-1 - IMPLEMENTED 2026-08-29 10:21. Compiled and linked; NOT run.

*(Decisions: `DECISIONS_LOG.md` at 10:21. Rulings taken before execution: **L-1b = option (A)**; **N-2 folded into P-1**; **M-8j closed by the user**. The whole tree compiles at `-Wall -Werror` and links with `/usr/bin/clang` out-of-tree -- 71 translation units, `hikari -v` runs. **The in-tree build is the USER'S; no `sudo`, no install, no `git`, nothing tagged.**)*

| Item | File | What landed |
|---|---|---|
| L-1a | `src/layer_shell.c` | `full_area` anchored at `output->geometry.x/y`, not `{0,0}` |
| L-1b | `src/layer_shell.c` | `usable_area` translated back to output-local before the store -- option (A) |
| L-1c / L-1e | -- | Verified correct once L-1a landed; no edit |
| L-1d | -- | Sweep complete: **no fifth omission; the family is closed at four** |
| L-2c-i/ii/iii | `src/layer_shell.c` | `focus()` resolves `layer->output->workspace`, clears `focus_layer` where it is actually held, and assigns `hikari_server.workspace` |
| Finding 5 | `src/layer_shell.c` | `hikari_layer_init()` no longer dereferences a NULL `hikari_server.workspace` |
| X-1a | `src/output.c`, `include/hikari/output.h` | `output_geometry()` becomes the public `hikari_output_update_geometry()` -- the single entry point |
| X-1b | `src/layer_shell.c`, `include/hikari/layer_shell.h`, `src/server.c` | `hikari_layer_shell_arrange()`; the layout-change handler now runs update-geometry then arrange then reflow |
| X-2a | `src/output.c` | The noop output's `geometry` and `usable_area` are initialised |
| X-3a | `src/server.c` | The wallpaper is re-decoded only when the output's dimensions change |
| N-2 | `src/normal_mode.c` | `cursor_move()` compares the hovered node against `focus_layer` as well as `focus_view` |

**N-2 -- found 2026-08-29 while verifying L-2c, and on no tracker before that.** `cursor_move()` compared the node under the pointer only against `hikari_server.workspace->focus_view`. A layer node can never equal `focus_view`, so hovering any layer surface called `hikari_node_focus()` on **every pointer motion event** -- for a keyboard-interactive layer, a full seat keyboard leave/enter per event. Harmless while `focus()` did nothing for non-interactive layers; not harmless once L-2c makes `focus()` assign the workspace. Fixed with the comparison it should always have had.

**Still open in this phase, unchanged:** P-2 (`X-4a`, the `install-user` wallpaper path, one line), P-3 through P-11, and **L-2c-iv**, which is user-run and is now testable for the first time.

### Phase 95: Coordinate space, output geometry, dependencies, documentation — ANALYSIS COMPLETE AND RULINGS TAKEN; NOTHING IMPLEMENTED, awaiting approval

*(Analysis: `DECISIONS_LOG.md` Phase 95. Sequencing: `PLANS.md` item -19. Requested by the user 2026-08-29 as three questions — multi-screen positioning/reorganising/hot-plug/memory, comprehensive user-facing configuration documentation, and dependency handling during install — plus a v1 readiness assessment. **Nothing was built, installed, tagged or run; no `sudo`, no `make`, no `git`.**)*

**Standing rule restated by the user 2026-08-29 08:28, recorded so no phase plan assumes otherwise: the agent never runs `sudo` and never installs. Every build, install and hardware test below is the USER'S to run.**

**P — THE PLAN, re-verified 2026-08-29 08:57. Ordered by dependency. No time estimates: the ones given at 08:28 had no basis and are withdrawn.**

*Every item below cites the file and line it was verified against. **Nothing is implemented.** Every build, install and hardware test is the USER'S — the agent does not run `sudo` and does not install.*

- [x] **P-0 — no verification step is required before code, and the one previously proposed is struck.** See L-V1 above. The defect is already established by direct measurement from the running compositor. L-V2 (a menu on `eDP-1` laid out for `DP-3`'s 1920x1080 rather than the panel's 1920x1200) remains available as free corroboration and requires no configuration change.
- [x] **P-1 — DONE 2026-08-29 10:21. The layer-shell defect and everything that shares its code.** Findings 1, 2, 3, 4, 5, and 7 while in the same handler. This is the `sofi` session's WP-A → WP-B/WP-D → WP-C. Ownership was offered to that session at 08:28; the user has since instructed this session to write these trackers, so **that session should read the 08:57 `DECISIONS_LOG` entry before writing anything here.** Finding 2 must land with finding 1: fixing *where* panels are drawn without fixing *when* the arrangement is recomputed leaves the same bug reachable through every hotplug and every position change.
- [ ] **P-2 — the `install-user` wallpaper path.** Finding 6, one line, independent of everything else. `Makefile:408-409`.
- [ ] **P-3 — monitor settings that do not exist.** Finding 8. Add `mode`, `refresh`, `scale`, `transform` and `enabled` to the outputs parser, and make startup and reload share **one** apply function — today `src/output.c:570-581` and `src/configuration.c:2506-2516` are separate implementations and only the first sets a mode at all. **Depends on P-1's finding-2 fix**, or nothing re-derives when a mode changes at runtime. Touches `output_config.h`, `output_config.c`, `configuration.c`, `output.c`.
- [ ] **P-4 — relative positioning between monitors.** Finding 9. The relative parser already exists and is used for views (`src/position_config.c:35-73`); outputs bypass it (`src/configuration.c:1789`). Needs ordering resolution so declaration order does not matter and cycles are rejected.
- [ ] **P-5 — send a window to another monitor.** Finding 11. One action pair over the existing `hikari_server_migrate_focus_view()` (`src/server.c:2429`). Note that "focus the next monitor" already exists and is already bound — see the corrected O-5.
- [ ] **P-6 — windows return when their monitor does.** Finding 12. Identity is the **monitor, not the port**, per the user's ruling: `wlr_output` carries `make`, `model` and `serial` and the header documents all three as possibly NULL (`wlr_output.h:189`), so the identity is a fallback chain — `make|model|serial` → `make|model|connector` → `connector` — with the resolved identity logged at startup. Consequence, and it is the intended one: the same monitor on a different port keeps its windows; a different monitor on the same port does not.
- [ ] **P-7 — `wlr-randr` / `kanshi` support.** Finding 10. New file behind a build switch; the wlroots headers are present in the installed 0.20.2 (`wlr_output_management_v1.h`, `wlr_output_power_management_v1.h`, `wlr_output_swapchain_manager.h`). **Depends on P-1 and P-3.** This is what makes positioning and reorganising screens a usable capability rather than a config key.
- [ ] **P-8 — build preflight.** Findings 13 and 14. `make check-deps` probing every pkg-config module and every required tool, failing immediately and naming the exact `pkg install` line. Prerequisite of `all` and `install`. Installs nothing.
- [ ] **P-9 — the two missing documentation statements.** See the rescoped D-2. Two paragraphs in `OUTPUTS`, not a rewrite.
- [ ] **P-10 — FreeBSD port**, so `pkg install` resolves dependencies for a user. After P-9, against a tag. **User's, and package origins must be confirmed on FreeBSD — they could not be queried from the environment this analysis was performed in.**
- [ ] **P-11 — the user builds and verifies.** Phase 92's M-1/M-2 have been compiled and never run; M-V2 (hold `L`, drag a floating window — it must track the pointer with no lag) is the regression check that matters.

**X-1 — `arrange_layers()` is never re-run when output geometry changes. This is L-1's completion and belongs in the same phase (CRITICAL, v1 blocker)**

- [x] **X-1a — DONE.** `output->usable_area` has **two writers** — `output_geometry()` (`src/output.c:309-317`) and `arrange_layers()` (`src/layer_shell.c:206`) — and `output_geometry()` is called from **exactly one place in the tree: output init, `src/output.c:610`**. `output_layout_change_handler()` (`src/server.c:1243-1288`) updates `output->geometry` and repositions view scene nodes, then stops. Nothing else calls either: `arrange_layers()` is driven only by layer-surface events (`src/layer_shell.c:583, 606, 631, 751`). **Make `output_geometry()` the single entry point and call it from the layout-change handler.**
- [x] **X-1b — DONE.** Expose a public `hikari_layer_shell_arrange(struct hikari_output *)` so the layout-change handler can re-arrange layers, and reflow tiled sheets after it. **Ordering matters:** `output_geometry()` seeds `usable_area` from the full box and reserves the bar; `arrange_layers()` re-derives from the same baseline and shrinks by exclusive zones. The second must run after the first or the bar's rows are handed back to views — the hazard `arrange_layers()`'s own comment at `:134-140` already documents.
- [x] **X-1c — DONE.** **This is what makes reload, mode change and hotplug all correct at once** — the first two thirds of the user's "hot loading" question. Without it, `outputs { position }` on reload moves the output and leaves every layer surface and the usable area behind.
- [x] **X-1d — MOOT. L-V1 is struck and the hazard is fixed: a reload now re-arranges layers.** Original text: L-V1 correctly says *restart*. If the user instead reloads with `L+S+r`, `hikari_output_move()` fires and the layout changes, but **no layer surface is re-arranged at all** — `sofi` stays exactly where it was and the test reads as a **false negative**, appearing to refute a correct diagnosis. **Restart, do not reload.**

**X-2 — the noop output's `geometry` and `usable_area` are never initialised (latent uninitialised read)**

- [x] **X-2a — DONE.** `hikari_output_init()` assigns every field of `struct hikari_output` explicitly except `geometry` and `usable_area`, which are written only by `output_geometry()` — inside `if (!noop)` (`src/output.c:499-610`). The struct comes from `hikari_malloc()`, a bare `malloc(3)` with no zeroing (`src/memory.c:19-21`); both `new_output_handler()` and `init_noop_output()` (`src/server.c:1442-1446`) allocate this way. **Reachable when the last real output goes away:** `hikari_output_fini()` merges into the noop workspace and makes it current, after which `hikari_geometry_constrain_relative()`, the nine named positions and `queue_full_maximize()` (`src/view.c:1748`) all read an indeterminate box. Two lines.

**X-3 — every wallpaper is re-decoded on every layout change (performance, not correctness)**

- [x] **X-3a — DONE, by not re-decoding rather than by caching.** `output_layout_change_handler()` calls `hikari_output_load_background()` for **every** output on **every** layout change (`src/server.c:1260-1268`), and that decodes the PNG from disk and re-renders it at full output size (`src/output.c:86-126`). Per output, per hotplug, per move. Cache the decoded surface; re-render only when the output's dimensions or the configured path change.

**X-4 — `make install-user` writes a broken background path (v1 blocker, one line)**

- [ ] **X-4a** Carried from **M-7d**, unchanged and still unfixed. `Makefile:408-409` runs `s,/share/backgrounds/hikari,${HOME}/.config/hikari,` *after* `s,PREFIX,${PREFIX},` has already made the path absolute, stranding the prefix: `/usr/local/home/<user>/.config/hikari/hikari_wallpaper.png`. Confirmed by running the real pipeline. **Any fresh `install-user` produces a config whose wallpaper cannot load.** Fix: anchor the second substitution on the full post-prefix path, or reorder it before the first.

**C — dependencies and the install process (Phase 96; ruled "B now, A at release")**

- [ ] **C-1** **Measured, not reasoned about:** a missing pkg-config module is a **warning**, not an error. Reproduced with a throwaway makefile — `bmake: t.mk:1: warning: Command "pkg-config --cflags nonexistent-lib-xyz" exited with status 1` and `X=[]`. The variable is left empty and the build proceeds to fail far later with `#include <wlr/...>: file not found`. True of all nine probes, and of `WAYLAND_PROTOCOLS != ${PKG_CONFIG} --variable pkgdatadir wayland-protocols`, where an empty result makes the `xdg-shell-protocol.h` rule scan the nonexistent `/stable/xdg-shell/xdg-shell.xml`.
- [ ] **C-2** `wayland-scanner`, `pandoc`, `install` and `sed` are used by rules and probed by **nothing**. `pandoc` is required by `make install` from a git checkout — discovered only after a full compile.
- [ ] **C-3 — option B, RULED for now.** `make check-deps`: probe every pkg-config module and every required tool, fail fast naming what is missing and the exact `pkg install` line to run. Prerequisite of `all` and `install`. Installs nothing.
- [ ] **C-4 — option A, RULED for the release.** A FreeBSD port: `ports/x11-wm/hikari-sakura/{Makefile,distinfo,pkg-descr,pkg-plist}` with `LIB_DEPENDS`/`BUILD_DEPENDS`/`RUN_DEPENDS`, so `pkg install` resolves everything. **This is the only route that genuinely installs dependencies for the user.** Do it after Phase 97, against a tagged tree. **Exact package origins must be confirmed on FreeBSD — they could not be queried from this environment.**
- [ ] **C-5 — option C NOT TAKEN.** A `make deps` target running `pkg install -y …`. Recorded as considered and declined: it needs root and installs software as a build side effect.
- [ ] **C-6** Runtime dependencies are declared nowhere machine-readable: `seatd`, `dbus` (`dbus-run-session`, `start-hikari.sh:145`), `xdg-desktop-portal` + `-wlr`, XWayland, optional `sofi`/`sakura`, optional `grim`/`slurp`. README lists build dependencies only. `RUN_DEPENDS` in C-4 is where these belong.

**D — user-facing documentation (Phase 97). Coverage is complete; NAVIGABILITY is the gap**

*Measured by extracting every `strcmp(key, …)` in the parser and every `strcmp(str, …)` in `src/action.c` and checking each against `hikari.md` and `hikari.conf`: **70 of 70 configuration keys documented; 65 of 66 action names documented** (only `debug-damage`, which is `#ifndef NDEBUG`). The apparent gaps in a naive grep are bracket-family entries such as `view-decrease-size-[up|down|left|right]` (`hikari.md:442`) and are correct.*

- [x] **D-1 — STRUCK 2026-08-29 08:57. THE CLAIM WAS FALSE.** `etc/hikari/hikari.conf:507-701` **is** a default-keymap reference, grouped and commented by task — session control, launching, laptop media keys, view cycling and lifecycle, view state toggles, modal operations, sheet switching and pinning, layout registers, and a prose block on layout manipulation. It is better organised than the section that was proposed to replace it. No work here. *Original text, retained so the error is legible: "no default-keymap reference exists anywhere ... Add a `DEFAULT BINDINGS` section to `hikari(1)`, plus a short 'first ten minutes' table in `README.md`."*
- [ ] **D-2 — RESCOPED 2026-08-29 08:57; the original claim was overstated.** Multiple outputs *are* covered: CONCEPTS states one workspace per output (`hikari.md:116`), the sheet-assign action documents TAB cycling outputs (`:412`), and the control-socket section documents the `output <name>` response (`:1717`, `:1727`). **The genuine gap is two statements the `OUTPUTS` section does not make:** (i) resolution, refresh, scale, rotation and per-output enable are **not configurable** — the only modeset in the tree is `wlr_output_preferred_mode()` at init (`src/output.c:512`); and (ii) **no output-management protocol is advertised, so `wlr-randr`, `kanshi` and `wdisplays` cannot work** (`src/server.c:1588` creates only the read-only xdg-output manager). Worth adding as well, since it is the same paragraph: `position` takes absolute `{x,y}` only, while views accept the nine relative keywords (`src/configuration.c:1789` vs `src/position_config.c:35-73`).
- [ ] **D-3** 11 documented actions are bound nowhere in the shipped configuration and are discoverable only by reading `hikari(1)` cover to cover: the four `group-cycle-view-*`, `layout-cycle-view-first`/`-last`, `mode-enter-input-grab`, `mode-enter-mark-switch-select`, `workspace-show-group`, `workspace-show-invisible`, `debug-damage`.
- [ ] **D-4** Configuration documentation is organised by parser section, not by task. No cookbook: "float this app on sheet 3", "laptop plus external monitor", "bind a custom layout end to end".
- [ ] **D-5** No troubleshooting section. The real failure modes — portal/screen-share, ZFS runtime dir, missing `sofi`, duplicate keys, wallpaper path — are scattered across five README sections.
- [ ] **D-6** README states FreeBSD-only but never states the Makefile is **BSD make**. Someone typing `make` on GNU make gets syntax noise. One line.
- [ ] **D-7** The `XDG_CURRENT_DESKTOP="Hikari Sakura:wlroots"` rationale is well commented in `start-hikari.sh:20-32` and absent from user-facing documentation; `hikari.desktop` carries `DesktopNames=Hikari Sakura;wlroots`. Worth stating, since it is what makes a portal backend match at all.
- [ ] **D-8** No CHANGELOG and no release notes. `make dist` referenced a `CHANGELOG.md` that has never existed until Phase 93 removed the reference.

**V1 — readiness verdict, recorded 2026-08-29 08:28: NOT YET, and the gap is five items**

- [x] **V1-1 — CODE CLEARED 2026-08-29 10:21; the user's build and run is what closes it.** **L-1 + L-2c** — layer surfaces drawn on the wrong output. Affects every layer-shell client, not `sofi` alone.
- [ ] **V1-2** **Phase 92 M-1/M-2 never run** — compiled clean, never linked, never executed. A 1.0.0 cannot contain an unexercised change to the move path. **User-run.**
- [ ] **V1-3** **X-4a / M-7d** — first-run wallpaper path is broken. One line.
- [x] **V1-4 — CODE CLEARED 2026-08-29 10:21; the user's build and run is what closes it.** **X-1** — geometry and layer arrangement never re-derived. Now reachable: two outputs since 2026-08-25.
- [ ] **V1-5** **C-1/C-2** — a missing dependency fails incomprehensibly.
- [x] **V1-6 — recorded, explicitly NOT blockers.** The 255 dead `assert()`s (open by the user's standing instruction); OBS ScreenCast black (downstream, Phase 81); M-9 global modifier state (real defect, no observed symptom); and every multi-screen *feature* — output management, modes/scale/rotation, relative placement, output actions, per-monitor memory — which is 1.x work under Phases 98 and 99.
- [x] **V1-7 — recorded: nothing was versioned.** The user asked for an assessment, not a bump. No tag was created, `VERSION` was not changed, no release was prepared. Existing tags are `1.0.0-alpha2` and `1.0.0-beta`; `VERSION ?= "CURRENT"` means an untagged build reports `CURRENT` in `hikari -v` and in the manual page's `.TH` line.

**O — multi-screen configuration and output management (Phases 98 and 99, post-1.0)**

- [ ] **O-2** Full output configuration keys — `mode`, `refresh`, `scale`, `transform`, `enabled`, `adaptive-sync` — through **one** `apply_output_config()` shared by init and reload. Today those are two divergent copies (`src/output.c:544-581` and `src/configuration.c:2497-2521`), and only the first sets a mode at all: `wlr_output_preferred_mode()` (`src/output.c:512`) is the only modeset in the tree.
- [ ] **O-3** Relative placement (`left-of`/`right-of`/`above`/`below`, plus the existing relative enum). `src/configuration.c:1789` calls `hikari_position_config_absolute_parse()` directly, so relative keywords fail to parse for outputs even though `src/position_config.c` implements nine of them for views. Needs a topological sort so declaration order does not matter and cycles are rejected.
- [ ] **O-5 — CORRECTED 2026-08-29 08:57. The "dead API" claim was FALSE.** `hikari_output_next()`/`hikari_output_prev()` are **macro-generated** by `CYCLE_OUTPUT` (`src/output.c:753-770`) and **macro-called** by `CYCLE_WORKSPACE` (`src/workspace.c:83-94`) via `hikari_output_##name`; a literal grep misses both sides, which is how the error was made. They are load-bearing — they are how `workspace-cycle-next`/`-prev` walk between monitors (`src/server.c:2139-2161`), and **both are already bound in the shipped config**: `LS+n`/`LS+b` (`hikari.conf:662-663`) and 3-finger swipes (`:405-406`). **So "focus the next monitor" already exists and works.** What remains is genuinely absent: **no single action sends a window to another monitor.** Migration itself works and is reachable three ways — walking the window across the seam (`src/server.c:2453-2491`), pointer/touch drag (`src/move_mode.c:77`), and TAB in sheet-assign mode (`src/sheet_assign_mode.c:57`) — all funnelling through `hikari_server_migrate_focus_view()` (`src/server.c:2429`). Add `view-move-to-output-next`/`-prev` on top of it.
- [ ] **O-1** `wlr-output-management-v1` in a new `src/output_manager.c` behind `WITH_OUTPUT_MANAGEMENT`, applying atomically via `wlr_output_swapchain_manager`, re-publishing head state on every layout, mode and hotplug change. **This is what makes `wlr-randr`, `kanshi` and `wdisplays` work.** All three headers are present in the installed wlroots 0.20.2. Depends on X-1 and O-2.
- [ ] **O-6** Per-monitor memory, on the identity the user ruled 2026-08-29. `wlr_output` carries `make`, `model` and `serial` and the header states they **may be NULL** (`wlr_output.h:189`), so the identity is a fallback chain — **`make|model|serial` → `make|model|connector` → `connector`** — with the resolved identity logged at startup. Consequence, and it is the intended one: **the same monitor on a different port keeps its windows; a different monitor on the same port does not.** Record `(identity, sheet index)` per view on evacuate; sweep and migrate back on new-output init.
- [ ] **O-8** Lock backdrop and clock for an output hot-plugged while locked. `hikari_output_init()` disables the new output if lock mode has outputs down (`src/output.c:539-543`) but never creates its lock nodes, so the new screen shows an empty lock layer. Anticipated in `PLANS.md:358` item 5. Safe today — the desktop layers are globally disabled — just unfinished.

### Phase 94: Layer surfaces are drawn on the wrong output — ANALYSIS ONLY, NOTHING IMPLEMENTED, awaiting approval

*(Analysis: `DECISIONS_LOG.md` Phase 94. Raised in the `sofi` repository 2026-08-29: the shell only ever appears on the built-in screen. **Root cause is here, not in the client.** No source in either tree has been modified.)*

**The defect in one line:** `arrange_layers()` (`src/layer_shell.c:127-131`) passes wlroots a `full_area` anchored at `{0,0}` — output-local — while the four layer scene trees are server-global on a scene root whose space is the output layout (`src/server.c:1005`), so **every layer surface on every output is positioned inside the layout rectangle belonging to the output at layout origin.**

**User-run verification — L-V1 is the one that measures the assumption**

- [x] **L-V1 — STRUCK 2026-08-29 08:57. The test is circular and its result would be corrupted by the defects it is meant to prove.** The user's opening request for this line of work was to establish whether positioning and reorganising screens is possible at all; proving a defect by exercising that same feature assumes the answer. Worse, it cannot work here: panels are drawn at the layout origin regardless of position (`src/layer_shell.c:127-131`), and on reload nothing downstream re-derives from a moved layout (`src/server.c:1243-1288` contains no `usable_area`, no `arrange`, no `output_geometry`), so the visible result of a position change is partial either way. **No position-based test is needed** — the `sofi` session already measured the contradiction directly: hikari's own IPC reported `output DP-3` as the active workspace while `sofi` drew on `eDP-1`. L-V2 remains valid and costs nothing, since it is an observation rather than a configuration change. *Original text: swap the two outputs' absolute `position` and restart, predicting every sofi surface then lands on `DP-3`.*
- [x] **L-V2 — STRUCK 2026-08-29 10:21. The quantity it corroborated is now measured directly.** `sofi`'s S-C bound `zxdg_output_unstable_v1` and read `DP-3` at `position: 1920,0`, so the layout topology is no longer inferred. Original text: Summon a sofi menu while the pointer is on `DP-3`. It should appear on `eDP-1` **laid out for a 1920x1080 screen rather than the panel's 1920x1200** — the size comes from `full_area`'s width/height, which are the *correct* output's, while only the origin is wrong. A visible vertical mismatch is direct evidence of the split.

**L-1 — `arrange_layers()` positions layer surfaces in the wrong coordinate space (CRITICAL, blocked on approval)**

- [x] **L-1a — DONE.** Make the box handed to `wlr_scene_layer_surface_v1_configure()` layout-global: `full_area` origin becomes `output->geometry.x/y` rather than `{0,0}`. ~2 lines. **This alone is the fix for the reported symptom.**
- [x] **L-1b — DONE, option (A) per the user's ruling 2026-08-29.** `usable_area` is seeded from `full_area` and stored to `output->usable_area`, which **must stay output-local**: `src/geometry.c:64-170` computes every view position straight from `usable_area->x/y`, and `src/view.c:287-289` adds `output->geometry.x/y` on the way to the scene. Phase 92's live read corroborates it — `eDP-1 usable_area {0,34 1920x1166}`. wlroots requires both boxes in one space, so translate back to output-local before the store, or carry two boxes deliberately. **Getting this wrong offsets every view on every non-origin output by that output's layout origin, silently.**
- [x] **L-1c — VERIFIED CORRECT, no edit needed.** `layer->geometry.x/y = nx - output->geometry.x/y` (`src/layer_shell.c:176-180`) becomes correct with no edit once L-1a lands — its comment already describes the fixed behaviour. **Verify rather than assume:** today it computes `-1920` for a surface on `DP-3`, and `hikari_output_add_damage()` and `popup_unconstrain()` both consume that box.
- [x] **L-1d — DONE. No fifth member exists; the family is closed at four.** Sweep for a fifth member of the scene-port family, as M-1c did for views. Every scene node parented to `hikari_server.layers.*` or to the scene root must add the output origin explicitly; `src/bar.c:1552-1555` states the rule, `src/view.c:287-289`, `src/lock_clock.c:229`, `src/lock_indicator.c:228` and `src/indicator_bar.c:110` are the known obeyers. Confirm the list is complete.
- [x] **L-1e — VERIFIED CORRECT, no edit needed.** Re-check `hikari_layer_popup` placement (`src/layer_shell.c:340-355`, `:535-553`) against the corrected geometry — `popup_unconstrain()` builds its box from `output->geometry.width/height` at origin zero and damages through `layer->geometry`, both of which L-1a/L-1c move.

**L-2 — which output should an unassigned layer surface land on? RULED (c) by the user 2026-08-29 08:28. Nothing built.**

*Ruling: **(c)** — placement keeps following the focused workspace, and the focus gap is closed **at its cause**. Analysis in `DECISIONS_LOG.md` Phase 95 §"L-2c: the gap has two halves". **(c) is additive to (a), not a replacement for it** — Phase 94 recorded all three as additive to the placement fix and none as observable until L-1 lands, so the ruling resolves to **L-1 and L-2c together**. (b) is not taken.*

- [x] **L-2a** — **(a)** fix `arrange_layers()` — **NOT OPTIONAL under any of the three answers, and tracked as L-1a.** Original text: fix `arrange_layers()` only; placement keeps following the focused workspace, which `cursor_move()` already moves with the pointer. **Recommended.**
- [x] **L-2b** — **(b) NOT TAKEN.** Original text: additionally resolve a NULL `wl_output` to `wlr_output_layout_output_at(cursor->x, cursor->y)`, making "the screen with the mouse" literal rather than focus-derived.
- [x] **L-2c** — **(c) DONE, both halves, 2026-08-29 10:21.** The gap is larger than Phase 94 recorded, and the second half was not on record:
  - [x] **L-2c-i — DONE.** — the early return.** `if (state->keyboard_interactive)` wraps the *entire* body of `focus()` (`src/layer_shell.c:904-935`), so a layer surface without keyboard interactivity — bars, toasts, background setters, `sofi`'s non-input surfaces — moves `hikari_server.workspace` **not at all**. The pointer can sit on `DP-3` while the compositor still believes the active workspace is `eDP-1`'s.
  - [x] **L-2c-ii — DONE.** — the wrong workspace is written even on the interactive path.** Inside the branch, `workspace = hikari_server.workspace` takes the **currently focused** workspace and the function ends `workspace->focus_layer = layer`. For a layer on another output — precisely this case — `focus_layer` lands on the workspace the user is *not* pointing at, and the layer's own workspace never learns it holds focus.
  - [x] **L-2c-iii — DONE.** — mirror the view path rather than patching the caller.** `hikari_workspace_focus_view()` (`src/workspace.c:415-489`) takes the target workspace **as a parameter** and ends `hikari_server.workspace = workspace` (`:487`); layer shell is the one focus path that never assigns it. Fix inside `focus()`: resolve `layer->output->workspace`, assign it, write `focus_layer` there. **Do not patch `cursor_move()`** — `src/normal_mode.c:216-253` re-focuses only in its `node == NULL` branch and otherwise delegates to `hikari_node_focus()`, which is correct polymorphism; special-casing the caller would leave every other entry point to `hikari_node_focus()` still broken.
  - [ ] **L-2c-iv — verification is user-run and cannot precede L-1.** Hover a `sofi` bar or toast on `DP-3` with no click, then read `printf 'state\n' | nc -U $XDG_RUNTIME_DIR/hikari.sock` — it must report `output DP-3`. Today it reports the other screen.

**L-3 — recorded, no action requested**

- [ ] **L-3a** This has affected **every** layer-shell client since the wlroots-0.20 scene port, not `sofi` alone. It could not have been noticed before 2026-08-25: this machine had exactly one output until then (M-8d, live-verified `eDP-1 {0,0 1920x1200}` and nothing else), and a single output at layout origin makes the bug invisible by construction.

### Phase 92: Motion, keyboard geometry and input gestures — M-1 + M-2 IMPLEMENTED (compiled, not linked, not run); M-3/M-4 open; M-5/M-6 deferred by the user

*(Analysis: `DECISIONS_LOG.md` Phase 92, two entries — the audit at 13:26 and the implementation at 13:44. Raised by the user 2026-08-25 13:18; their 13:37 test confirmed M-1 by elimination and deferred gestures/touch. **`src/view.c` compiles clean at `-Wall -Werror`; NOT linked, NOT run — the build is the user's.**)*

**User-run verification — do these FIRST, they are cheap and two of them are discriminating**

- [x] **M-V1 — RETIRED AS ANSWERED, 2026-08-25 13:37.** The user's own test settled it without needing this: with animation enabled, re-tiling motion is visibly drawn (it runs through `hikari_view_refresh_geometry()`) while `LA+Left` produced no motion at all in the same session. The only structural difference between those two paths is the one the audit found. Confirmed by elimination.
- [ ] **M-V2 — now a REGRESSION check, not a diagnostic.** Hold `L` and drag a floating window with the left button. It must track the pointer **instantly**, with no interpolation lag — `may_animate()` excludes move and resize mode, so the drag takes the snap branch. This is the one thing the M-1 fix must not have broken.
- [ ] **M-V3 — DEFERRED by the user 2026-08-25 13:37.** Splits "hikari bug" from "hardware/libinput", one command. `libinput debug-events` (or `--device`) while performing a 3-finger swipe. `GESTURE_SWIPE_BEGIN/UPDATE/END` present ⇒ the fault is in hikari and `src/cursor.c` is worth reading again. Absent ⇒ nothing in hikari can help and the trail leads to libinput/kernel/hardware.
- [ ] **M-V4 — DEFERRED with M-V3.** Only if M-V3 shows events. Swipe 3 fingers left, then right, and see whether the sheet changes. Also worth trying more travel than usual: classification needs 20.0 accumulated units.
- [ ] **M-V5 — DEFERRED with the rest of touch.** Tap a window (should focus and raise) and drag one (should move — though note this rides the same `move_view` sink as M-1, so a negative result here is expected to be M-1, not a touch bug).
- [x] **M-V6 — DONE by the user 2026-08-25 13:37: animation enabled and confirmed working.** Original text: Set `ui { animation { enabled = true } }` and open/close a window on a tiled sheet. This is the existing T14/T15/T16 block; Phase 92 adds only that it must be run on a **tiled** sheet, since re-tiling is the one path that currently reaches the animation at all.

**M-1 — `move_view()` does not reposition the scene node (CRITICAL, blocked on M-V1)**

- [x] **M-1a** **DONE.** Added the scene-node placement to the end of `move_view()` (`src/view.c:190`), after the fullscreen/maximized early-returns, guarded `scene_node != NULL && output != NULL` exactly as `hikari_view_refresh_geometry()` guards it. ~6 lines.
- [x] **M-1b** **DONE.** The move is offered to `hikari_animation_move()` first, mirroring `hikari_view_refresh_geometry()` term for term. `view-move-*` and `view-snap-*` now animate, which makes `hikari(1):1008` and `hikari.conf:127-131` true as written — so M-4a needs no text change. Verified that the XWayland path does **not** animate twice: `move_view` writes `geometry->x/y` before calling `view->move`, so the commit handler's inequality test (`src/xwayland_view.c:94-98`) is false and it skips its `refresh_geometry`. `src/animation.c` unchanged.
- [x] **M-1c** **DONE — the family is closed at three.** Enumerated every write to an origin reachable through `hikari_view_geometry()`: `view.c:219,230` (inside `move_view`, now covered), `view.c:976,984` (reaches `refresh_geometry` via `resize()`), `maximized_state.c:18-21` (via `commit_pending_geometry`), `xwayland_view.c:105-106` (calls it on the next line), `xwayland_unmanaged_view.c:35-36` (has its own `position_surface_tree`). Nothing else writes one. Original text: Two members are already on record (restacking, DECISIONS_LOG:1340; indicator show/hide, DECISIONS_LOG:2290) and `move_view` is the third — the question is whether there is a fourth. Sweep: every function that writes `view->current_geometry` or a `wlr_box` reached through `hikari_view_geometry()` without passing through `hikari_view_refresh_geometry()`.

**M-2 — the shipped resize bindings drift and cannot grow left or up (blocked on the user's choice, DECISIONS_LOG Phase 92 ambiguity 2)**

- [x] **M-2a** **DONE in BOTH `etc/hikari/hikari.conf` and the live `~/.config/hikari/hikari.conf`** — the live file is what is under test, so a repo-only change would have been untestable. Replaced `LC+Left` = `view-decrease-size-right` with `view-decrease-size-left`, and `LC+Up` = `view-decrease-size-down` with `view-decrease-size-up`, in **both** `etc/hikari/hikari.conf:588-591` and `~/.config/hikari/hikari.conf:560-563`. Makes grow and shrink exact inverses and removes the 100 px-per-press rightward/downward walk.
- [x] **M-2b** **DONE — option (b) taken.** The user did not choose between (a) and (b); (a) is a strict subset that fixes the drift but leaves the left and top edges unreachable, which is half the reported complaint. Reverting to (a) is deleting four lines. `LCS+arrows` verified free first (`LCS+Return/j/k/Home` taken, no arrows), duplicate-key scan clean afterwards. Bound the four origin-moving edge actions — `view-increase-size-left/up` and `view-decrease-size-right/down` — on `LCS+arrows`, which is unbound. These four actions are implemented and documented (`hikari(1):411-420`) and **currently unreachable from any binding**; this is the only thing that makes the left and top edges controllable from the keyboard.
- [x] **M-2c** **DONE in both files.** Rewrote the RESIZING BY KEY comment block in both config files to state the model plainly (which edge each action moves, and that the two families differ in whether the origin holds). The present comment says only "Each direction has an increase and a decrease", which is what made the asymmetry easy to miss.

**M-8 — `LA+Left` still does not move left after the M-1 fix (OPEN, blocked on one observation)**

*The user rebuilt at 13:54 and restarted at 13:56, so the running compositor has the M-1 fix and the resynced config. M-1 was necessary but **not sufficient**.*

- [x] **M-8-V1 — ANSWERED BY THE USER 2026-08-25 14:12: `LA+Left` ALONE fails. `LA+Right`, `LA+Up`, `LA+Down`, `LA+c` all work.** The failure is genuinely left-specific and was reported as such from the start; asking again was the error. Original text: Press **`LA+c`** (`view-move-center`) and **`LA+Right`**, and report which of the three (`LA+c`, `LA+Right`, `LA+Left`) do anything. `LA+c` is the useful one because it reaches `view.c:move_view()` via the `MOVE()` macro **without** going through `server.c:move_view(dx, dy)` — so it tests the `LA` modifier, the binding table and the view-level move while skipping the output-layout lookup.
  - all three fail → the `LA` family never dispatches; the fault is the modifier mask, not the move code.
  - `LA+c` works, `LA+Right` fails too → horizontal moves refused → chase `HORIZONTALLY_MAXIMIZED` / the output-lookup branch.
  - only `LA+Left` fails → genuinely left-specific, which **no path in the read source can produce**; instrumentation becomes the only way forward.
- [x] **M-8b — binding table CLEARED BY PROBE.** The real `src/binding_config.c` was compiled into a harness and fed all 108 keyboard bindings from the live config, reproducing `resolve_keysym()`/`match_keycode()` verbatim: `LA+Left` → mask 72, keycode 105, `view-move-left`; mask 72 holds exactly the five expected bindings; **zero collisions across all 108**; **zero unresolved (keycode 0)**; keycode 105 appears under four different masks (72/73/68/69) so none shadows another. `view-move-left` **is** dispatched.
- [x] **M-8c — constraint re-cleared at the USER'S OWN settings.** Live config reads `border = 1`, `gap = 5`, `step = 100` — the exact values the earlier harness used. `usable_min_x` only clamps a leftward move when the window's right edge is under `gap*2 - border = 9` px. Ruled out at real settings, not guessed ones.
- [x] **M-8a — APPLIED as an opt-in diagnostic** (not the permanent instrumentation originally proposed). `#ifdef HIKARI_DEBUG_MOVE` blocks in `server.c:move_view()` (dx/dy, geometry, output name + layout box, probe point, **what `wlr_output_layout_output_at()` returned**, branch taken) and `view.c:move_view()` (requested x/y, geometry, fullscreen, maximisation state, tiled, `usable_area`, border/gap; and the resulting geometry). Both files compile clean at `-Wall -Werror` **with and without** the flag, so default builds are unaffected.
- [x] **M-8-V2 — SUPERSEDED, and NO user action was needed.** The instrumented build's stderr goes to `/dev/ttyv1` (`procstat -f 4073`) with `HIKARI_LOG` unset, so the diagnostics were printed to the console and discarded. Instead the **running compositor (PID 4073) was read directly via `/proc/4073/mem`** — reads only, never stopped or attached to. Struct offsets were computed by compiling an `offsetof()` program against hikari's own headers with the exact macros from `make -V CFLAGS`, because the shipping build carries no DWARF.
- [x] **M-8d — REFUTED by live data.** `hikari_server.outputs` holds **exactly one** output: `eDP-1 {0,0 1920x1200}`, `usable_area {0,34 1920x1166}`. Nothing sits at negative x, so `wlr_output_layout_output_at()` returns eDP-1 or NULL and **both select the same `hikari_view_move()` branch**. The migrate path is unreachable. The output-topology hypothesis is dead.
- [x] **M-8f — the LIVE binding table is correct on all SIX keyboards.** `bindings[72]` = `105->move_view_left, 106->move_view_right, 103->move_view_up, 108->move_view_down, 46->move_view_center`, resolved against the binary's symbol table. `[73]/[68]/[69]` likewise correct, and the M-2 rebinding is confirmed live. `hikari_server.mode == &normal_mode`, so the handler that reads `bindings[modifiers]` is active. `animation.enabled=1 duration=120`.
- [x] **M-8g — THE KEY FINDING: the focused window has never moved, in ANY direction.** `animation = {active=0, placed=1, from=(-1,33), to=(-1,33), drawn=(-1,33), start_msec=0}`. `start_msec` is written only on the branch that begins an animation; with `enabled=1` and `placed=1`, `may_animate()` is true, so any successful move would have stamped a non-zero clock. `from==to==drawn` is the first-placement snap state. View is `flags=0x0000` (not floating/hidden/fullscreen/forced), `tile=NULL`, `maximized_state=NULL`, `scene_node` non-NULL, present in `output->views`. Geometry `{-1,33,1920x1166}` against `usable_min_x = -1911` — the constraint permits `x=-101` comfortably.
- [x] **M-8h — CLOSED. ROOT CAUSE: the keyboard never sends the keystroke.** 10 ms polling of `wlr_keyboard.keycodes[]` across all eight seat keyboards recorded **zero** occurrences of code 105 (LEFT) in the entire session, while 106/103/108 (RIGHT/UP/DOWN) each arrived six times and moved the window. Twelve `125(LOGO) 56(LALT)` chords were held with **no third keycode ever appearing**, each at `GLOBAL=72` on `dev#4 HS6209 2.4G Wireless Receiver Keyboard`. No substitute code appeared either (nothing tagged `UNMAPPED`), so Left emits **nothing at all** under that chord. **The `HS6209 2.4G Wireless Receiver Keyboard` cannot report LOGO+ALT+Left** — matrix ghosting / limited rollover on a low-cost 2.4 GHz keyboard. The built-in `AT keyboard` works because its matrix differs, exactly as the user observed. **Not a hikari defect**; every compositor component was individually cleared in the running process.
- [x] **M-8j — CLOSED BY THE USER 2026-08-29: "the issue was the specific keyboard in use, it's working now." No change made or needed. M-8 is closed entirely.** Original text: hikari cannot receive an event the keyboard declines to send. Options: bind `view-move-*` to `LA+h/j/k/l` (all four verified free, vim-adjacent); or try RIGHT Alt, which may clear the matrix conflict that LEFT Alt hits. Worth testing which chords the HS6209 can actually report before committing to a scheme.
- [x] **M-8k — the M-1 fix stands on its own.** `move_view()` genuinely never positioned the scene node; the capture confirms geometry now changes and redraws for `view-move-*` in every direction the keyboard can deliver. A real port-omission bug, third of a known family, fixed — just not this symptom. Either `hikari_server_move_view_left` is never entered (the key event does not reach `normal_mode:key_handler` with `modifiers == 72` despite a correct table), or it is entered and something reverts the geometry within a frame. The discriminator is `hikari_server.keyboard_state.modifiers` **while the chord is held** — unsamplable from a snapshot (at rest it reads 0).
- [x] **M-8i — passive watcher running, asks NOTHING of the user.** Polls `output->views` from `/proc/4073/mem` every 150 ms for 15 minutes, logging any geometry or animation change. **Reads only** — no stop, attach, patch or instrumentation, cannot destabilise the session. Captures the next ordinary `LA+Left` press and distinguishes "never moved" from "moved and reverted". `dtrace`'s pid provider would be faster but patches the live process and needs root — **deliberately not used on a working desktop.**
- [ ] **M-8-V2-orig** *(superseded)* `sudo make CFLAGS_EXTRA=-DHIKARI_DEBUG_MOVE install`, restart with `HIKARI_LOG` set (`start-hikari.sh:139` redirects stderr there), press **`LA+Left` then `LA+Right`**, and send the `[hikari/move-server]` and `[hikari/move-view]` lines. The `branch=` field on the server line is the one that matters.
- [x] **M-8d *(stale duplicate)* — ANSWERED 2026-08-29, and it changes nothing about M-8.** The topology **can** now be read, from a Wayland client rather than `wlr-randr`: `sofi -h` reports **two** outputs, `eDP-1` 1920x1200 and `DP-3` 1920x1080. But that is the topology of *today* — a second screen was attached after this entry was written, and the checked M-8d above refuted the hypothesis against a genuinely single-output machine on 2026-08-25. **M-8 is closed by M-8h regardless**: the `HS6209` keyboard never emits keycode 105 under `LOGO+ALT`, measured across the whole session on all eight seat keyboards. Do not reopen. *(The same reading exposed a live defect of its own — see Phase 94 / L-1.)* Original text: `view-move-left` and `view-move-right` are the same code with opposite signs; only the `wlr_output_layout_output_at()` probe and the constraint depend on that sign, and the constraint is now excluded by measurement. So either the output layout contains something to the **left** (a second output, or a layout box not starting at x=0), sending only leftward moves down `hikari_server_migrate_focus_view()`, or the failure is somewhere the source does not reveal. **The live topology could not be read from here** — `wlr-randr` is not installed and there is no DRM sysfs on this platform.
- [ ] **M-8e — REMOVE the diagnostic once the cause is found.** It is opt-in, so it may also simply be left in place; decide when M-8 closes.
- [ ] **M-8a-orig** *(superseded by M-8a above)* One-shot instrumented build: `fprintf` in `server.c:move_view()` and `view.c:move_view()` reporting the branch taken, `dx/dy`, pre- and post-constraint geometry, maximisation state and the fullscreen flag.
- [x] **M-8-R — ELIMINATED BY PROBE, do not re-investigate.** (1) The M-1 scene-node placement is present and correct in the installed source. (2) Keysym resolution is exact — a probe against `libxkbcommon` reproducing `match_keycode()` verbatim gives `Left`→105, `Right`→106, `Up`→103, `Down`→108, and `XKB_KEYSYM_CASE_INSENSITIVE` returns the same keysym as the case-sensitive lookup for every name in the config. (3) `hikari_calloc()` wraps `calloc(3)` and really zeroes, so `resolve_keysym()`'s `*keycode == 0` guard is sound. (4) The noop output is **not** in the output layout — both `wlr_output_layout_add()` calls sit inside `if (!noop)` (`output.c:449-583`), so a leftward lookup at negative x returns NULL and still calls `hikari_view_move()`. (5) `hikari_geometry_constrain_relative()` is symmetric — a harness linked against the **real `src/geometry.c`** moved a full-width tile, a left tile, a right tile and a floating window `-100` with zero clamping, and six presses from a left-edge tile walked `5 → -95 → … → -595` without stalling.
- [x] **M-8-N — recorded.** Every remaining silent no-op in `move_view()` is **direction-symmetric**: fullscreen and `FULLY_MAXIMIZED` refuse all moves, `HORIZONTALLY_MAXIMIZED` refuses **both** horizontal directions, `VERTICALLY_MAXIMIZED` both vertical ones. **No path refuses left while permitting right** — which is why M-8-V1 must establish whether the symptom really is left-specific before any more code is read.

**M-9 — GLOBAL modifier state is wrong for multi-device seats (REAL DEFECT, not the cause of M-8)**

- [ ] **M-9a** `hikari_server.keyboard_state.modifiers` is a single global written by `update_mod_state()` from `wlr_keyboard_get_modifiers()` of whichever device last fired a modifiers event, then read by `normal_mode:key_handler` to index that device's `bindings[modifiers]`. **This machine has EIGHT keyboard devices** — `System keyboard multiplexer`, `ACPI video extension`, `Power Button`, `AT keyboard`, and **four** from one `HS6209 2.4G Wireless Receiver` (two of them both named `Keyboard`). A modifiers event from any idle device can zero the mask under a chord held on another. **Live capture proved this is NOT what breaks `LA+Left`** (the global reached exactly 72 from the external keyboard and `LA+Right` fired), but the design is still wrong and should be fixed on its own merits — most likely by OR-ing the modifier masks across all seat keyboards, or by reading the chord from the device that sent the key.

**M-7 — config hygiene (added 2026-08-25 13:50)**

- [x] **M-7a** **DONE.** Resynced `~/.config/hikari/hikari.conf` from the template, carrying across the six live personal settings (animation on, font, `layout.auto`/`insert`, background path, terminal), each **extracted from the running file** rather than hardcoded, each anchored on a match asserted unique because three of them also occur in comment prose. `PREFIX` expanded as `make install` expands it. Backup: `~/.config/hikari/hikari.conf.bak-20260825-1350`.
- [x] **M-7b** **DONE — the `L+n` duplicate is gone.** Took the template's `L+bracketright`/`L+bracketleft` for sheet next/prev. `workspace-switch-to-sheet-next-inhabited` had been dead in the deployed config since Phase 91 (first binding wins; `L+n` was already `action-notifications`), so sheet navigation only worked in one direction. This was the **only** setting change the resync made.
- [x] **M-7c** **DONE.** Verified after writing: deployed-vs-template differs in exactly the six keepers; no duplicate binding keys; zero `PREFIX` tokens; brace depth 0 at EOF under a quote/comment-aware scan; wallpaper present at the kept path.
- [ ] **M-7d** **`make install-user` writes a broken background path — NOT fixed, needs approval.** `Makefile:408-409` runs `s,/share/backgrounds/hikari,${HOME}/.config/hikari,` *after* `s,PREFIX,${PREFIX},` has already made the path absolute, so the prefix is stranded: `/usr/local/home/orpheus497/.config/hikari/hikari_wallpaper.png`. Confirmed by running the real pipeline, not by reading it. Any fresh `install-user` produces a config whose wallpaper cannot load. Fix: anchor the second substitution on the full post-prefix path, or reorder it before the first.
- [ ] **M-7e** *(no action needed, recorded)* `/usr/local/etc/hikari/hikari.conf` is from the 11:41 `make install` and still carries the pre-M-2 walk-right bindings. It is only read when the user config is absent (`main.c:135-148`) and is refreshed by the next `make install`.

**M-3 — no directional movement or split resizing in a tiled layout (FEATURE, tabled, not planned)**

- [ ] **M-3a** *(tabled)* `layout-exchange-view-{left,right,up,down}` — directional tile exchange. Today the only movement within a layout is list-order `next`/`prev`/`main` (`src/action.c:249-257`).
- [ ] **M-3b** *(tabled)* A split-ratio action. There is currently **no** runtime way to change a split; `scale`/`min`/`max` are read from the `layouts` register at apply time only.
- [ ] **M-3c** Document the present limitation in `hikari(1)` regardless of whether M-3a/b are ever built — with `layout { auto = true }` shipped and in live use, "move this window left" having no expression is a thing a user will look for and not find.

**M-4 — motion is off, minimal, and over-documented**

- [x] **M-4a** **RESOLVED BY M-1b, no text change needed.** The claim became true rather than being amended away. Original text: `hikari(1):1008` and `etc/hikari/hikari.conf:127-131` both claim animation smooths `view-snap-*` and `view-move-*`. It does not — those paths never reach it. Either fix M-1 (which makes the claim true) or amend both texts; **doing neither leaves the manual lying.**
- [ ] **M-4b** *(user decision)* Tuning: default `duration`, and whether `ease-in-out` should be preferred for long journeys.
- [ ] **M-4c** *(user decision, new capability)* Map/unmap opacity fades via `wlr_scene_buffer_set_opacity`. There is currently no opacity call for any view node anywhere in `src/`.
- [ ] **M-4d** *(user decision, largest)* Sheet/workspace-switch transitions. Needs the outgoing sheet's nodes kept alive for the transition; today `hikari_view_show/hide` is a bare `set_enabled`.
- [ ] **M-4e** Animate the indicator bars with the window they annotate. They are independent top-level nodes and teleport to the destination at commit time while the window travels — visible the moment M-1 and animation are both on.
- [x] **M-4f** Resize animation — **remains deferred** per the user's 2026-08-25 ruling (B-3). Phase 92 reopens nothing.

**M-5 — trackpad gestures (blocked on M-V3; no defect found in the source)**

- [ ] **M-5a** No code task exists until M-V3 says the hardware emits gesture events. The full path — protocol creation, device attach, all eight listeners, config parse, classification thresholds — was traced and is sound.
- [ ] **M-5b** *(documentation)* State in `hikari(1)` that the buffer-and-replay design means **continuous client-side gesture feedback cannot work** (GTK pinch-zoom tracking the fingers, browser back-swipe animating as it goes). The manual documents the buffering mechanism at 1492-1497 but not this consequence, and it is the part a user will actually notice.

**M-6 — touchscreen gestures (SCOPED OUT, documented at the user's request)**

- [x] **M-6a** Recorded: basic touch is implemented and looks correct — device tracking, per-panel confinement by EDID name, `WL_SEAT_CAPABILITY_TOUCH`, real `wl_touch` events, first-finger-as-click with correct release on up/cancel/deactivate.
- [x] **M-6b** Recorded: **touchscreen gestures do not exist and cannot arrive via the current path.** `wlr_pointer_gestures_v1` carries libinput's *touchpad* recogniser only; libinput does not synthesise gestures from a touchscreen. No edge swipe, no multi-finger touchscreen swipe, no compositor-side pinch, no long-press, no on-screen-keyboard trigger, no kinetic scrolling.
- [ ] **M-6c** *(scoped out unless the user says otherwise)* A touch-point gesture recogniser in `src/cursor.c` over the existing `touch_down/motion/up` handlers, plus its config surface. This is a feature of real size, not a fix.


### Phase 91: Layouts, motion, palette — IMPLEMENTED, COMPILED AND LINKED (not run)

*(Analysis: `DECISIONS_LOG.md` Phase 91. Architecture: `BLUEPRINT.md` §18.)*

**User rulings recorded:** resize animation **deferred**; hidden views are **unhidden and added to the layout**; work sequenced in procedural order (A → B → C → D), not by ease.

#### WP-A — automatic re-tiling

- [x] **A-1** `layout { auto | insert | reflow-on-close | default-register }`, new top-level section. Singular, so it cannot be confused with `layouts` (the registers) — a policy key in that block would be read as a register name.
- [x] **A-2** `src/layout_policy.c` — defaults reproduce historical behaviour exactly; an unresolvable `default-register` falls back to the per-sheet register rather than disabling the feature.
- [x] **A-3** `src/reflow.c` — request-and-drain. **Never re-tile where the request is made:** a newly mapped view is dirty and would be the one window omitted.
- [x] **A-4** Queue link lives in `hikari_sheet`, self-linked when idle; `hikari_workspace_fini()` cancels. A freed sheet left queued leaves the static head in freed memory.
- [x] **A-5** `drain()` clears `idle_source` first — libwayland destroys an idle source while dispatching it.
- [x] **A-6** Deferred sheets stay queued and are **not** re-armed from inside the handler (busy loop).
- [x] **A-7** Lock mode **drops** requests; `hikari_view_map()` schedules only on its non-locked branch.
- [x] **A-8** `display_sheet()` re-offers, since drain drops requests for non-visible sheets.

#### WP-B — motion

- [x] **B-1** Grab anchor in `move_mode` and `resize_mode`. Corner warp retained for the pointer-not-over-the-window case, so the old path is preserved.
- [x] **B-1a** Fixes the `border`-pixel shrink on every entry into resize mode.
- [x] **B-2** `src/animation.c` — position interpolation, three easings, tick from `frame_handler()`.
- [x] **B-2a** `node_at()` applies `hikari_animation_offset()`. hikari hit-tests through its own geometry, not the scene graph.
- [x] **B-2b** Refused for: disabled, duration 0, unplaced, no node/output, hidden, lock mode, **and interactive move/resize**.
- [ ] **B-3** Resize animation — **deferred by user decision, 2026-08-25.** Only a stale-buffer scale is available. Re-open only if the user asks.

#### WP-C — the palette

- [x] **C-1** `ui { palette { color0..color15 } }`; semantic slots derived from it.
- [x] **C-2** Palette resolved by explicit lookup **before** `parse_ui()` iterates, so file key order cannot change meaning.
- [x] **C-3** A palette entry may not reference another (`parse_color()` takes NULL there).
- [x] **C-4** `foreground` wired to the indicator text — it was a hardcoded black equal to the key's own default.
- [x] **C-5** `grouped` / `first` wired to group indicator frames, the sites `hikari(1)` already described.
- [x] **C-6** Palette handed to `hikari-topbar` as `argv[1]`, built in the parent; pywal retained as fallback.

#### WP-D — geometry and documentation

- [x] **D-1** `grid` border accounting: per-gap, not per-cell.
- [x] **D-2** Hidden views unhidden before layout, once, so all six algorithms agree.
- [x] **D-3** `etc/hikari/hikari.conf` rewritten — every tunable documented with its default and the reasoning.
- [x] **D-4** `hikari(1)`: **LAYOUT POLICY** section, **Palette** and **Animation** subsections, colourscheme defaults, and the no-auto-insert paragraph amended to point at the opt-in.
- [x] **D-5** `BLUEPRINT.md` §16's "hikari adds no animation" amended — it is no longer true and would mislead.

#### Follow-up review findings (2026-08-25 09:58) — all four verified valid and fixed

- [x] **R-1** `hikari.conf` media comment described `mixer` while the commands were `pactl`. Corrected, and the porting advice reversed — `pactl` is the portable half, `backlight` the FreeBSD-specific one.
- [x] **R-2** `L+n` bound twice. **Probed the parser rather than assuming:** the *first* binding wins and the second is dropped with no diagnostic, so `workspace-switch-to-sheet-next-inhabited` was dead. The user's binding kept; mine moved to `L+bracketright`/`L+bracketleft` (both directions, to keep the pair adjacent). Keysyms verified against an invalid-keysym control.
- [x] **R-3** `hikari_animation_offset()` recomputed the position for *now* instead of reporting the last placement. **279px error at peak — 35% of an 800px journey.** Now records `drawn_x`/`drawn_y` at the three sites that move the node; the retarget origin reads it too, which also removes a forward jump on mid-flight retarget that the finding did not mention.
- [x] **R-4** An unmap could strand a deferred re-tile — it is the one path that stops a view blocking a drain without passing through `hikari_view_commit_pending_operation()`. One `hikari_reflow_settle()` after the unlink.

#### Footgun closed (2026-08-25 10:08) — duplicate configuration keys are no longer silent

- [x] **F-1** Established libucl's actual behaviour by probe: a repeated key chains via `->next`, and `ucl_object_iterate_safe()` yields only the head **regardless of `expand_values`** — so no parser could ever have seen the discarded values.
- [x] **F-2** Checked the false-positive risk before writing the check: array elements carry neither key nor chain. Both are tested regardless.
- [x] **F-3** `include/hikari/config_key.h` — header-only `static inline`, no new object, no Makefile change.
- [x] **F-4** Called at **all 31 key-iteration sites across five files**. Covering only bindings was rejected: partial coverage teaches "no warning means no duplicate", which is worse than uniform silence.
- [x] **F-5** Warns rather than rejects — a config carrying a duplicate for months must not stop the desktop booting after an upgrade.
- [x] **F-6** Coverage suite: 31 sites planted with duplicates, **31 reachable, 0 unreachable**, every context string correct.
- [x] **F-7** Documented in `hikari(1)` (*Duplicate keys*, incl. the stderr/`HIKARI_LOG` caveat) and the `hikari.conf` header.
- [x] **F-8** All three build configurations clean — release, `DEBUG=YES` (`-Werror`), `WITH_ALL=NO`.

#### Correction to the footgun fix (2026-08-25 10:23) — my own comment was false

- [x] **F-9** `config_key.h` claimed `ucl_object_tostring_forced()` returns NULL for objects/arrays, and gated rendering on that. **Untrue** — verified against libucl 0.9.4, it returns the literal strings `"object"` and `"array"`, so a duplicated nested block printed `in effect: object / ignored: object`. A real defect in a diagnostic meant to make silence legible, not just a stale comment.
- [x] **F-10** Rendering now gated on `ucl_object_type()` via `config_key_is_renderable()` — scalars in, containers and `UCL_USERDATA` out. The warning line still fires for containers; the key identifies them.
- [x] **F-11** Re-verified: containers omit values, scalars still render, 31/31 sites reachable, shipped config silent, all three build configurations clean.

#### Battery charge bands (2026-08-25 10:54)

- [x] **B-1** Battery block was one fixed colour at every level; a flat battery looked like a full one. Banded per the user's ladder.
- [x] **B-2** `battery_color_index()` returns a palette **index**, so the bands follow `ui { palette }` and need no config surface of their own.
- [x] **B-3** "Plugged in" derived from the **raw ACPI flags**, not from `bat_state` — that label collapses CRITICAL-alone onto `"AC"`, which would have painted a critically flat battery in the mains colour. Only `s == 0` and `s & 2` set it; everything ambiguous falls through to the bands.
- [x] **B-4** Boundaries tested at both ends of all seven ranges; external power asserted to win at every level 0-100; all 7 bands proven reachable.
- [x] **B-5** Documented in `hikari.conf` and `hikari(1)`, including the correction to the earlier "the palette entries have no meaning on their own" claim.
- [x] **B-7** `hw.acpi.acline` made authoritative for "plugged in", with the flag inference retained as fallback. Closes the gap where a plugged-in machine reporting an unrecognised ACPI flag combination was coloured as discharging. Precedent: `lock_config.c:77` already reads it for the same question.
- [x] **B-8** Verified against the **real** `get_bat_info()` by mocking `sysctlbyname` at preprocessing time — 17 combinations, including mains+CRITICAL (now external, the gap closed) and acline-absent+CRITICAL (still not external, the original hazard still guarded). Live binary on mains emits `#9fa0a6`.
- [ ] **B-6** **User test:** unplug and watch the battery block change colour through the bands; confirm it goes grey on reconnecting the charger. Needs a compositor restart to pick up a palette change.

#### Verification done here

- [x] 71 translation units, 0 warnings, **both binaries link** (native FreeBSD clang 19.1.7).
- [x] Shipped config parsed by **hikari's own `hikari_configuration_load()`**, linked against the real objects.
- [x] 104 libucl structural assertions over the shipped config.
- [x] Every new knob set non-default and read back; **8 rejection paths**, each with a specific diagnostic.
- [x] `grid` arithmetic over **528** configurations.
- [x] Topbar palette intake: 8 cases, clean under **ASan+UBSan**.
- [x] Man page converts with pandoc.

### Phase 91 — tests only the user can run

**Status 2026-08-25 09:06:** the user built in-tree and reports the compositor working. Items marked `[x]` below are the ones a plain run against the **shipped** config actually exercises. **T2–T10 and T14–T16 are still open and cannot have passed** — they need `layout { auto = true }` and `ui { animation { enabled = true } }`, and both ship off.

- [x] **T1** Default config: open and close windows. **Nothing should re-tile** — `auto` is false.  *(implied by the 09:06 run; re-check if anything looks off)*
- [ ] **T2** Set `layout { auto = true }`, apply `LC+g` (grid), open a window. It should join the grid and the others should resize.
- [ ] **T3** With `auto = true`, close a window from a grid. The survivors should close the gap.
- [ ] **T4** With `auto = true` and no layout applied and no `default-register`, open windows. The sheet should stay **stacking** — not silently start tiling.
- [ ] **T5** Set `default-register = "g"` and open a window on a fresh sheet. It should adopt the grid.
- [ ] **T6** Set `insert = prepend`. A new window should become the **main** window; existing ones keep their relative order.
- [ ] **T7** Set `reflow-on-close = false` with `auto = true`. Opening should re-tile, closing should **not**.
- [ ] **T8** Open a window on a **background** sheet (via a view config), then switch to that sheet. It should be incorporated on arrival.
- [ ] **T9** Lock the screen, have something map (e.g. a timed launch), unlock. **No crash, no assert** — the request must have been dropped.
- [ ] **T10** Open several windows rapidly (a script launching four terminals). All four should end up in the layout — this is the dirty-view deferral doing its job.
- [x] **T11** `L+left`-drag a window **from its middle**. It must not jump; the grab point must stay under the pointer.  *(implied by the 09:06 run; re-check if anything looks off)*
- [x] **T12** `L+right` on a window and release **without moving**. The window must not change size at all (this was the `border`-pixel shrink).  *(implied by the 09:06 run; re-check if anything looks off)*
- [ ] **T13** `L+m` with the pointer far from the focused window. The old corner-warp behaviour should be intact.
- [ ] **T14** Enable animation (`enabled = true`), apply a layout. Windows should slide. Try all three easings.
- [ ] **T15** With animation on, **click a window while it is travelling.** It must select the window where it is *drawn*. This is the `node_at()` offset.
- [ ] **T16** With animation on, drag a window. Dragging must be **instant**, with no lag behind the pointer.
- [x] **T17** Confirm the palette: borders should be `#f0edf2` focused / `#5e5966` unfocused, desktop `#2b1e3a`.  *(implied by the 09:06 run; re-check if anything looks off)*
- [ ] **T18** Hold Logo with two windows in the same group. The focused one should frame in `#aba0d9`, the group's first in `#8e7cc3`, the rest in `#b18fc7`. **Release Logo — every frame must disappear.**
- [ ] **T19** Change `palette { color15 }`, reload with `LS+r`. The focused border should follow immediately.
- [ ] **T20** Restart the compositor and check the top bar takes the palette (it cannot pick it up on reload — spawned once).
- [ ] **T21** Remove `~/.cache/wal/colors` and restart. The bar must still be themed, from the compositor's palette.
- [x] **T22** Apply `LC+g` with 4 windows and measure the cells. The top-left should now be within a pixel or two of the others, not `2 * border` larger.  *(implied by the 09:06 run; re-check if anything looks off)*
- [ ] **T23** `L+h` a window, then apply a layout. It should **reappear and take its slot** — no gap.
- [ ] **T24** Reload with a deliberately broken value (`easing = swoosh`). The old config must stay in force and the error must name the key.

## Previously Active

### Phase 90: Client-driven fullscreen + top-bar media overflow — CYCLE 1 + W-A/W-B IMPLEMENTED, UNBUILT

*(Analysis: `DECISIONS_LOG.md` Phase 90. Plan: `PLANS.md` item -16.)*

**Scope ruling from the user:** no new keybinding — `L+f` stays maximize. Fullscreen is a **client protocol request** (F11, a video player's own button) and is answered where the client asks for it. Bar coverage **(a) now**; layer-shell coverage **(b) tracked as FS-2, not built**.

#### Cycle 1 — W-1 + W-2 + W-3 (native Wayland; `src/view.c` heavy, ships alone)

- [x] **W-1.1** `FLAG(fullscreen, 5UL)` (`view.h:176`); `struct wlr_box fullscreen_geometry` by value; `HIKARI_OPERATION_TYPE_FULLSCREEN` (`operation.h`).
- [x] **W-1.2** Fullscreen branch atop `refresh_geometry()` (`view.c:792`), **above** the `maximized_state` test. This is the line that makes exit-from-fullscreen restore correctly with no bookkeeping.
- [x] **W-1.3** `queue_fullscreen()` — `{0, 0, output->geometry.width, output->geometry.height}`, **`op->center = false`** (Finding 7).
- [x] **W-1.4** `commit_fullscreen()` — sets flag, stores box, `HIKARI_BORDER_NONE` unconditionally, **leaves `maximized_state` untouched**.
- [x] **W-1.5** `queue_unfullscreen()` — clears flag, re-queues maximized / tiled / floating as found.
- [x] **W-1.6** `hikari_view_set_fullscreen()` — the single D8 entry point; **defers** on `is_dirty` rather than dropping (Finding 5).
- [x] **W-1.7** Two switch cases. `xdg_view.c:82-97` gets **`WLR_EDGE_NONE`** for fullscreen — grouped with RESET/UNMAXIMIZE, **not** with the maximize cases.
- [x] **W-1.8** **Flag-clearing audit, 9 sites** — `commit_reset` `:810`, `commit_unmaximize` `:1528`, `commit_tile` `:1438`, `toggle_vertical_maximize` `:1709`, `toggle_horizontal_maximize` `:1734`, `toggle_floating` `:1751`, `hikari_view_unmap` `:1211`, `hikari_view_fini` `:440`, `hikari_view_evacuate` `:1610`. **A stranded flag leaves the bar hidden forever.**
- [x] **W-1.9** New `is_fullscreen` early-returns in `move_view()` `:194` and `queue_resize()` `:851` — the existing `FULLY_MAXIMIZED` guards do not fire for a **floating** window that went fullscreen.
- [x] **W-2.1** `bool obscured` on `struct hikari_bar`. **`hikari_bar_reserve()` (`bar.c:650`) must not read it** — else `usable_area` changes and every tiled window reflows (D5).
- [x] **W-2.2** Move `hikari_bar_refresh()`'s cache-hit early-return **below** the obscured check, or a cached frame skips the visibility change.
- [x] **W-2.3** `hikari_bar_update_visibility()` over `output->workspace->views` (the *visible* list — a fullscreen window on another sheet must not hide the bar).
- [x] **W-2.4** **Five call sites** — `show` `:1299`, `hide` `:1334`, `unmap` `:1211`, `commit_pending_operation` `:2236`, **and `reset_visibility()` in `lock_mode.c`**, which writes `set_hidden`/`unset_hidden` **directly, bypassing show/hide**, so the other four miss it.
- [x] **W-3.1** Rewrite `apply_requested_fullscreen()` (`xdg_view.c:656`) — delete the `!= hikari_view_is_fully_maximized(view)` guard. **This is Finding 1, the operative defect.**
- [x] **W-3.2** Same substitution at the map-time reconciliation (`xdg_view.c:252-255`).
- [x] **W-3.3** Deferred re-apply drained in the existing `commit_handler` (Finding 5).
- [x] **W-3.4** **Add the missing `request_maximize` listener (Finding 4 — a protocol violation** per `wlr_xdg_shell.h:212-219`). Registered at new_toplevel time; removed in `toplevel_destroy_handler` or wlroots asserts on teardown. **Makes a client's own maximize button work for the first time.**
- [ ] **W-3.5 — DEFERRED to its own cycle, not done.** `requested.fullscreen_output` is left unhonoured; a client naming another output gets fullscreen on its current one. The only API for moving a view between outputs, `hikari_view_migrate()`, is a full visibility transition (unlink → re-constrain both geometries → migrate sheet → show), and driving that from inside a protocol handler on the same path being fixed would bundle two risky changes into one build cycle. Sequencing rule, Phases 75/78. Recorded at the call site in `xdg_view.c` as well as here.

#### Cycle 2 — W-4 + W-5 (XWayland + foreign-toplevel)

- [ ] **W-4.1** **`request_fullscreen` listener — Finding 2.** hikari registers 10 listeners and this is not one of them, so **mpv, VLC, Steam and X11 browsers cannot go fullscreen at all**. Removal added to the 9-link block at `xwayland_view.c:277-286`.
- [ ] **W-4.2** `request_configure_handler` (`:333-366`) must not clamp a fullscreen view to `usable_area` — Finding 3, independent of W-4.1.
- [ ] **W-4.3** Map-time reconciliation from `surface->fullscreen` (`xwayland.h:182`).
- [ ] **W-4.4** D7 guard in both commit handlers' else-branches — they write surface dimensions back through `hikari_view_geometry()`, which under D3 is `&view->fullscreen_geometry`.
- [ ] **W-4.5** All 10 → 11 listeners re-verified for exactly-once removal (Phase 57/78 precedent).
- [x] **W-5 DONE 2026-08-24 11:42** (pulled forward from cycle 2 by an external review finding, verified valid against current code). `request_fullscreen_handler` now routes to a new thin `set_fullscreen()` → `hikari_view_request_fullscreen()`; `request_maximize_handler` → `set_full_maximize()`, **unchanged**. `publish_state` reports maximized from `hikari_view_is_fully_maximized()` and fullscreen from `hikari_view_is_fullscreen()`, independently. Both now-false comments replaced. **Note the two are not mutually exclusive** — fullscreen shadows maximization, so a maximized window that a client fullscreens correctly reports both, and reports only maximized once released. 0 warnings in all three configurations. **Still unbuilt/unverified on hardware.**

#### W-A / W-B — top-bar media overflow (independent subsystem, any order)

- [x] **W-A.1** Per-run right limits; clamp `center_x`/`right_x` to `>= PADDING` — **both can currently go negative** and draw off the left edge.
- [x] **W-A.2** `cairo_save`/`cairo_rectangle`/`cairo_clip`/`cairo_restore` per block. Chosen over `pango_layout_set_width` + ellipsize, whose wrap/ellipsize interaction is too version-dependent to be the tool for a *guarantee*.
- [x] **W-A.3** Local `utf8_valid_prefix_len()` before `pango_layout_set_text()`. **This is a live latent bug today** — `json_string_field()` copies bytes with no validation and both upstream truncations cut on byte boundaries.
- [x] **W-A.4** Correct two false comments: `bar.c:32-34` (inter-run padding that does not exist) and `bar.c:41-44` (`HIKARI_BAR_MAX_BLOCK_WIDTH` bounds only `min_width`, never rendered text).
- [x] **W-B.1** `int scroll_offset` on `struct hikari_bar_block`; `parse_line()` (`:233`) carries it forward on identical text, resets on change.
- [x] **W-B.2** Render `max_chars` **codepoints** from `full_text + separator` modulo the combined length, so the banner wraps continuously rather than snapping back.
- [x] **W-B.3** `scroll_offset` into **both** the sizing and writing `snprintf` calls in `build_cache_key()` (`:291`) — they are duplicated and must stay in sync.
- [x] **W-B.4** `wl_event_source *scroll_timer`, armed **only while a block overflows** (zero wakeups on an idle desktop), torn down in `hikari_topbar_source_fini()`.
- [x] **W-B.5** `ui { bar { max-block-chars = 26; scroll-interval = 300; scroll-separator = "   *   "; } }` following the `hikari_lock_config` pattern; `0` disables. **Must be compositor-side** — the helper is `execl`'d once with no argv/env and has no reload path, so a knob in `topbar.c` could never be reloaded.

#### TEST (USER-RUN) — cycle 1

- [ ] **1. Video fullscreen from a MAXIMIZED browser** — the reported bug (Path B). Covers whole screen, bar gone.
- [ ] **2. Exit that fullscreen (Path B')** — **returns to maximized**, not un-maximized.
- [ ] **3.** Video fullscreen from a floating window (Path A). **4.** Exit restores size and position.
- [ ] **5.** F11 in a browser — same as 1–4.
- [ ] **6.** `L+f` on an ordinary window — **unchanged**, maximizes below the bar, bar visible.
- [ ] **7.** `L+x` / `L+y` while fullscreen — drops fullscreen cleanly, bar returns. *(Targets the stranded-flag risk directly.)*
- [ ] **8.** Client's own CSD maximize button — **now works**; it never has.
- [ ] **9.** Fullscreen, switch sheet — bar returns on the other sheet. **10.** Lock and unlock — bar state correct. **11.** Close the window — bar returns.
- [ ] **12.** Fullscreen on the second monitor — bar hidden on **that output only**.
- [ ] **13.** Tile several windows, fullscreen one, exit — **no tiled window moves at all** (the D5 trap).

#### TEST (USER-RUN) — cycle 2

- [ ] **14.** `mpv` fullscreen (`f`) — covers whole screen. Currently impossible.
- [ ] **15.** VLC / Steam Big Picture. **16.** Exit restores previous geometry.
- [ ] **17.** X11 window that self-reconfigures while fullscreen — stays fullscreen (W-4.2).
- [ ] **18.** waybar taskbar: maximize vs fullscreen buttons are now **distinct operations** (W-5).

#### Found during execution (2026-08-24 10:09) — see DECISIONS_LOG Phase 90 cycle 1

- [x] **Name collision caught by the IDE on the first edit.** `FLAG(fullscreen, 5UL)` generates `hikari_view_set_fullscreen(view)`, colliding with the planned public setter of the same name. Renamed to **`hikari_view_request_fullscreen(view, bool)`** — the better name anyway: it answers a request and may decline it.
- [x] **Use-after-free in my own first draft of `queue_unfullscreen()`.** Restoring a *tiled* view as `queue_tile(view, view->tile->layout, view->tile, false)` reads correctly and is wrong: `commit_tile()` frees the current tile then assigns `operation->tile`, so passing the same pointer frees it and stores the dangling value. `queue_reset()` is not a substitute either — it frees the tile outright, dropping the window from its layout. Replaced with a plain RESIZE to `tile->view_geometry`, which touches no ownership. **Phase 55 class; caught by reading `commit_tile()`, not by the compiler.**
- [x] **Uninitialised state, twice.** `hikari_xdg_view` comes from `hikari_malloc` (non-zeroing) and `drain_pending_state()` runs on the first commit, so the four `pending_*` booleans are now cleared in `hikari_xdg_view_init()`; `fullscreen_geometry` zeroed in `hikari_view_init()`. **`view->flags` was checked and is already zeroed** — the fullscreen bit could never start set.
- [ ] **PRE-EXISTING, not fixed, do not lose:** `hikari_view_toggle_horizontal_maximize()` lacks the `if (hikari_view_is_dirty(view)) return;` guard that its vertical twin has, so it can queue over an unacked operation. Unrelated to fullscreen; belongs in its own change.

#### IPC audit (`src/ipc.c`, arrived from a concurrent session 09:03–09:05) — 3 defects fixed

- [x] **CRITICAL — no mode gating at all.** `hikari_view_pin_to_sheet()` asserts `!is_hidden` and calls `hikari_view_hide()`, which asserts `!is_forced`; **lock mode forces every view**, so `pin` on a locked session violates that — and under `-DNDEBUG` it corrupts the visibility linkage rather than aborting (Phase 55 class). **The exact hazard Phase 89 gated with `can_act()`, reproduced.** Fixed with one gate in front of the whole command table so a later command cannot be forgotten. One test closes both the modal-abort hole and *an external process switching sheets on a locked screen*; `state` is gated too, since its view counts leak how many windows are open.
- [x] **MEDIUM — `close(0)`.** `hikari_server` is a global, so `ipc_fd` is **0** if setup never ran, and `if (ipc_fd >= 0) close(ipc_fd)` would close **stdin**. The client list was guarded against exactly this; the descriptor was not. Now `> 0`.
- [x] **MEDIUM — `pin` could queue over an in-flight resize.** One `pending_operation` slot per view; every in-tree caller of that class already guards on `is_dirty`, and an IPC request is the one whose timing the compositor does not control.
- [ ] **Reported, not changed:** `hikari_ipc_setup()` is called from inside `setup_xdg_activation()`. Layering wart, predates this module (`hikari_foreign_toplevel_manager_setup()` is there too). Not this phase's business.
- [ ] **USER-RUN — IPC cannot be verified beyond compilation from here.** On the next run check the `WLR_INFO` line naming the socket path, then: `printf 'state\n' | nc -U $XDG_RUNTIME_DIR/hikari.sock` returns sheet/output/counts; `sheet 3` switches; `pin 2` moves the focused window; **and all three return `error compositor busy` while the screen is locked.**

#### W-A/W-B execution notes (2026-08-24 11:35) — see DECISIONS_LOG

- [x] **Two more origin bugs found beyond the reported one.** `center_x` and `right_x` could both go **negative** when a run was wider than the output, drawing that run off the left edge and across the left run. Never reachable via the media block, and the character cap would not have covered it. Both clamped.
- [x] **The display buffer nearly became a stack problem.** A fixed `char[MAX_BLOCKS][...]` is ~131KB of stack per refresh at the original 1024-codepoint config bound, and cannot hold the text at all when capping is disabled. Resolved by carrying **(pointer, byte length)** — a block that fits points straight at `full_text` with its valid-UTF-8 prefix length, no copy; only a scrolling block needs a buffer, bounded by `HIKARI_BAR_MAX_CAP_CHARS` (256).
- [x] **Second use-after-free avoided.** Carrying `scroll_offset` across parses by snapshotting the old blocks and then calling `clear_blocks()` frees the very strings the comparison reads. Made an ownership **transfer** instead, released at the end of `parse_line()`.
- [x] **UTF-8 validation is fixing a LIVE defect, not a consequence of the cap.** `fgets` into `mpris[128]` and `json_escape` both truncate on byte boundaries and `json_string_field` copies without inspecting, so accented/CJK titles already reached Pango cut mid-sequence. Local decoder rejects overlong forms, surrogates and >U+10FFFF.
- [x] **UNIT-TESTED standalone, clean under ASan+UBSan.** The real translation unit is included by the test rather than the algorithm reproduced. Verified: malformed sequences rejected; truncated `é` yields the valid prefix; under-cap blocks untouched; banner wraps through the separator and back; multibyte titles step one codepoint at a time and **never** produce a cut sequence at any offset.
- [x] **Config parses with real libucl** (`max-block-chars=26 scroll-interval=300 scroll-separator="   •   "`); **man page converts with pandoc**.

#### TEST (USER-RUN) — top bar

- [ ] **Play something with a long title.** It must cap at 26 characters and scroll as a banner, wrapping through `   •   ` back to the start.
- [ ] **It must never paint under or across the clock, the network/volume/battery icons, or the right-hand run.** This is the reported bug.
- [ ] **A short title must not scroll** and must render exactly as before.
- [ ] **Pause/stop media** — the block goes to `Idle`, and the scroll timer must disarm (no ongoing repaints).
- [ ] **A non-ASCII title** (accents, CJK, emoji) must scroll without ever showing a replacement glyph or a cut character.
- [ ] **Reload the config** with a different `max-block-chars` — it must take effect without restarting the session, which is the whole reason this lives compositor-side.
- [ ] **Note:** a deployed `~/.config/hikari/hikari.conf` keeps the built-in defaults until a `ui { bar { ... } }` block is added to it — same caveat as the Phase 60 `bar` colour.

#### External review findings triaged 2026-08-24 11:42 — 2 of 4 valid

- [x] **VALID, FIXED — foreign-toplevel fullscreen used the maximize lifecycle.** This was W-5, already scoped for cycle 2; the finding matched the existing analysis and was verified still current. See W-5 above.
- [x] **VALID, FIXED — accepted IPC descriptor had no FD_CLOEXEC.** `accept(2)` returns a descriptor with FD_CLOEXEC **clear**; the listener's `SOCK_CLOEXEC` does not propagate to it. It matters because `hikari_command_execute()` (`src/command.c:14-19`) forks twice and execs `/bin/sh` with **no descriptor hygiene at all** — no `closefrom()`, unlike the topbar (`bar.c:990`) and unlocker (`lock_mode.c:156`) helpers — so every keybinding- or autostart-launched application inherited any open control-socket connection. Fixed with one `fcntl(F_SETFD, FD_CLOEXEC)`; `accept4()` was not used since the only argument for it is an accept→fcntl fork race and hikari is single-threaded.
- [ ] **REJECTED — "default WITH_FOREIGN_TOPLEVEL_MANAGEMENT to NO until a global filter authorizes trusted clients".** The protocol was added in Phase 89 **at the user's explicit request**, for their sofi window switcher; defaulting it off removes that feature and is barred by AGENTS.md section 3. The underlying observation is fair — it is a privileged protocol any client can bind — but the proposed remedy disables a requested feature rather than fixing anything, and hikari has no client-trust model for a filter to consult (the IPC socket grants comparable powers to anything that can reach `$XDG_RUNTIME_DIR`). **A `wl_display_set_global_filter` is a legitimate future workstream, not a minimal fix; recorded here rather than actioned.**
- [ ] **REJECTED — "use send() with MSG_NOSIGNAL instead of write() in ipc.c write_all()".** The implied hazard is that writing to a peer that has closed raises SIGPIPE and kills the compositor. **`signal(SIGPIPE, SIG_IGN)` is already set unconditionally in `server_init()` (`src/server.c:1528`)**, before the event loop exists, so `write()` returns `-1`/`EPIPE`, `write_all()` returns false, and the caller closes the connection — which is the intended behaviour and is already correct. The change would be behaviourally a no-op while adding a portability constraint.

#### Review finding triaged 2026-08-24 11:52 — 1 of 1 valid

- [x] **VALID, FIXED — `build_cache_key()` omitted the display parameters, so a config reload did not invalidate the repaint cache.** The key carried per-block state (`full_text`, `min_width`, `align`, `scroll_offset`, colour) but not `max_block_chars` or `scroll_separator`, both of which change the pixels for identical telemetry — the first decides whether a block is capped and how wide its window is, the second is spliced into the scroll cycle and changes its period. **`hikari_configuration_reload()` does not repaint bars either** (verified: it refreshes view geometry, pointers, keyboards, outputs and switches, and touches nothing bar-related), so the only repaint path is the 200ms telemetry tick — which this cache gated. The bar therefore kept rendering at the OLD cap until some block's text changed on its own; usually the next CPU-percentage tick, but indefinitely on an idle machine with steady telemetry. **That contradicted the behaviour documented in `hikari.conf` and the man page**, which state these keys take effect on reload rather than needing a restart.
  Fixed by prefixing the key with both values. `scroll_interval` deliberately **not** included — it changes only how often a step is taken, never what a frame looks like, and both timer paths re-read it live.
  The two duplicated literal format strings were replaced by a `key_append()` varargs helper in the same change, so the sizing pass and the writing pass can no longer drift — which is the failure the old code's own comment warned about.
  **Unit-tested under ASan+UBSan:** identical config still yields an identical key (the cache genuinely still works, no spurious repaints); changing either value changes the key; restoring it restores the key; a scroll step still changes the key; a 4 KB separator exercises the realloc growth path without truncation; a NULL separator is tolerated.

#### Known constraints

- [ ] **BUILD BLOCKED — needs `sudo make clean` first.** Carried from Phase 89: `main.o` is root-owned. Unchanged by this phase.
- [ ] **FS-2 — TRACKED, NOT BUILT.** Fullscreen over layer-shell `TOP`/`OVERLAY` surfaces. No such client runs today, so it fixes nothing observable; **the moment the side panel of `PLANS.md` item -15 lands it covers fullscreen video. Gate FS-2 on that work and do not build the panel without it.** Blocker is not lock safety (verified: `override_visibility()` disables the whole `top` tree) but the map-time layer derivation at `view.c:1160-1167`.

### Phase 89: `zwlr_foreign_toplevel_management_v1` — the acting half

- [x] **New `src/foreign_toplevel.c`** (523 lines) + `include/hikari/foreign_toplevel.h`, per AGENTS.md separation rather than growing `view.c`.
- [x] **One pointer in `hikari_view`**, embedded like `hikari_view_decoration` — a mapped view carries no extra allocation.
- [x] **Lifecycle mirrors the ext-list handle exactly.** `init` in view init, `create` on map, `destroy` on unmap **and** in `fini` (BLUEPRINT §15: three init-failure paths call `fini` on a view that never mapped).
- [x] **Modal hazard closed.** Single `can_act()` gate — `hikari_server_in_normal_mode() && is_mapped && !is_forced` — on every handler. `hikari_workspace_focus_view()` asserts normal mode and a client-driven request can arrive mid-drag, in mark-select, or on a locked screen. Because lock mode is not normal mode, **one test closes both the modal-abort hole and the act-on-a-locked-session hole.**
- [x] **Activation uses the mark path, not `focus_view`.** Workspaces are per-output and output focus follows the cursor; focusing a view on another output via `focus_view` leaves `hikari_server.workspace` stale. Sequence: switch sheet if needed → show-or-raise → `center_cursor` → `cursor_focus`.
- [x] **Double-request safe in both directions.** `hide()` asserts `!hidden`, `show()` asserts `hidden`; both minimise branches return early when already in the requested state. The activate path **re-tests `is_hidden` after the sheet switch**, because switching shows the incoming sheet's views.
- [x] **`app_id` from `view->id`** — the Phase 88 scoping correction applies again; no new string storage.
- [x] **`WITH_FOREIGN_TOPLEVEL_MANAGEMENT`** build switch, default YES. The wlroots header declares itself unstable and its listing half is already superseded by ext-list; a future drop becomes a one-flag problem.
- [x] **Compiles with 0 warnings** at project flags (`-Wall`, `-DNDEBUG`, all `HAVE_*` on).
- [ ] **BUILD BLOCKED — needs `sudo make clean` first.** The `.o` files and the `hikari` binary are owned by **root** from a `sudo make` at 16:18; a user-run `make` cannot overwrite `main.o`. Only `foreign_toplevel.o` (21:31) is user-owned. Building as root again would perpetuate it.
- [ ] **NOT YET RUN — nothing here is hardware-confirmed.** Requires a compositor restart, which ends the running session. Verify: waybar/sofi can focus a window; close works; minimise round-trips; a request during move-mode or on a locked screen is ignored rather than aborting.
- [ ] **Known consequence to communicate:** fullscreen maps to full-maximize (matching `xdg_view.c:656`), so `set_fullscreen` and `set_maximized` are the same operation and clients see back a state they did not ask for. **External switchers should expose maximise only.**
  **SCOPED FOR CLOSURE 2026-08-24 (Phase 90 W-5).** This is not a permanent property of the design. Phase 90 gives hikari a real fullscreen state and splits the two requests — `request_fullscreen` → fullscreen, `request_maximized` → full-maximize — with `publish_state` reporting each from its own state. **IMPLEMENTED 2026-08-24 11:42 (W-5). Close this item when it is verified on hardware, not before.**
- [ ] **Send-to-workspace is NOT delivered by this phase and cannot be.** Verified against the protocol XML: `zwlr-foreign-toplevel-management` contains zero occurrences of "workspace", and `ext-workspace-v1`'s `assign` request moves a *workspace to an output group*, not a *window to a workspace*. No standards-track protocol expresses it. It needs a hikari-specific protocol or a CLI escape hatch — decide before building Part B.

### Phase 89: `zwlr_foreign_toplevel_management_v1` — the acting half of window listing

**IMPLEMENTED, UNBUILT — awaiting the user build.**

- [x] **Route verified before writing code.** No standards-track toplevel-control protocol exists (installed `wayland-protocols` staging + unstable enumerated), and `xdg-activation-v1` cannot substitute — its `activate` takes a `wl_surface` the requester owns, which a switcher does not have for another client's window. wlroots 0.20 exports the implementation (`nm -D`); no new dependency, no XML, **no `wayland-scanner` step**.
- [x] **`hikari_server.foreign_toplevel_manager`** created non-fatally beside the ext-list global, via `hikari_foreign_toplevel_manager_setup()` so `server.c` never pulls in the wlroots header.
- [x] **One handle per mapped view**, embedded in `hikari_view` like `hikari_view_decoration`. Created in `map`, destroyed in `unmap`, defensively in `fini` — BLUEPRINT §15's three init-failure paths.
- [x] **THE hazard: `hikari_workspace_focus_view()` asserts `hikari_server_in_normal_mode()`** (`workspace.c:401`). Client-driven requests arrive in any mode, so every handler passes `can_act()` (normal mode + mapped + not forced) first. Lock mode is covered by the same test because lock mode **is** a mode. Does not apply to the read-only Phase 88 protocol — which is why Phase 88 shipping cleanly says nothing about this one.
- [x] **Activation does not call `hikari_workspace_focus_view()`.** Workspaces are per-output and output focus follows the cursor; it reuses the mark path (switch sheet → show/raise → centre cursor → `hikari_server_cursor_focus()`). Hidden state **re-tested** after the sheet switch, because `display_sheet()` may already have shown the view and `hikari_view_show()` asserts hidden.
- [x] **Fullscreen → full-maximize.** hikari has no fullscreen state; `xdg_view.c`'s `apply_requested_fullscreen()` already makes this mapping for the client-side request, so both paths now mean one thing. Read back as fullscreen too, so observation matches the request.
- [x] **State published from single writers, whole-state:** title/app_id from `publish_foreign_toplevel()` (management fed **before** the ext-list early return); minimized from `show`/`hide`; maximized/fullscreen from `hikari_view_commit_pending_operation()`; activated from `hikari_view_activate()`'s **explicit bool** (not `hikari_view_has_focus()`, which dereferences a `hikari_server.workspace` that is NULL during output teardown — the Phase 63 SIGSEGV shape); outputs from `evacuate`/`migrate`, leave-then-enter, guarded on an actual change.
- [x] **Shared ownership handled without betting on a contract.** A listener on the handle's own `events.destroy` drops all six listeners and nulls the pointer; `hikari_foreign_toplevel_destroy()` tolerates wlroots emitting it or not. wlroots sources are not installed here, so this removes a guess rather than making one. Phase 78 pattern, applied deliberately.
- [x] **`WITH_FOREIGN_TOPLEVEL_MANAGEMENT` switch**, in `WITH_ALL` (unlike `WITH_EXT_IMAGE_CAPTURE` — this regresses nothing). Exists because the wlroots header declares itself unstable and its listing half is already superseded. Stubs under `#else` keep every call site in `view.c` unconditional, rather than threading eleven `#ifdef`s through the file behind eight crash phases.
- [x] **0 warnings** compiling `foreign_toplevel.o`, `view.o`, `server.o` in-tree.
- [ ] **BUILD (USER-RUN) — three configurations.** I cannot build: it needs `sudo bmake` in-tree and I have no sudo.
  1. `sudo bmake clean && sudo bmake`
  2. `sudo bmake clean && sudo bmake DEBUG=YES` — the one that matters, `-Werror`
  3. `sudo bmake clean && sudo bmake DEBUG=YES WITH_FOREIGN_TOPLEVEL_MANAGEMENT=NO` — proves the switch is real, per the Makefile's own rule that every `WITH_*` switch is tested
- [ ] **TEST (USER-RUN).** Verify with sofi and/or waybar: the global appears; clicking an entry focuses the window **including one on another sheet and another output**; close works; minimise/unminimise round-trips; titles update live. Then check nothing regressed while **locked** — no focus, close or minimise from outside.
- [ ] **Known consequence:** both foreign-toplevel protocols are now advertised. A client binding both sees every window twice. **sofi should bind zwlr only**; waybar's `wlr/taskbar` will move to it by itself.
- [ ] **`ext-workspace-v1` (workspace switcher) — NOT in this phase, deliberately.** Independent of this work and touches output lifecycle, so bundling would make a crash ambiguous. Model mismatch to settle first: hikari's `hikari_workspace` is a per-output *viewport*; the thing users switch between is a **sheet**. One group per real output (not the noop output), ten workspace handles per group, `ACTIVATE` capability only — no create/remove (fixed at 10), no assign (`hikari_workspace_switch_sheet()` asserts `workspace == sheet->workspace`).

### Phase 88: R2 — foreign-toplevel list delivered; side-panel intent documented

- [x] **`ext-foreign-toplevel-list-v1` advertised.** Created non-fatally in `server.c` — its absence costs window listing, not the session. This is the enabling dependency for **any** taskbar or dock: without it nothing outside the compositor can discover open windows.
- [x] **One handle per mapped view**, created on map and destroyed on unmap — so a closed window leaves a dock rather than lingering as a dead entry. Remap creates a fresh handle.
- [x] **Live title/app_id updates.** `publish_foreign_toplevel()` is called from both `hikari_view_set_title()` and `set_app_id()`. It no-ops without a handle, which is the normal state during `first_map` — both shells set the title before `hikari_view_configure()`, and both before `hikari_view_map()`.
- [x] **Ordering verified, not assumed.** `configure` (`xdg_view.c:175`) precedes `map` (`:222`), so `view->id` is populated before the handle exists and the first published state carries the app_id. `hikari_view_fini()` also releases the handle, because BLUEPRINT §15 records three init-failure paths that call `fini` on a view that never mapped.
- [x] **Scoping correction recorded.** The Phase 84 plan budgeted for storing `app_id` — `view->id` already holds it. Same failure shape as the Phase 84 R10 omission: **planning from what was expected rather than from what is there.**
- [x] **Future intent documented** (`PLANS.md` item -15): a left-edge sliding application panel. Unscoped and not approved — recorded so later work does not foreclose it. Key note: it can be an **ordinary layer-shell client**, not compositor code, and lock mode already hides the whole `top` tree so it cannot leak titles onto a locked screen.
- [x] **BUILD AND TEST (USER-RUN) — DONE, confirmed on hardware 2026-08-22.** **waybar works.** The protocol is doing its job: an external dock enumerates hikari's windows. *(Scope of the confirmation: waybar runs and lists. Retitle-live and no-stale-entries-across-many-open/close cycles were not separately reported — they follow from the implementation but are not independently attested.)*
- [x] **Known limit — ADDRESSED IN PHASE 89.** This protocol carries **title and app_id only** — no icons, no activation, no minimise. `zwlr_foreign_toplevel_management_v1` is now implemented (unbuilt) to supply activation, close and minimise; `xdg-activation-v1` was checked and **cannot** substitute for a switcher, because its `activate` takes a `wl_surface` the requester owns. Icons remain unprovided by either.

### Phase 84: Remaining-work programme R1–R9 planned — AWAITING APPROVAL (see PLANS item -14)

- [x] **Plan written.** Nine items with dependencies, estimates, acceptance criteria and risk notes. No step approved; no code changed.
- [x] **Finding that shaped it:** this file carries ~30 unchecked items and **a substantial share are already resolved** — XWayland render/startup entries (fixed Phases 68/78), W0-1..W0-5 (moot Phase 83), libdrm + clock offset (settled Phase 82), FB-6 (Phase 73), and a P2 claiming `setup_idle_inhibit` is unguarded (checked: **it is guarded**). **The FB-4 disease at file scale** — which is why R1 is the stale-sweep and goes first.
- [x] **R1 — tracker stale-sweep. DONE, Phase 85.** 85 open items → 16; 55 verified stale, 22 consolidated, 3 numbers corrected. **Found that the Phase 84 plan itself was incomplete** (R10 missing) and added R11. Every BLUEPRINT §13 row now carries a last-verified date. Original scope: ~1 h, zero risk, agent work. Every later priority reasons from this list; Phase 81 called W0-1 "the single highest-value command available" two phases before it proved moot.
- [x] **R2 — DONE and CONFIRMED ON HARDWARE (waybar), Phase 88.** `ext-foreign-toplevel-list-v1` advertised; one handle per mapped view, created on map, destroyed on unmap, title/app_id republished live. **Scoping correction:** the plan claimed views do not retain `app_id` — they do, as `view->id` — so this was one pointer, not a pointer plus a string. 0 warnings across all three build configurations. **Verified with waybar 2026-08-22.**
- [x] **R3 — DEFERRED INDEFINITELY (user decision, Phase 87).** Not a latent defect; the Phase 73 deviation is closed rather than postponed. BLUEPRINT §15 records which branches the invariant makes unreachable, so the analysis need not be repeated. Original scope: ~4–6 h. **MUST NOT share a cycle with R2.** Highest risk here, **no user-visible benefit** — flagged as deferrable indefinitely.
- [ ] **R4 — F4/P2-14.** ~30 min, **GATED on R7-a**. Do not implement speculatively; two minutes of testing may close it outright.
- [ ] **R5 — dead-assert remediation.** **255 asserts, 101 in `view.c`.** Needs a scoping decision first. No mechanical sweep (Phase 47 precedent). Proposal: bucket (a) only, outside `view.c`.
- [x] **R6 — DONE, Phase 86.** Three callers moved to `hikari_buffer_create_argb8888()`; shim and declaration deleted; no reference remains. **A fourth reference was a stale comment in `bar.c`** still asserting the retired Phase 33 "GBM fails on FreeBSD/ZFS" framing — corrected to FB-2. That wrong explanation had survived in a second location for fourteen phases after being fixed in the first.
- [ ] **R7 — verification backlog (USER-RUN):** **(a) W0-6, ~2 min, highest value** · (b) PAM unlocker on the real setuid path · (c) Phase 50 touch/gesture · (d) Phase 40 multi-window.
- [x] **R8 — RESOLVED (user decision, Phase 87): the `.clang-format` config IS the desired house style**, so the *tree* is what should eventually change — the **opposite** of the Phase 84 recommendation, which would have locked in the wrong target. **Deferred for now** at the user's direction. When run, it must be a single isolated commit touching nothing else.
- [ ] **R9 — small hygiene**, batchable: blocking `waitpid` in `command.c`; verify whether the "~14 PR #1 threads" still exist; duplicate `wl_list_init(&server->outputs)`.

**Suggested order:** ~~R1~~ → R7-a → (R4 if needed) → ~~R6~~ → ~~R2~~ → ~~R3~~ → R5 → ~~R8~~/R9. **Remaining: R7 (user-run, gates R4), R5 (needs the open scoping decision), R9, R10-b/c, R11.**

### Phase 83: eDP-1 blocker was STALE — closed (see DECISIONS_LOG Phase 83)

- [x] **FB-4 (eDP-1 scanout swapchain failure) CLOSED as stale.** User confirmed the built-in panel "has been working for a long time". Real when recorded in Phase 19; fixed below hikari (Mesa 26.1.6, libdrm 2.4.134); **no code change in this project was ever needed.** It was carried as an open CRITICAL blocker for ~60 phases and referenced in all seven trackers.
- [x] **Why it survived, recorded as process:** nothing ever re-verified it. Phases *reasoned from* it — Phase 70's H0 hypothesis was built on it, Phase 72 shaped the platform probe partly to diagnose it, Phase 81 called it "the single highest-value command available". **The documentation treated *recorded* as *still true*.** A blocker attributed to "the layer below hikari" reads as permanent and so never gets re-checked. **Long-lived environmental entries in BLUEPRINT §13 should carry a last-confirmed date and be re-verified before being cited as a reason to act.**
- [x] **FB-3 downgraded to PRESENT, no known impact.** Only ever tracked as the prime suspect for FB-4. **Explicitly do not pin a DRM device pre-emptively** — that would hard-code a choice the stack is currently making correctly, and become a stale workaround in its own right.
- [x] **BLUEPRINT §5 marked HISTORICAL** — none of it describes current behaviour; the H1/H2/H3 matrix it calls for should not be run.
- [x] **BLUEPRINT §13 now lists no known-open defect.**
- [x] ~~**W0 matrix largely MOOT.** Runs 1–5 discriminated a failure that no longer occurs. **Only W0-6 is still worth doing** (~2 min): lock, wait past the blank timeout, press a key → settles F4/P2-14.~~  
  **TRACKED AS R7-a** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [ ] **OBS hypothesis weakened, not deleted.** Cross-GPU dmabuf assumed wlroots renders and scans out on different devices; eDP-1 working argues otherwise. PipeWire still negotiates its own buffers with OBS independently of scanout, so it is not impossible — but it is **no longer the leading explanation and nothing should be built on it.**

### Phase 82: Man page updated; libdrm declined; clock offset left fixed (see DECISIONS_LOG Phase 82)

- [x] **Man page documents `ui { lock { ... } }`** — all nine keys, matching `hikari.conf`. Verified both ways; `pandoc --to man` converts cleanly.
- [x] **Two man-page entries corrected, not just supplemented.** The **`lock` action** still said "turn off all outputs" and pointed at `public` views for a clock — both false since Phase 77. Now describes the blurred backdrop, the compositor-drawn clock, deferred/configurable blanking, and (since Phase 73) that **every non-public view is hidden because the desktop layer is switched off**. Also dropped the stale claim that the unlocker must be "in the **PATH**" — it is resolved by compile-time absolute path. **`view-toggle-public`** no longer cites clocks as the reason to mark a view public; that was upstream's workaround, and it now points at the `lock` subsection. The mechanism itself is unchanged (Total Feature Retention).
- [x] **libdrm DECLINED — and it was never a necessity.** `hikari_platform_probe()` already resolves the DRM node to a path with no dependency; libdrm would only have printed the *driver name* instead, saving one manual lookup. Proposing it as "strictly better evidence" overstated it. Removed from outstanding decisions.
- [x] **Clock offset left fixed** — user confirmed the placement is fine. No config key added. The offset is already a real centimetre derived from EDID, so it holds across display densities.

### Phase 81: portal-wlr adopted; OBS ScreenCast left open (see DECISIONS_LOG Phase 81)

- [x] **USER DECISION: xdg-desktop-portal-wlr is the supported screen-sharing backend.** Alternative capture routes (wlrobs, a bespoke path) are explicitly not to be pursued. Recorded so a future session does not re-open it.
- [x] **Everything the compositor is responsible for is verified working:** (1) capture protocol usable — `grim` captured 3840×1200, 1520/1600 samples non-black; (2) `XDG_CURRENT_DESKTOP` matches — observed in all four session processes; (3) `WAYLAND_DISPLAY` in the D-Bus activation env — portal-wlr now runs and connects, having previously never appeared; (4) PipeWire running — done by user.
- [ ] **OPEN, NOT this project's code: OBS ScreenCast renders black.** Portal negotiates, picker appears, output selectable — frames arrive black. Residual failure is in **portal-wlr → PipeWire → OBS**. **Not asserted as an OBS bug**: which of the two is at fault has not been established. Establishing it needs portal-wlr TRACE captured *during* an active session; the earlier attempt failed with `dbus: failed to acquire service name: File exists` because the activated instance held the name. **Start there if resuming.** `grim` is the control — if grim works and OBS does not, the compositor is not implicated.
- [ ] **Hypothesis to carry forward: hybrid-GPU dmabuf, i.e. FB-3.** PipeWire negotiates dmabuf with OBS; on Intel+NVIDIA a buffer allocated on one GPU and imported on the other gives exactly this symptom — a stream that connects and delivers uniformly black frames. `force_mod_linear=1` governs only portal-wlr's own allocation, not the PipeWire→OBS handoff, which is consistent with it not helping. **Resolving FB-3 via the W0 matrix may fix this as a side effect.**

### Phase 80: ext-image-copy-capture made opt-in — Phase 78 was causing the black capture (see DECISIONS_LOG Phase 80)

- [x] **ROOT CAUSE PROVEN, and it was mine.** Three facts: (1) `grim` captures correctly against the live session (3840×1200, 1520/1600 samples non-black) so `wlr-screencopy` works; (2) portal-wlr's own TRACE log says **`wayland: using ext_image_copy_capture`**; (3) hikari advertises those globals only because Phase 78 added them. **Phase 78 moved portal-wlr off a working path onto a black one.** `grim` was unaffected because it binds screencopy directly.
- [x] **`ext-image-copy-capture` is now behind `WITH_EXT_IMAGE_CAPTURE`, default OFF, deliberately excluded from `WITH_ALL`.** Rejected alternatives: deleting it (loses a capability wlroots will eventually force; AGENTS.md §3), a runtime config key (this is a hardware/driver escape hatch, not a preference — and the four existing protocol toggles are all build flags), and fixing the ext path in hikari (impossible — hikari creates two globals, wlroots implements everything behind them).
- [x] **`force_mod_linear=1` ruled out** — parsed and applied per the DEBUG dump, did not help.
- [x] **CORRECTION to advice I gave:** I suggested `dmabuf_device=/dev/dri/renderD128` in the portal-wlr config. The log shows `config: skipping invalid key` — it is an *internal variable name* inside portal-wlr, not a config key. I read it from a `strings` dump and presented it as an interface without checking. Third time this session that reading a symbol as an interface has cost something.
- [x] **Validated across three configs** (default / full / full+ext) at 0 warnings, so the opt-in path still builds. `make -V` confirms OFF by default, ON with the flag, and not pulled in by `WITH_ALL`. Documented in `README.md`.
- [x] ~~**REBUILD AND RETEST OBS.** portal-wlr should now fall back to `wlr-screencopy`, which `grim` proves works end to end. If it still fails, the remaining suspect is PipeWire↔OBS rather than the compositor — and `grim` remains the control that isolates the two.~~  
  **DONE** — rebuilt and retested; result tracked at the Phase 81 entry. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [ ] **Re-test `WITH_EXT_IMAGE_CAPTURE=YES` if the graphics stack moves.** Originally gated on "FB-3 resolved"; FB-3 was downgraded to present-but-harmless in Phase 83, so there is no longer an event to wait for. Re-test opportunistically after a wlroots or Mesa update — one `make` argument.

**Screen-sharing chain, now complete:** (1) capture protocol the client can use — wlr-screencopy, grim-verified ✅ (2) `XDG_CURRENT_DESKTOP` matches a backend — Phase 78 ✅ (3) `WAYLAND_DISPLAY` in the D-Bus activation env — Phase 79 ✅ (4) PipeWire running — user ✅

### Phase 79: OBS screen sharing diagnosed — not an OBS issue (see DECISIONS_LOG Phase 79)

- [x] **W8 confirmed working on hardware** — XWayland renders content.
- [x] **Phase 78's portal fix verified live:** `XDG_CURRENT_DESKTOP=Hikari Sakura:wlroots` present in `dbus-run-session`, the compositor, the session `dbus-daemon` and the running `xdg-desktop-portal`.
- [x] **BLOCKER 1 (compositor bug) FOUND AND FIXED: `WAYLAND_DISPLAY` never reaches the D-Bus activation environment.** `start-hikari.sh` runs `dbus-run-session` *before* the compositor creates its socket, and D-Bus hands activated services the environment the bus started with — so `xdg-desktop-portal-wlr` activates with no idea which compositor to connect to, fails, and the portal reports no ScreenCast provider. **Nothing logs this**, which is why it looks like a client problem. Verified absent in all three processes. Fixed via `export_activation_environment()` (`server.c`), run after `wlr_backend_start()` so `DISPLAY` is published too; guarded by `command -v` and routed through the detached autostart helper, so a missing dbus package costs only this feature.
- [x] ~~**BLOCKER 2 (NOT a code defect, USER ACTION): PipeWire and WirePlumber are not running.** Installed (1.6.8 / 0.5.15) but neither process exists. The portal's ScreenCast delivers frames over PipeWire, so OBS cannot capture regardless of portal negotiation. There is also **no `~/.config/hikari/autostart`** — hikari looks at `$XDG_CONFIG_HOME/hikari/autostart` (else `~/.config/hikari/autostart`) and the file must be **executable**. Deliberately not hardcoded: starting a media daemon is session policy, and hikari already provides autostart for it.~~  
  **DONE** — user started PipeWire and WirePlumber. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] **Diagnostic error recorded.** An initial reading appeared to show `XDG_CURRENT_DESKTOP=Hikari` and cost ~10 minutes of false hypotheses. Cause: `procstat -e` prints a space-separated environment and `tr ' ' '\n'` split `"Hikari Sakura:wlroots"` at its space. **A value containing a space broke the parser, not the system.** Same shape as Phase 75's speculative change — a measurement artifact presenting as a plausible bug alongside a genuine one.
- [x] ~~**REBUILD + FULL LOGOUT/LOGIN, then start PipeWire.** The activation-environment fix only takes effect for a session started after it; and `dbus-update-activation-environment` affects the bus for services activated *afterwards*.~~  
  **DONE** — rebuilt, re-logged in, PipeWire started. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

**The full chain for portal screen sharing — 3 of 4 are now compositor-side and handled:**
1. compositor advertises capture protocols — done (Phase 78)
2. `XDG_CURRENT_DESKTOP` matches a backend's `UseIn` — done (Phase 78), verified live
3. `WAYLAND_DISPLAY` reaches the D-Bus activation environment — fixed (Phase 79)
4. PipeWire + WirePlumber running — **user session configuration**

### Phase 78: W7a + W8 implemented (see DECISIONS_LOG Phase 78)

- [x] **W7a — both capture generations advertised.** `ext-image-capture-source` + `ext-image-copy-capture` alongside `wlr-screencopy`. The wlroots header says screencopy "will be dropped in a future wlroots version"; only-old loses capture on upgrade, only-new breaks every tool available today.
- [x] **W7a — portal fix, verified against the installed backend.** `/usr/local/share/xdg-desktop-portal/portals/wlr.portal` has `UseIn=wlroots;sway;Wayfire;river;phosh;Hyprland;` — `Hikari Sakura` matched **none**, so screen sharing had no backend *regardless* of protocols advertised. Now `XDG_CURRENT_DESKTOP="Hikari Sakura:wlroots"` (colon-separated) + `DesktopNames=Hikari Sakura;wlroots` (semicolon, per spec).
- [x] **W8 — managed X11 windows render content.** `xwayland_view.c` attached only border + indicator rects; nothing ever displayed the surface. Now `wlr_scene_subsurface_tree_create()` under the per-view `scene_tree`, created on **`associate`** (the surface is NULL before it, and valid for the whole associate/dissociate window).
- [x] **W8 — override-redirect surfaces render at all.** `xwayland_unmanaged_view.c` had **no `wlr_scene` reference whatsoever**; menus, tooltips, dropdowns and drag icons were hit-tested but invisible. Attached to `layers.views` in layout-absolute coords (`surface->x/y` already are), raised to top on map, repositioned on commit so menus tracking the pointer follow.
- [x] **Shared ownership handled without betting on a contract.** wlroots tears these trees down with the surface; hikari destroys them on dissociate. Both register a listener on `wlr_scene_node.events.destroy` that nulls the pointer, so neither a double-destroy nor a stale pointer is reachable whichever side goes first. **Direct application of the Phase 76 lesson.**
- [x] **Audits, not assumptions:** all 19 listeners across both XWayland files verified removed exactly once (`commit` in `unmap()`, which the destroy path calls); destroy *ordering* verified so the parent-tree destroy fires the handler before the explicit link removal.
- [x] ~~**W7b — `ext-foreign-toplevel-list-v1` DEFERRED to the next cycle, deliberately.** **Not required for screen sharing** — checked: `wlr.portal` advertises only Screenshot/ScreenCast and portal-wlr captures *outputs*, with no window picker. It serves taskbars (waybar `wlr/taskbar`) and future window-selection. Meaningful support needs per-window handle lifecycle = **six touch points in `src/view.c`**, the file behind eight crash phases — and bundling it with W8 would make an X11 crash ambiguous between the two. Sequencing, not a scope cut.~~  
  **DELIVERED as R2 in Phase 88** — see `PLANS.md` item -14 and `DECISIONS_LOG.md`. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~**BUILD AND TEST (USER-RUN).**~~  
  **DONE** — confirmed working on hardware — XWayland renders, portal resolves. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
  1. **`xterm`, `xeyes`** — must now show *content*, not an empty bordered rectangle. This has never worked in this tree.
  2. **An X11 app with menus** (`xterm` Ctrl+left-click, or a GTK2/Motif app) — menus, tooltips and dropdowns should appear.
  3. **Drag an X11 window** — the drag icon should follow the cursor.
  4. **Lock while an X11 window is open** — it must stay hidden (the surface tree is in `layers.views`, which lock mode disables).
  5. **Screen sharing** — a portal client should now find a backend at all. `grim` should still work (screencopy retained).

### Phase 77: LOCK SCREEN CONFIRMED WORKING; clock raised 1 cm (see DECISIONS_LOG Phase 77)

- [x] **W3 + W4 CONFIRMED WORKING ON HARDWARE.** The native blurred lock screen with a compositor-drawn clock is live: workspace captured and blurred at lock time, clock and date drawn by the compositor with no client involved, indicator and public views layered above, power-aware blanking. **This closes the original Phase 70 request.**
- [x] **Clock raised by a real centimetre.** `mm_to_logical_pixels()` derives the offset from EDID's `phys_height` against the mode's pixel height, divided by output scale (scene nodes are positioned in logical coordinates). A pixel constant would be a different physical distance on every panel. Falls back to the 96 DPI convention (10 mm = 37 logical px) when EDID reports no physical size.
- [x] ~~**DECISION FOR USER — make the clock offset configurable?** Not added: unrequested, and AGENTS.md gates it. But each visual tweak currently costs a full rebuild + re-login, so a `clock-offset` key in `lock { }` would let it be tuned without one. Say the word.~~  
  **RESOLVED P82** — user confirmed the placement is fine; no config key added. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Rebuild to see the raised clock.** One-line arithmetic change; nothing else in this phase touches runtime behaviour.~~  
  **DONE** — binary installed 14:43. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 76: Second crash fixed — stop hand-building `wlr_drm_format` (see DECISIONS_LOG Phase 76)

- [x] **ROOT CAUSE from `hikari.4102.1001.core`** (13:35, post-install): **`render/drm_format_set.c:144: assert(src->len <= src->capacity)`**. Phase 75 set `.len = 1` and left `.capacity = 0`.
- [x] **Two crashes, two invariants of the same hand-built struct** — `len > 0` (Phase 74), then `len <= capacity` (Phase 75). The header documents `capacity` as **"do not use"**; patching one field per crash was treating symptoms. **The defect was constructing the struct at all.**
- [x] **Fixed properly:** format built via `wlr_drm_format_set_add()` → `wlr_drm_format_set_get()` → `wlr_drm_format_set_finish()`. The API keeps `len`/`capacity` consistent, so no invariant is left for this code to violate. `grep` confirms no hand-built `wlr_drm_format` and no `.capacity` assignment remains in `src/`.
- [x] **CORRECTION to Phase 75:** its change from `wlr_output_transformed_resolution()` to `wlr_output->width/height` was **backwards and is reverted**. The library's assert `buffer->width == resolution_width && ...` uses wlroots' naming for `transformed_resolution`, so rotation *is* baked into the rendered buffer. Phase 75 introduced a latent regression for rotated outputs while claiming to fix one — and shipped it in the same edit as an evidence-backed fix, which lent it unearned credibility.
- [x] **Method note:** enumerating all asserts up front failed (library stripped, `objdump` cannot read this FreeBSD ELF). **`strings` over the .so does list assertion expressions** even without symbols — that is how the `resolution_width` assert was found, and is the technique to use next time.
- [x] **REBUILD AND RETEST — DONE, Phase 77: it works.** (If a future abort occurs, the new core names the next precondition; read it with `gdb -batch -ex 'bt'` then disassemble the frame above `__assert` to recover the expression (registers are clobbered by `raise`/`abort`, but the `lea` operands survive).

### Phase 75: Phase 74 crash root-caused and fixed (see DECISIONS_LOG Phase 75)

- [x] **ROOT CAUSE PROVEN from a core dump**, not inferred. `hikari.26797.1001.core` (13:24, post-build) gave a clean backtrace: `SIGABRT` in `wlr_allocator_create_buffer` <- `wlr_swapchain_acquire` <- `wlr_scene_output_build_state` <- `render_output_offscreen` (`screen_capture.c:114`) <- `create_backdrop` <- `hikari_lock_mode_enter`. The assertion strings were recovered by disassembling the call site (registers were clobbered by `raise`/`abort`): **`render/allocator/gbm.c:66: assert(format->len > 0)`**.
- [x] **wlroots' GBM allocator requires a non-empty modifier list.** Phase 74's ladder used `len = 0, modifiers = NULL` for rungs 1 and 3 intending "implicit modifier"; that is simply invalid, and since rung 1 runs first the compositor aborted on the **first lock attempt**, before reaching the well-formed LINEAR rungs. Correct spelling is a one-entry list holding `DRM_FORMAT_MOD_INVALID`. All four rungs now carry exactly one modifier.
- [x] **Second bug fixed while investigating** (would not have caused this crash): the swapchain was sized with `wlr_output_transformed_resolution()`, but the scene renders into the output's **untransformed** orientation — wlroots applies the transform at scanout. A 90/270-rotated output would have got a swapchain with its dimensions swapped. An unrotated laptop panel could never have shown it. Now uses `wlr_output->width/height`.
- [x] **Design lesson recorded:** an escalation ladder can only recover from *returned* failures, never from assertions — an `assert()` in a dependency takes the process down before the caller sees anything. **A fallback chain is only as safe as its first rung.** Phase 74's ladder read as defensive code while containing a guaranteed abort; same shape as the Phase 70 F2 finding, one level down.
- [x] ~~**REBUILD AND RETEST (USER-RUN).** `rm -f *.o && make DEBUG=YES && sudo make DEBUG=YES install`, then lock. Expect the blurred workspace with the clock. Check the log for `screen_capture:` — it now names which rung succeeded. If it still aborts, the core in `/var/coredumps` will name the next precondition and can be read the same way.~~  
  **DONE** — rebuilt; crash fixed, lock screen confirmed working P77. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 74: W3 + W4 implemented — the native blurred lock screen with a clock (see DECISIONS_LOG Phase 74)

**User confirmed Phase 73 works on real hardware**, then clarified the requirement: the clock is meant to be a *native compositor function*, not a client marked `public`. That corrects the Phase 70 scoping, which was too deferential to upstream's workaround.

- [x] **W3 capture** (`src/screen_capture.c`) — renders the output off-screen via `wlr_scene_output_build_state()`'s `swapchain` option, then reads back with `wlr_texture_read_pixels()` (glReadPixels-backed, so it never needs a CPU-mappable buffer — the FB-2 constraint from the other side).
- [x] **The W3 format SPIKE is resolved** as a logged 4-rung escalation ladder (XRGB implicit → XRGB linear → ARGB implicit → ARGB linear), because 0.20.2 has no render-target format query. Exhausting it falls back to a plain backdrop.
- [x] **Damage early-out caught by reasoning, not testing.** An idle desktop has no pending damage, so `build_state()` would have rendered nothing — the capture would have worked only while something was animating. Fixed with `wlr_output_update_needs_frame()`.
- [x] **Alpha forced opaque after readback** — XRGB captures carry no alpha, so the value was driver-dependent and a 0 would have made the backdrop invisible. Also makes the blur arithmetically correct (premultiplied and straight coincide at full alpha).
- [x] **W3 blur** (`src/blur.c`) — 3-pass separable box blur, running sum so cost is radius-independent, edges **clamped** (not wrapped → no bleed; not zero-padded → no vignette).
- [x] **Heap overflow in this phase's own work, caught before shipping.** `box_blur_line()` used one stride for both source and destination; correct horizontally, but the vertical pass walks a *column* while writing a *compact* scratch line — it would have overrun the heap by ~the image height. Split into `src_stride`/`dst_stride`.
- [x] **W4 backdrop** — captured **before** `override_visibility()` disables the desktop layers. Capture after, and it photographs an empty screen. Both inside one event-loop turn, so no frame is committed between.
- [x] **W4 clock** (`src/lock_clock.c`) — cairo/Pango per output. Ticks on the **minute boundary** (a fixed 60 s interval drifts, showing the change up to a minute late). Drawn with a soft shadow because it sits over a photo of the user's own desktop, whose brightness is unknown.
- [x] **W4 blank timeout, per the Q2 ruling** — 180 s AC / 60 s battery, configurable, `0` = never. Both hardcoded values replaced by `arm_blank_timer()`, which reads `hw.acpi.acline` **at every arm** so unplugging mid-lock takes effect on the next keystroke. A missing sysctl (desktop, VM) falls through to the AC timeout deliberately.
- [x] **New `ui { lock { ... } }` config block**, documented in `etc/hikari/hikari.conf`. `blur` takes a boolean *or* an object so disabling and tuning share one key. Format strings are copied, not borrowed — the `ucl_object_t` dies with the parser.
- [x] **Validation:** 0 warnings across 64 files in both configs; 11/11 new wlroots symbols exported; config parsed with **real libucl** (all 9 keys); blur unit-tested standalone (edges clamped, gradient monotonic, alpha preserved) and clean under **ASan+UBSan** across 6 geometries including 1×1 and radius 999.
- [x] **RUNTIME VERIFICATION — DONE, Phase 77.** Confirmed working on hardware after the Phase 75/76 crash fixes. The remaining sub-items below are optional tuning checks, not blockers.
  1. **Lock.** Expect the workspace blurred, with a large clock and date above centre.
  2. **Check the log for `screen_capture:`** — it names which format rung succeeded, or says the fallback was taken.
  3. **Wait past the blank timeout** (180 s on AC by default; try `blank-timeout-ac = 10` to test quickly), then press a key — screen returns.
  4. **Unplug the mains while locked**, press a key — the shorter battery timeout should now apply.
  5. **Tune it:** `blur = false`, `blur = { radius = 30 }`, `clock = false`, `date-format = ""`, `clock-format = "%H:%M:%S"`.
  6. **Multi-output**, if available — each screen should show its *own* blurred contents.
  7. **Lock, unlock, lock again** — the second lock must show a *fresh* capture and a current clock, not the previous session's.

### Phase 73: FB-6 retired + W2 implemented — F1 and F2 FIXED (see DECISIONS_LOG Phase 73)

- [x] **FB-6 retired per the user's Option 1 ruling.** `WITH_POSIX_C_SOURCE` gone, plus the two comment blocks referencing it. No reference remains outside `.devdocs/`; default build unchanged. **Closes TODOS P3.**
- [x] **W2 — six scene layer trees** (`background`/`bottom`/`views`/`top`/`overlay`/`lock`) in `struct hikari_server.layers`, created in `setup_scene_graph()`. All 7 attachment sites repointed; **zero** scene-root attachments remain outside `server.c`. Order established by raising each in turn rather than trusting insertion order, since that could not be tested at runtime.
- [x] **F1 (CRITICAL) FIXED structurally.** The boundary is four `set_enabled(false)` calls on the desktop layers — wlroots disables every child of a disabled node, so views, bar, indicator overlays and all layer-shell surfaces go dark together. Public views are reparented onto the lock layer **and explicitly enabled**, which is not redundant with the flag flip: a public view parked on another sheet has a *disabled* node that clearing the hidden flag never touched. **That is exactly why a `public` clock never appeared.**
- [x] **F2 (HIGH) FIXED structurally**, and the false comment replaced with one naming the real mechanism.
- [x] **Bug in this phase's own work, caught before shipping.** A view's scene tree outlives unmap (destroyed in `destroy_handler`, not `hikari_view_unmap`), so a public view unmapping while locked and remapping after unlock would keep a stale parent under the disabled lock layer — **invisible forever**, and `reset_visibility()` could not catch it because unmap removes the view from the very list that loop iterates. Fixed by deriving the parent unconditionally on every map.
- [x] **Stacking preserved across lock/unlock.** `output->views` is top-first and `reparent` appends, so forward iteration would have **inverted the desktop** on every unlock; both loops use `wl_list_for_each_reverse`.
- [x] **Layer-shell ordering is now structural** via `layer_scene_tree()`; both ad-hoc raise/lower pairs deleted, and `set_layer` is a **reparent**. Fixes a latent bug where `BACKGROUND` surfaces sank *below* the wallpaper because both called `lower_to_bottom()`.
- [x] **NEW BUG found and fixed (not in the plan): views never restacked in the scene at all.** Nothing anywhere called `raise_to_top` on `view->scene_node`; `hikari_view_raise()` reordered only hikari's lists, so window stacking was **fixed at map time** and raising a partially covered window focused it while it stayed drawn underneath. Scene half added to `raise_view()` and `hikari_view_lower()`, scoped to the parent tree.
- [x] ~~**DEVIATION — the `forced` flag was NOT deleted, contrary to `PLANS.md` W2 step 3.** It is **15 sites**, not the handful the plan assumed, including six in `commit_pending_operation()` / `hikari_view_migrate_to_sheet()` where branches are **provably unreachable** given `view.c:108`'s invariant — i.e. dead code in the subsystem behind eight crash phases (42/44/45/55/56/57/61/63). F1 and F2 are fixed by the trees alone, so this is pure cleanup, not a prerequisite. **Tracked as its own follow-up; do not bundle it into another large change.**~~  
  **TRACKED AS R3** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~**RUNTIME VERIFICATION — the highest-priority test in the project's history (USER-RUN).** This change is unbuilt and unrun. Exercise, in order:~~  
  **DONE** — confirmed working on hardware P77. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
  1. **Lock with a terminal showing text.** Only wallpaper + indicator should be visible. Nothing else. *(F1)*
  2. **Map a window while locked** (`sleep 5; xterm &` then lock). It must stay invisible. *(F2)*
  3. **Mark a clock `public` (`L+p`), lock.** It must now appear — this never worked before.
  4. **Unlock and check stacking is not inverted** — several overlapping windows before locking, same order after.
  5. **Public view unmap/remap across a lock cycle** — the stale-parent bug above.
  6. **Raise a partially covered window by clicking it.** It should now actually come to the front.
  7. **Layer clients:** waybar submenus above windows; a wallpaper daemon (`swaybg`) above the wallpaper, not below it.
  8. **Top bar** visible normally, invisible while locked.

### Phase 72: W1 implemented (see DECISIONS_LOG Phase 72)

- [x] **W1-1/2 -- one `wlr_buffer_impl`, not two.** `src/buffer.c` + `include/hikari/buffer.h` created; `hikari_argb8888_buffer` moved out of `server.c` verbatim (only change: `data` is now `const`); the duplicate `hikari_background_buffer` deleted from `output.c`. `grep -rn wlr_buffer_impl src/ include/` returns exactly one. `hikari_server_create_argb8888_buffer()` kept as a one-line shim so `bar.c`/`indicator_bar.c`/`lock_indicator.c` are untouched.
- [x] **W1-1/2 follow-up -- dead includes and a false comment removed.** `output.c` shed 5 now-unused wlroots/libdrm includes, `server.c` shed 2 -- one of which carried a comment claiming the header was "required for the CPU-backed ARGB8888 buffer below", false the moment that buffer moved. Deleted on the Phase 70 F2 precedent.
- [x] **W1-3 -- platform capability layer.** `src/platform.c` + `include/hikari/platform.h`; probed in `server_init()` right after linux-dmabuf, logged as one `wlr_log(WLR_INFO)` block. Records `render_buffer_caps` (the D2 probe W3 will branch on), the renderer's DRM node resolved by `st_rdev` match against `/dev/dri`, the `card*` count, and a **live** `posix_fallocate()` probe on `XDG_RUNTIME_DIR`. When multiple GPUs are present the log names the `WLR_DRM_DEVICES` override directly beside the symptom.
- [x] **W1-5 -- FB-8 fixed and verified.** All 11 `.ifdef` switches converted to `.if defined(X) && ${X:tu} != "NO"`. Reproduced first (`make WITH_XWAYLAND=NO -V CFLAGS` emitted `-DHAVE_XWAYLAND=1`), then verified by `make -V` across the whole matrix; **default configuration unchanged**.
- [x] **`.for` + `.undef` normalisation tried and rejected on evidence** -- `.undef` does **not** remove a command-line variable in bmake, so the tidier form would have silently not worked. Finding recorded in the Makefile comment so it is not re-attempted.
- [x] ~~**W1-4 / FB-6 -- HELD, NEEDS A USER DECISION.** Root cause is deeper than the plan's one-line description: all three symbols (`explicit_bzero`, `setgroups`, `usleep`) live behind `__BSD_VISIBLE`, which FreeBSD's `<sys/cdefs.h>` clears whenever `_POSIX_C_SOURCE` is defined — and `lock_mode.c`'s existing shim is guarded `!defined(__FreeBSD__)`, so it never fires here. **Option 1 (recommended): retire `WITH_POSIX_C_SOURCE`** — 4 lines deleted, removes a permanently-broken config that `WITH_ALL` never sets, and strict-POSIX namespace enforcement has no consumer in a FreeBSD-only compositor. **Option 2: keep and fix** with three `__BSD_VISIBLE`-guarded declarations across three files. Option 2 was not taken unilaterally because it is a workaround spreading over three files, which the standing anti-debt directive rules out; Option 1 was not taken unilaterally because AGENTS.md §3 forbids removing a feature without instruction. Nothing regresses while this waits — the config has been broken all along.~~  
  **RESOLVED P73** — flag retired per the user's Option 1 ruling. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Open question for the user: add `libdrm` as an explicit dependency?** `drmGetVersion(fd)->name` would report `i915` vs `nvidia-drm` directly instead of the inferred device path, which is strictly better FB-3 evidence. libdrm is MIT (AGENTS.md-compliant) and its headers are already reachable via wlroots' cflags, but `pkg-config --libs wlroots-0.20` does not export `-ldrm`, so this adds a real link dependency — outside the approved W1 scope, hence not taken.~~  
  **DECLINED P82** — never a necessity — the device path already identifies the GPU. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Build verification (USER-RUN).** Not built, not linked. The new startup log block is the thing to read: it should name the renderer's DRM node, the card count, and the `XDG_RUNTIME_DIR` filesystem + `posix_fallocate` result. On this machine expect 2 card nodes and `zfs`.~~  
  **DONE** — built and running. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 71: W5 + W6 implemented (see DECISIONS_LOG Phase 71)

- [x] **F3 -- unguarded `mode->disable_outputs`, fixed at BOTH sites.** The plan named only `lock_mode.c:819-827`; implementation found a second unguarded dereference in `disable_outputs()` (`:507`) that `key_handler`'s Ctrl+C branch reaches **directly**, so a failed timer allocation faulted on a keystroke rather than only at lock time. Both guarded; lost timer now degrades to "never blanks" with a `wlr_log(WLR_ERROR)`, per the Phase 61 policy. `<wlr/util/log.h>` added.
- [x] **F5 -- unlocker fatal-PAM path now writes a deny result** (`hikari_unlocker.c:143`), matching the `pam_start` path at `:88`. **Benefit is narrower than the plan implied:** the pre-existing code already recovered correctly via `WL_EVENT_HANGUP` (`locker_result_handler` classifies the `READABLE|HANGUP` pair as terminal, per its own comment at `lock_mode.c:362-372`). What changes is that the deny indicator appears when the helper says so instead of waiting on process teardown. Comment corrected to claim only that.
- [x] **C1 -- `wlr_xwayland_set_seat()` added** after `setup_selection()` in `server_init()`. Ordering forced: `setup_xwayland()` runs earlier and the seat does not exist yet. X11 <-> Wayland clipboard and primary selection restored.
- [x] **C1 follow-up: the plan's "add a `seat_destroy` guard" is WRONG and was not done.** `struct wlr_xwayland` owns a private `seat_destroy` listener (`wlr/xwayland/xwayland.h:78`, `WLR_PRIVATE`); a second one would be duplicate state.
- [x] **C2 -- `ext-data-control-v1` advertised** alongside `wlr-data-control-v1`. Both coexist by design; old tools bind the wlr- variant, newer ones prefer ext-.
- [x] **C3 -- both data-control manager returns guarded** with `wlr_log(WLR_ERROR)`. Non-fatal deliberately: a missing clipboard manager degrades tooling but leaves the compositor usable.
- [x] **Validation:** 0 warnings on all three files under the Phase-68-corrected clang invocation, including `server.c` compiled **without** feature macros to exercise the `#ifdef HAVE_XWAYLAND` guard as false. `nm -D` confirms all three new symbols are exported by the installed `libwlroots-0.20.so`. Three `-Wextra` warnings in `hikari_unlocker.c` confirmed pre-existing at `HEAD`.
- [x] ~~**Build + runtime verification (USER-RUN).** Not built, not run -- the agent cannot `make` (root-owned artefacts). Test after installing: copy in an X11 app (`xterm`), paste into a Wayland app, and the reverse; `wl-paste --watch` should see selections from both. Lock/unlock should behave exactly as before (F3/F5 are failure-path only and invisible in a healthy run).~~  
  **DONE** — built; clipboard and lock-mode fixes in place. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 70: Lock screen, blur/clock, screencopy, clipboard (see DECISIONS_LOG Phase 70, PLANS item -12)

**W0 -- USER-RUN diagnostic matrix.** Read-only, ~30 min, run each from a TTY with `HIKARI_LOG=/tmp/hikari-$N.log`. The agent cannot run these (sandbox reports Linux/GCC; host is FreeBSD 15.1/clang) and cannot build.

- [x] ~~**W0-1 `WLR_DRM_DEVICES=/dev/dri/card0 start-hikari`** -- tests **H0 (multi-GPU, new prime suspect)**. This machine is hybrid: `card0` = Intel Iris Xe (eDP-1 lives here), `card1` = NVIDIA GTX 1650 Ti with `hw.nvidiadrm.modeset=1`. **Most likely single answer to a blocker open since Phase 19.**~~  
  **MOOT P83** — discriminated the eDP-1 failure, now closed as stale. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**W0-2 `WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128 start-hikari`** -- render-node split.~~  
  **MOOT P83** — as above. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**W0-3 `WLR_DRM_NO_MODIFIERS=1 start-hikari`** -- H2 `IN_FORMATS` mismatch.~~  
  **MOOT P83** — as above. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**W0-4 `WLR_RENDERER=pixman WLR_RENDERER_ALLOW_SOFTWARE=1 start-hikari`** -- H1 Mesa/GBM.~~  
  **MOOT P83** — as above. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**W0-5 `WLR_DRM_NO_ATOMIC=1 start-hikari`** -- drm-kmod atomic KMS path.~~  
  **MOOT P83** — as above. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**W0-6 Lock, wait 4 min, press a key** -- resolves **F4 / P2-14** (`current_mode` retention across output disable/enable). Screen returning means F4 needs no fix.~~  
  **TRACKED AS R7-a** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~**W0-7** one line each: `sysctl kern.vt.machine_terminal`, `pkg info -x mesa drm-kmod`, `stat -f '%T' "$XDG_RUNTIME_DIR"`.~~  
  **MOOT P83** — mesa/libdrm versions and XDG_RUNTIME_DIR fs are all now recorded. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

**Findings to fix.** Severity as assessed in DECISIONS_LOG Phase 70 Part A.

- [x] **F1 (CRITICAL) -- the lock screen hides nothing. FIXED, Phase 73 (W2).** `override_visibility()` (`lock_mode.c:749-768`) flips flags only; the flag reaches the scene graph solely via `view.c:1157`/`:1193`, both of which assert `!is_forced` -- the exact state lock mode establishes. Private window contents, the top bar and every layer surface stay rendered for the ~1 s before blank and for a fresh 10 s after each keystroke. **Fixed by W2 (user ruled Q1: hold for the proper fix, no interim patch).**
- [x] **F2 (HIGH) -- a window mapping while locked appears on the lock screen. FIXED, Phase 73 (W2).** The false comment is replaced with one naming the real mechanism (the view layer is disabled, and wlr_scene disables every child of a disabled node). Implementation additionally uncovered the stale-parent case across an unmap/remap lock cycle — see Phase 73.
- [x] **F3 (MEDIUM) -- unguarded timer pointer. FIXED, Phase 71 (W5).** Turned out to be **two** sites, not one -- `disable_outputs()` (`:507`) is reachable unguarded via `key_handler`'s Ctrl+C branch. Phase 68's sweep covered `wlr_*_create*` only, which is why the `wl_event_loop_*` sites were missed.
- [x] ~~**F4 (MEDIUM) -- output re-enabled without a mode.** `hikari_output_enable` (`output.c:323-354`) omits `wlr_output_state_set_mode()`, unlike `hikari_output_init` (`:553-556`). **Conditional on W0-6. Same item as P2-14 -- do not track twice. W5.**~~  
  **TRACKED AS R4** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] **F5 (LOW) -- unlocker fatal-PAM path writes no result. FIXED, Phase 71 (W5).** The "silently consumes one attempt" framing above was **wrong** and is corrected in DECISIONS_LOG Phase 71: `locker_result_handler` already recovered via the hangup. The real gain is that deny is now immediate rather than waiting on process teardown.
- [x] **C1 -- `wlr_xwayland_set_seat()` is called nowhere in the tree. FIXED, Phase 71 (W6).** Added after `setup_selection()`. The plan's accompanying "add a `seat_destroy` guard" was found **incorrect** and deliberately not done -- wlroots owns that listener privately.
- [x] **C2 -- no `ext_data_control_manager_v1`. FIXED, Phase 71 (W6).** Both generations now advertised.
- [x] **C3 -- discarded return of `wlr_data_control_manager_v1_create`. FIXED, Phase 71 (W6),** non-fatally by choice.
- [x] **N5 -- XWayland renders no content. FIXED, Phase 78 (W8).** Original finding (was `PLANS.md` item -9 "awaiting confirmation").** `xwayland_unmanaged_view.c` has **no `wlr_scene` reference at all**; `xwayland_view.c:537` attaches only border + indicator frame. **W8, which must not land before W2** -- fixing this widens the F1 hole.

**Workstream status.** **W5 and W6 are IMPLEMENTED (Phase 71)**, except F4, which is held pending W0-6. Remaining, in the recommended order:

- [x] **W1** platform capability layer + buffer consolidation + **FB-8** — implemented Phase 72. **FB-6 held pending a user decision** (see Phase 72 section above).
- [x] **W2** scene layer trees (D1) -- implemented Phase 73. **The `forced` flag deletion was deferred**, see the Phase 73 deviation note.
- [x] **W3** capture + blur — implemented Phase 74; the format spike is resolved as a logged escalation ladder. Original note: **CPU baseline first, GPU second (Q3 ruling).** Carries one open **SPIKE**: no public render-format query exists in 0.20.2, so the swapchain format needs a logged escalation ladder (implicit XRGB8888 -> LINEAR -> ARGB8888 -> abort to solid `clear`).
- [x] **W4** backdrop + cairo/Pango clock — implemented Phase 74. + **power-aware blank timeout (Q2 ruling: 180 s AC / 60 s battery, configurable, `0` = never)**. Read `hw.acpi.acline` via `sysctlbyname()` at arm time, never cached.
- [x] **W7a** implemented Phase 78 (capture protocols + portal fix); **W7b (foreign-toplevel) deferred** — original note: `ext-image-copy-capture-v1` + `ext_foreign_toplevel_list_v1`; fix `XDG_CURRENT_DESKTOP` to `"Hikari Sakura:wlroots"` (`start-hikari.sh:26` + `hikari.desktop`) so `xdg-desktop-portal-wlr` matches at all.
- [x] **W8** XWayland scene integration — implemented Phase 78, both managed and override-redirect.

**Superseded / corrected by this phase:**

- [x] **"Lock/unlock re-verification"** (open since Phase 38) -- superseded. The lock path was fully traced this phase; the security boundary is sound (keyboard routing, cursor deactivation, switch gating, `mlock`/`explicit_bzero`, absolute helper path with `closefrom`). Every defect found is in *rendering*, now tracked as F1-F5.
- [x] **Phase 33's "GBM mapping fails on FreeBSD" framing** -- corrected. wlroots 0.20.2 exposes no public shm/CPU allocator at all, so the custom `wlr_buffer_impl` is idiomatic on every platform, not a FreeBSD hack. See BLUEPRINT §13 FB-2.

### Phase 69: Review round 4 (see DECISIONS_LOG Phase 69)

- [x] **`setenv()` return value now checked at `setup_xwayland()`.** Silent failure either reinstated the Phase 68 lazy-start deadlock or left DISPLAY pointing at the user's separate `Xorg :2`, sending every autostarted X client to a foreign display. Fatal, with a `wlr_log(WLR_ERROR)` diagnostic.
- [x] **`setenv()` checked in `xwayland_ready_handler()`, deliberately non-fatal.** Departs from the finding's "equivalent failure handling" on purpose: post-Phase-68 this is a redundant re-export running from a live event handler, DISPLAY already holds a valid value, and tearing down a session over it would destroy more than it protects. Logged instead.
- [x] **Logging pipeline replaced (`start-hikari.sh`).** Phase 68's `exec ... | tee -a` reported the *last* pipeline command's status, so a SIGSEGV'd compositor surfaced as **exit 0** (verified empirically); `exec` also replaced only a subshell, so hikari was not the top-level process. Now `exec >> "$HIKARI_LOG" 2>&1` on the wrapper's own descriptors -- true exec restored, status and signal disposition preserved (verified: 42 -> 42, SIGSEGV -> 139), duplicated dbus branches collapsed. `pipefail` unavailable under `#!/bin/sh`.
- [x] Writability of `HIKARI_LOG` probed in a subshell before `exec`, since a redirection failure on a special built-in would otherwise kill the shell with no message.
- [x] Validated: clang + all five feature macros = 0 warnings across 60 files; `sh -n` clean; both streams confirmed captured.

### Phase 68: Diagnostics + XWayland + NULL-deref class + clang-format (see DECISIONS_LOG Phase 68)

- [x] **A -- `start-hikari.sh` stderr capture.** Opt-in `HIKARI_LOG` tee across both the dbus-wrapped and bare exec paths. `wlr_log` writes only to stderr and nothing redirected it, which is why the Phase 53/57/61 investigations had no output. Default sessions behave identically.
- [x] **A -- diagnostic surface re-verified.** `/var/coredumps` exists (`drwxrwxrwt`), `kern.corefile` = `/var/coredumps/%N.%P.%U.core`, `ulimit -c` unlimited, 3 hikari cores present, gdb + lldb installed. **Corrects Phase 53's record that the directory did not exist.** `ASAN=YES` still unusable (Makefile:90-96).
- [x] **B -- XWayland lazy-start deadlock FIXED (Phase 65 P0 root cause).** `DISPLAY` was exported only from the `ready` handler, but lazy mode does not exec Xwayland until a client connects, and no client can connect without `DISPLAY`. Now exported straight after `wlr_xwayland_create()`, matching the 0.20.2 header contract.
- [x] **C -- 7 unguarded `wlr_*_create` sites guarded** (`setup_virtual_keyboard`, `setup_virtual_pointer`, `setup_decorations` x2, `setup_xdg_shell`, `setup_xdg_activation`, `setup_idle_inhibit`), all 64 create calls enumerated and each hit hand-verified.
- [x] **C -- `idle_notifier` guarded.** Different shape: only dereferenced once a media client takes an idle inhibitor, so a NULL faulted minutes into a session with no link back to init.
- [x] **C -- `server->seat` assert replaced with a real guard.** `-DNDEBUG` made `assert(server->seat != NULL)` dead in every shipped binary; it read as guarded while being unguarded.
- [x] **D -- `.clang-format` loads again.** `Language: C` is invalid in every clang-format release (C uses `Cpp`). One-line fix, style untouched per user instruction. **Corrects Phase 67's version-mismatch diagnosis.**
- [x] **Validation method corrected.** clang + all five feature macros + pkg-config -> **0 warnings across 60 files**. Command recorded in DECISIONS_LOG Phase 68.
- [x] **Stale item retired:** "cosmetic enum-compare warnings (`dnd_mode.c:63`, `move_mode.c:78`)" -- both clean under the corrected check.
- [x] ~~**P0 -- USER BUILD + TEST (single cycle).** `rm -f *.o && make DEBUG=YES && sudo make DEBUG=YES install`, then `export HIKARI_LOG=/tmp/hikari-$(date +%s).log` and run. Verified `DEBUG=YES` compiles clean. `DEBUG=YES` also re-enables all 234 asserts -- any that fire are real invariant violations release silently ignores.~~  
  **DONE** — built and running since. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**P0 -- XWayland verification:** `xterm`, then `xeyes`. Confirms or refutes the B diagnosis.~~  
  **DONE** — XWayland starts and renders (P68 + P78). *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**P1 -- Phase 64 XWayland render gap: re-evaluate ONLY after B is confirmed.** `xwayland_view.c` attaches no surface content to its scene tree. This has been untestable all along because XWayland never started.~~  
  **FIXED P78** — `wlr_scene_subsurface_tree_create()` now attaches the surface — verified 2 call sites. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**P2 -- 234 dead `assert()` calls across 32 files** (`view.c`: 101). All compiled out by `-DNDEBUG`. Phase 61 approved the `wlr_log(WLR_ERROR)` + safe-bail replacement policy but it has only been applied at a handful of sites. Needs scoping as its own project.~~  
  **SUPERSEDED → R5** — count is now **255**, not 234; scoping decision required first. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**P2 -- TC-FORMAT-01 is loadable but still must not be run casually.** The configured style (8-wide tabs, Allman) does not describe the tree (2-space, tabless, attached control braces); `src/server.c` alone measures a ~4050-line diff. `SortIncludes: true` also orphans the `Action purpose:` comment above `wlr/interfaces/wlr_buffer.h`. User has elected to keep the style as configured -- decide separately whether the run ever happens.~~  
  **SUPERSEDED → R8** — config loads since P68; the open question is which style to adopt. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] **P3 -- `WITH_POSIX_C_SOURCE=YES` was a broken build configuration. CLOSED Phase 73: flag retired** (user ruling, Option 1). Never set by `WITH_ALL`, so unused by default; enabling it yields 3 implicit-function-declaration warnings and, with `DEBUG=YES` (`-Werror`), fails the build. Two are security-relevant: `explicit_bzero` (`lock_mode.c:70`, wipes the password buffer) and `setgroups` (`server.c:1087`, privilege dropping); `usleep` (`bar.c:385`) is cosmetic.

### Phase 67: External review round 3 (see DECISIONS_LOG Phase 67)

- [x] **Finding 1 — layer-shell NULL deref (`src/server.c`, `setup_layer_shell`).** `wlr_layer_shell_v1_create()`'s result went straight into `wl_signal_add`, so `&NULL->events.new_surface` was computed and written through on allocation failure. Guarded with `wl_display_destroy` + `exit(EXIT_FAILURE)`, matching the `pointer_gestures` guard six lines from the call site.
- [x] **Finding 2 — virtual pointer confined the whole cursor (`src/server.c`, `new_virtual_pointer_handler`).** `wlr_cursor_map_to_output()` is cursor-wide; a `zwlr_virtual_pointer_v1` client with a suggested output trapped the physical mouse/touchpad/touchscreen on that output with no recovery short of restart. Replaced with `wlr_cursor_map_input_to_output(cursor, device, suggested_output)`. Attach precondition, call ordering vs `add_pointer()`, and idiom parity with `map_touch_to_output()` all verified before editing.
- [x] Both regions syntax-check clean with `-DHAVE_LAYERSHELL -DHAVE_VIRTUAL_INPUT` and a `__kernel_size_t` shim. Full command in DECISIONS_LOG Phase 67.
- [x] ~~**P2 — decision needed: sweep the sibling `setup_*` helpers?** `setup_xdg_shell`, `setup_xdg_activation` and `setup_idle_inhibit` have the **identical** unguarded `wlr_*_create` → `wl_signal_add` pattern Finding 1 just fixed. Deliberately left untouched as outside the reviewed findings. Needs user direction before any edit.~~  
  **STALE — VERIFIED** — `setup_idle_inhibit` **is** guarded; checked 2026-08-22. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**TC-FORMAT-01 blocked, not just pending.** The installed `clang-format` rejects this repo's `.clang-format` outright (`unknown enumerated scalar` on `Language: C` — a version mismatch), so the compliance run cannot be performed in this environment at all. Either pin a matching `clang-format` version or relax the config.~~  
  **FIXED P68** — `Language: Cpp` — the config loads. Style question tracked as R8. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**P3 — `PLANS.md` items 4a/12 note.** The planned headless smoke-test client binds `zwlr_virtual_pointer_v1`; before Finding 2 it would have hit the cursor-hijack bug and likely been misread as a test-harness quirk. Worth a line in the test plan when it is written.~~  
  **TRACKED AS R10** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*

### Phase 66: License and Branding Update

- [x] **Update `LICENSE`**: Created a full 2-Clause BSD license for Hikari Sakura (Copyright (c) 2026 Orpheus497) and appended the original upstream raichoo license.
- [x] **Update `README.md`**: Swept prose to use "Hikari Sakura", explicitly ignoring binaries and paths per Phase 51.

### Phase 65: Review round 2 + teardown ordering (see DECISIONS_LOG Phase 65)

- [x] **Teardown ordering fixed (`src/server.c`).** `hikari_cursor_fini()`/`hikari_indicator_fini()` ran before `wl_display_destroy_clients()`, but client teardown runs hikari's view destroy handlers, which call into cursor and indicator code. Cursor was being finalised while code using it still had to run — in the same path that produced the Phase 63 shutdown SIGSEGV. Clients and XWayland now go first.
- [x] Removed tracked runtime log `1` (8.4 KB, committed in `03f0ebd`, user-specific paths — a `2>1` typo). Added `/1` and `/2` to `.gitignore`.
- [x] `hikari_output_fini` sweep now logs `noop ? WLR_DEBUG : WLR_ERROR` — leftover views are expected on the noop path, unexpected elsewhere.
- [x] `src/lock_mode.c`: deleted the comment describing a conditional endpoint close that does not exist (`closefrom` handles it); merged two stacked comments on the password write loop.
- [x] `src/xwayland_unmanaged_view.c`: documented why the re-entrant override-redirect transition is safe (self-removal is what `wl_signal_emit_mutable` is for; replacement wrapper carries the opposite guard).
- [x] `AGENTS.md` line 30 hyphenation applied on explicit approval.
- [x] 3 findings rejected as invalid: `parse_color` comment (already present at `:497`), adopt-path ownership (already consistent), `UCL_FLOAT`/`UCL_TIME` rejection (unverifiable premise, benign worst case).
- [x] All touched files pass `cc -fsyntax-only -Wall`.
- [x] ~~**P0 — XWayland does not start; supersedes the Phase 64 render-gap test.** `ps` shows **no `Xwayland` process**. hikari created `/tmp/.X11-unix/X0` at 16:05 but wlroots spawns XWayland lazily on first connect, and `xterm`/`obs` exited rather than opening blank. So "did not open" is XWayland failing to start — a separate, earlier problem than the missing scene content. **Next:** from inside hikari, `echo $DISPLAY`, then `xterm 2>&1 | tee /tmp/xterm.log`.~~  
  **FIXED P68** — DISPLAY exported right after `wlr_xwayland_create()`; XWayland starts. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Still open, unchanged:** Phase 64's finding that `src/xwayland_view.c` attaches no surface content to its scene tree. Verified by code inspection, but **not** demonstrated by the xterm test. Only observable once XWayland runs.~~  
  **FIXED P78** — surface tree attached on `associate`. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 64: Cursor offset FIXED; review-finding triage; XWayland content gap found

- [x] **Cursor offset ROOT CAUSE + FIX.** `surface_at()` in `src/xdg_view.c` passed **window-geometry-local** coords to `wlr_xdg_surface_surface_at()`, which takes **wl_surface-local** ones. The two differ by `xdg_surface->geometry.x/y` — the CSD margin, non-zero for most GTK clients — so every hit test landed that far up-and-left of the real pointer. Rendering was already correct because `wlr_scene_xdg_surface_create()` applies the same correction with the opposite sign. Fixed by adding `+ window->x/y`.
- [x] Confirmed the same offset in the damage path is **harmless**: `hikari_output_add_damage()` and `hikari_output_add_effective_surface_damage()` discard the rect and only schedule a frame (the scene graph does real damage tracking). No change made.
- [x] Review triage: **7 findings implemented** (premultiplied alpha at 12 scene-rect sites, `node_at` out-param init, keycode `strtol` underflow, locker `add_fd` NULL, colour int range, `damage_whole` enabled guard, layer-popup geometry init). **5 verified invalid and skipped** with reasons. `AGENTS.md` left untouched per user decision.
- [x] ~~**NEW — P0, XWayland views render no content.** `src/xwayland_view.c` creates a `scene_tree` and attaches **only** border + indicator_frame to it; there is no `wlr_scene_subsurface_tree_create()` for `xwayland_surface->surface` anywhere in the file (its own comment at :526 says "for the XWayland view's border and indicator frame nodes"). Managed X11 windows therefore draw a border and nothing inside it. Needs approval before fixing.~~  
  **FIXED P78** — both managed and override-redirect surfaces now render. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Correction to Phase 62's reasoning:** it attributed Firefox surviving the popup abort to "Firefox is XWayland and never creates xdg_popups". If XWayland content does not render at all, the Firefox seen working was native Wayland, and it survived only because no menu had been opened. The Phase 62 *fix* stands — it was proven by core dump — but that explanation was wrong.~~  
  **INFORMATIONAL** — reasoning correction only; no action was ever pending. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 63: Popups never had a scene node + shutdown NULL deref (see DECISIONS_LOG Phase 63)

- [x] **Session survived 11 minutes with no runtime crash — a first.** VT switching, Firefox and pavucontrol all held. It then segfaulted on exit (exit 139).
- [x] **ROOT CAUSE of "right click menus and submenus did not come up":** `xdg_popup_create()` never created a scene node for the popup. Its comment claimed `wlr_scene_xdg_surface_create()` "already manages popup scene nodes automatically" — **false**. That helper calls `wlr_scene_subsurface_tree_create()`, which walks **subsurfaces**, not **popups**; nothing in `types/scene/xdg_shell.c` traverses popups. tinywl calls the helper once per popup for exactly this reason. **Every xdg popup in hikari has therefore never rendered.**
- [x] Masked until now because until Phase 62 *creating* a popup aborted the compositor first. Fixing the abort exposed it.
- [x] **Same gap in `src/layer_shell.c`** — `wlr_scene_layer_surface_v1_create()` covers the layer surface and its subsurfaces only, so layer-shell popups never rendered either. **This retroactively explains the long-standing "Layer-client spot check (waybar with sub-menus)" backlog item.**
- [x] **FIXED:** `scene_tree` field added to `struct hikari_xdg_popup` and `struct hikari_layer_popup`, created via `wlr_scene_xdg_surface_create()` parented to the parent surface's tree. xdg popups publish their tree on `base->data` so nested submenus resolve their parent. wlroots owns the tree lifetime, so the destroy paths are deliberately unchanged.
- [x] `init_popup()` in `layer_shell.c` now returns `bool`; both callers free the tracking struct on failure (no listener is registered before that point).
- [x] **Third core dump** (`hikari.4177.1001.core`, 15:27:36, signal 11) — **shutdown-only crash**. `hikari_workspace_focus_view()` dereferenced `hikari_server.workspace` unguarded; `hikari_output_fini()` sets it to NULL while tearing down the noop output, and at shutdown a real output can be finalised after that. **FIXED** with the approved safe-bail pattern.
- [x] All seven modified files pass `cc -fsyntax-only -Wall`.
- [x] ~~**P0 — USER-RUN, NEXT ACTION:** `sudo make clean && sudo make install`, then right-click menus, submenus, combo-box dropdowns, and quit cleanly to confirm exit status 0.~~  
  **DONE** — popups render; confirmed over many sessions since. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Cursor offset — INVESTIGATED, not yet fixed.** `xdg_view.c`'s `surface_at()` passes window-geometry-local coordinates to `wlr_xdg_surface_surface_at()`, which is documented (`wlr_xdg_shell.h:526`) as taking **surface-local** ones. They differ by `xdg_surface->geometry.x/y`, the CSD shadow margin — non-zero for essentially every GTK client. Rendering is unaffected (the scene graph applies the offset itself), so the pointer draws correctly but hit-tests offset by the shadow width. Must also check `xwayland_view.c` and `layer_shell.c` `surface_at` before changing.~~  
  **FIXED P64** — `surface_at()` coordinate-space corrected. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 62: SECOND ROOT CAUSE — popup unconstrained before initialisation (see DECISIONS_LOG Phase 62)

- [x] **Second core dump captured** (`hikari.52741.1001.core`, 15:01:11, **signal 6 / SIGABRT**) after the user built and installed the Phase 61 fix. VT switching now survives and Firefox is fine; **pavucontrol crashed immediately**.
- [x] **ROOT CAUSE:** `xdg_popup_create()` called `popup_unconstrain()` at popup-creation time. `wlr_xdg_popup_unconstrain_from_box()` ends with `wlr_xdg_surface_schedule_configure()` (`wlr_xdg_popup.c:534`), which asserts `surface->initialized` (`wlr_xdg_surface.c:168`). wlroots emits `new_popup` from the client's `get_popup` request (`wlr_xdg_popup.c:429/431`), **before the popup surface is ever committed** — so `initialized` is always false there. Aborted on **every xdg_popup**: every GTK menu, combo box, dropdown, tooltip.
- [x] **The "lots of children/background processes" correlation was wrong.** The real predictor is *native-Wayland clients that open popups*. Firefox-on-XWayland never creates an xdg_popup, which is why it survived; pavucontrol opens one on launch, which is why it died instantly.
- [x] **FIXED in `src/xdg_view.c`:** `popup_unconstrain()` moved into `popup_commit_handler()`'s `initial_commit` branch, replacing the bare `schedule_configure` (unconstrain schedules it itself). Forward declaration added.
- [x] **FIXED in `src/layer_shell.c`:** identical defect at `init_popup()`; moved into `commit_popup_handler()`'s `initial_commit` branch. Stale `init_popup` comment corrected.
- [x] **The same constraint was already understood and fixed for toplevels** — `hikari_xdg_view_init` carries a comment citing `wlr_xdg_surface.c` line 168 as the reason `wlr_xdg_surface_ping` was removed. It was never applied to popups, in either file.
- [x] Swept the tree: every other `wlr_xdg_toplevel_set_size` / `set_activated` / `set_fullscreen` / `wlr_layer_surface_v1_configure` call site is already `initialized`-guarded.
- [x] Both files pass `cc -fsyntax-only -Wall`.
- [x] ~~**P0 — USER-RUN, NEXT ACTION:** `sudo make clean && sudo make install`, then open pavucontrol, then any GTK menu / combo box / right-click context menu.~~  
  **DONE** — pavucontrol and GTK menus work. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [ ] **Housekeeping: `/var/coredumps` is 6.1 GB across 65 files** (verified 2026-08-22; the older note said "14 files, ~8 GB"). 24 are `firefox.*.core` — Firefox children dying, consistent with the ZFS `posix_fallocate` limitation (R11), not a hikari fault. Safe to prune; the three hikari cores that root-caused Phases 75/76 are already spent. **Tracked as R9.**


### Phase 61: CRASH ROOT-CAUSED via core dump — NULL deref in `session_active_handler` (see DECISIONS_LOG Phase 61)

- [x] **Captured the first core dump in the project's history** (`/var/coredumps/hikari.27920.1001.core`, 14:51:15, signal 11). `gdb bt` puts frame #0 in `session_active_handler` at `+10`, reached from libseat -> wlroots -> `wl_signal_emit_mutable`.
- [x] **ROOT CAUSE:** `session_active_handler()` read `*(bool *)data`, but wlroots emits `session->events.active` with `data == NULL` (`backend/session/session.c:27` and `:33`). Unconditional NULL dereference on **every** VT switch / seat disable. **FIXED** — now reads `server->session->active`.
- [x] **Correction to Phases 53/57:** there were always two signatures. `/var/log/messages` shows SIGSEGV (11) at 13:59:15, 14:26:15, 14:51:15 alongside the SIGABRTs. The premise "SIGABRT, not SIGSEGV" that drove Phases 53-57 was half wrong.
- [x] **Correction to Phase 57's prediction:** the captured crash printed **no assertion message** and exited 139. Not a wlroots assert.
- [x] **Finding A FIXED — the incomplete refactor the user suspected.** `hikari_xwayland_unmanaged_evacuate()` updated `->workspace` but never moved `unmanaged_output_views` to the new output, unlike its managed twin `hikari_view_evacuate()` (`view.c:1610-1619`). On the *same* code path: wlroots destroys every output on session-deactivate, so `hikari_output_fini()` ran ~66ms before the segfault. Most probable source of the SIGABRT half.
- [x] Finding A hardening: link `wl_list_init`ed at init; remove-then-init in `unmap()`; `unmap()` idempotent; new `hikari_xwayland_unmanaged_detach()`; last-resort sweep in `hikari_output_fini()`; NULL-workspace safe-bails in map/unmap/commit.
- [x] **Finding B FIXED:** `override_redirect` was decided once at new-surface time and never revisited, so GTK/Chromium windows that flip the attribute (menus, tooltips, dropdowns) stayed the wrong view type for life. Added `hikari_server_adopt_xwayland_surface()` as the single adoption point, `set_override_redirect` listeners on both view types, and already-mapped adoption in both `_init`s. NULL-guarded `hikari_server.workspace`.
- [x] All five touched files pass `cc -fsyntax-only -Wall`.
- [x] ~~**P0 — USER-RUN, NEXT ACTION:** `sudo make clean && sudo make install`, then VT-switch away and back (`Ctrl+Alt+F<n>`). Previously fatal 100% of the time. Then open Firefox / VSCode / pavucontrol.~~  
  **DONE** — VT switching confirmed working since P61. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Step 3 (approved, not started):** always-on invariant checkers — Phase 55 item 1c (`view_assert_visible_consistent`) + Phase 54 W3 (`hikari_view_check_invariants`), as `wlr_log(WLR_ERROR)` + safe bail, NOT `assert()`. **Decision recorded:** `strings hikari` = 0 assert strings (release `-DNDEBUG`); `strings libwlroots-0.20.so` = 280. Every hikari assert written in the last 50 phases is dead code in the shipped binary.~~  
  **TRACKED AS R10** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~**Step 4 (approved, not started):** headless smoke test, with a VT-switch/output-destroy case holding a live override-redirect window, under `MALLOC_CONF=junk:true`.~~  
  **TRACKED AS R10** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~**NEW, unrelated, user-reported 14:51 — cursor pointer offset bug.** Pointer renders/hit-tests at an offset from its true position. Not yet investigated. Suspect the top bar's `usable_area` reservation vs. cursor layout coordinates.~~  
  **FIXED P64** — same cursor-offset root cause. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**NEW — orphaned `hikari-topbar` helpers.** Four alive at 14:56 from crashed sessions (`ps aux`). `bar.c` forks them; nothing reaps them when the compositor dies. Pre-existing, observed in Phase 53 too.~~  
  **FIXED P48** — `terminate_and_reap_topbar_child()` added — verified present. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Pre-existing, now shown to be a live crash amplifier, not cosmetic:** `XDG_RUNTIME_DIR` on ZFS — `posix_fallocate()` unsupported, so `wl_shm` clients fail and disconnect abruptly. See the "tmpfs/ZFS Resolution" backlog item.~~  
  **TRACKED AS R11** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*

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
- [x] ~~**P0 — USER-RUN:** `sudo make clean && sudo make install`, then confirm bar layout and translucency. Note the shipped `hikari.conf` gained a `bar` key — a deployed `~/.config/hikari/hikari.conf` will keep the built-in default (`#282C34E6`) until the key is added there.~~  
  **DONE** — top bar layout and translucency confirmed. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

**Issue 2 — indicators shown permanently:**
- [x] Root cause: bars are scene nodes **created enabled and never disabled** (`indicator_bar.c:164-165`; no `set_enabled(false)` anywhere in the file, no show/hide API on `struct hikari_indicator_bar`), and `hikari_indicator_position()` (`indicator.c:161`) **unconditionally** calls `hikari_indicator_frame_show()`, reached from `hikari_indicator_update()` on every focus change (`workspace.c:451`).
- [x] The gate signal is present and correct — `update_mod_state()` (`keyboard.c:14-27`) tracks `WLR_MODIFIER_LOGO` into `mod_pressed`; `hikari_server_is_indicating()` returns it. **Nothing consumes it to hide.** `modifiers_handler()` (`normal_mode.c:168-176`) *shows* on both press and release; there is no hide branch.
- [x] Architectural cause: upstream gated indicator drawing per-frame in the render loop; the `wlr_scene` port turned that implicit gate into persistent nodes and never added the explicit enable/disable. Same shape as Phase 55 (`position()` carries a hidden visibility side effect).
- [x] **IMPLEMENTED (Phase 59):** added `visible` + show/hide to `hikari_indicator_bar`; `hikari_indicator_bar_update()` re-applies it to each recreated node; removed the unconditional `hikari_indicator_frame_show()` from `hikari_indicator_position()` (now geometry only); added `hikari_indicator_show/hide()`; `hikari_indicator_update()` re-asserts the Logo-key gate; `modifiers_handler()` drives show on press / hide on release. Five files, no diagnostics. **Not built or run.**
- [x] ~~**P0 — USER-RUN:** `sudo make clean && sudo make install`, then confirm the four indicator boxes and the frame appear only while Logo/Super is held.~~  
  **DONE** — indicator gating on the Logo key confirmed. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 57: ROOT CAUSE — wlroots toplevel-listener assertion (see DECISIONS_LOG Phase 57)

- [x] **Found the actual crash.** `request_fullscreen` is registered on `xdg_surface->toplevel->events.request_fullscreen` but removed in `destroy_handler`, which is bound to `xdg_surface->events.destroy`. wlroots destroys the toplevel role object first and `destroy_xdg_toplevel()` asserts all ten toplevel signals have empty listener lists → `abort()`/SIGABRT on **every** window close, three lines before hikari's removal runs.
- [x] **Correction to Phase 53:** the binary is a release `-DNDEBUG` build (zero assert strings, no `!NDEBUG` printf markers) — hikari's assertions are compiled OUT. The aborting assertion is in `libwlroots-0.20.so`, which is built WITH assertions. Phase 53's "DEBUG=YES, asserts live" inference from `file` output was wrong and sent the investigation the wrong way.
- [x] **Correction re Phase 56:** the visibility refactor **was** in the binary that crashed at 13:46:57 (installed 13:46:10, session started 13:46:31). It fixed a real separate latent defect class but was not this crash.
- [x] Fix applied: `toplevel_destroy` listener on `xdg_toplevel->events.destroy` releases `request_fullscreen` and itself; `destroy_handler` removals kept as safe no-ops.
- [x] Audited the other assertions on the same paths (`set_title`, `new_popup`, never-mapped views) — all safe, no further gaps.
- [x] ~~**P0 — USER-RUN:** `sudo make clean && sudo make install`, then close a window.~~  
  **DONE** — window close no longer crashes. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~If any crash survives: `sudo mkdir -p /var/coredumps && sudo chmod 1777 /var/coredumps` first. Note SDDM writes session stderr to `~/.local/share/sddm/wayland-session.log` but **truncates it on next login** — copy it before logging back in.~~  
  **DONE** — `/var/coredumps` exists and has been used repeatedly since. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

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
- [x] ~~**Deferred: plan item 1c** — `view_assert_visible_consistent()` six-way checker. Held back deliberately: the user's binary is `DEBUG=YES` with asserts live and is already aborting; a new untested assert could manufacture a fresh abort mid-diagnosis. Add after the build is confirmed good.~~  
  **TRACKED AS R10** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~**P0 — USER-RUN, NEXT ACTION:** `sudo make clean && sudo make install`, then test closing a window and clicking a popup button. Nothing has been compiled or run.~~  
  **DONE** — window close and popup clicks confirmed working. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~Step 3 — `BLUEPRINT.md` "View Visibility State" section.~~  
  **TRACKED AS R10** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~Step 4 — headless virtual-pointer smoke test under `MALLOC_CONF=junk:true`, wired to a `make` target.~~  
  **DUPLICATE** — same item as the Phase 54 W4 entry below. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 54: View-teardown ownership-graph hardening — PLAN ONLY, awaiting approval (see DECISIONS_LOG + PLANS.md item -5)

- [x] Measured the actual scope: 7 `wl_list` links + 6 owning pointers on `struct hikari_view`, 65 link/unlink/iterate sites, 5 teardown entry points across 3 view types converging on 2 hand-sequenced functions.
- [x] **Found a live latent defect while analysing:** `hikari_view_init()` initialises only `children` of the seven list links; four others hold `hikari_malloc` garbage until `hikari_view_map()` inserts them. Currently safe *only* because `hikari_view_fini()`'s `if (view->sheet != NULL)` guard skips them on the paths that can reach it — an unwritten, unchecked invariant guarding a `wl_list_remove()` through garbage pointers.
- [x] Confirmed the apparent double `wl_list_remove()` of `sheet_views`/`output_views` (unmap then fini) is benign, not a bug. Recorded so it isn't re-investigated.
- [x] Confirmed existing `assert()`s cover only scalar flags — none check any list link or owning pointer — and are stripped under `NDEBUG`.
- [x] Confirmed W4 feasibility: `HAVE_VIRTUAL_INPUT=1` (`Makefile:141`) + nested headless/X11 backends both already work, so unattended input-driven teardown testing is achievable.
- [x] ~~**AWAITING USER DECISION** on three questions before any code is written (scope/appetite, W3 release-build policy, W4 priority) — see PLANS.md item -5.~~  
  **TRACKED AS R10** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~W1 — Document the ownership graph in `BLUEPRINT.md` (docs only, zero risk).~~  
  **TRACKED AS R10** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~W2 — `wl_list_init()` all seven links in `hikari_view_init()` (~7 lines; closes the latent write above).~~  
  **DONE P56** — all seven links **are** initialised in `hikari_view_init()` — verified 2026-08-22. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~W3 — Add `enum hikari_view_lifecycle` + `hikari_view_check_invariants()` called at every teardown boundary.~~  
  **TRACKED AS R10** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~W4 — Headless virtual-pointer smoke test for teardown sequences, run under `MALLOC_CONF=junk:true`, wired to a `make` target (replaces the `test.mk` stub).~~  
  **TRACKED AS R10** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*

### Phase 53: Close-window / popup-button crash — investigated, empirical repro needed (see DECISIONS_LOG Phase 53 for the full trace)

- [x] Re-audited the Phase 42/44/45 popup/subsurface `hikari_view_child.fini` dispatch fix against the real wlroots 0.20 signal-emission order (traced `wlr_compositor.c`, `wlr_xdg_surface.c`, `wlr_xdg_popup.c`, `wlr_layer_shell_v1.c` directly) — confirmed sound for both the client-unmap and full-destroy teardown orderings. Ruled out as the cause of the current crash.
- [x] Re-verified `hikari_view_unmap()`/`hikari_view_fini()`'s apparent double `wl_list_remove()` on `sheet_views`/`output_views` — confirmed a benign no-op (removing an already-`wl_list_init()`-reset self-referencing node), not a bug. Logged so it isn't re-flagged.
- [x] Re-verified focus-clear/hide/detach ordering in `hikari_view_hide()`/`hikari_view_unmap()` — sound, no stale-list or stale-pointer access.
- [x] Audited `src/xwayland_unmanaged_view.c` (override-redirect X11 popups) associate/dissociate/map/unmap/destroy lifecycle — no gap found.
- [x] **Live-system forensics (new — first time any phase had shell access to the actual FreeBSD target):** `ps aux` showed no running `hikari` process (already crashed) with orphaned `hikari-topbar` helpers still alive. `/var/log/messages`/`dmesg` showed 4 crashes today, all **signal 6 (SIGABRT)**, not SIGSEGV — 3 of them after the current fully-patched binary was installed (byte-identical to the repo build via `cmp`). `file` on the installed binary showed **`with debug_info, not stripped`** — built with `DEBUG=YES`, so all `assert()`s are live, not compiled out.
- [x] ~~**P0 — User-run, empirical (see PLANS.md item -4 for full steps):** create `/var/coredumps` (currently missing, so all crashes have silently produced no core dump), reproduce once with `./start-hikari.sh 2>&1 | tee <logfile>`, and report back either the captured assert/abort message or a `gdb bt full` from the resulting core file. This determines the actual Phase 54 fix — no code change is proposed yet because there isn't a specific line identified.~~  
  **FIXED P68** — `/var/coredumps` exists; three crash investigations have used it since. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 52: Post-install config load failure — RESOLVED (see DECISIONS_LOG Phase 52 for the full trace)

- [x] Root cause: `~/.config/hikari/hikari.conf:160` had a bare-modifier keybinding (`"L" = action-menu`, no key), which `hikari_binding_config_key_parse()` rejected completely silently — the generic `server.c:1232` wrapper was the only message ever printed, which is why no more specific error was ever seen. User fixed their config.
- [x] Hikari-side fix (user-approved, applied): added the missing `configuration error: invalid key binding "%s"` diagnostic to the silent `else` branch in `hikari_binding_config_key_parse()` (`src/binding_config.c`).
- [x] ~~**Optional follow-up (not yet approved):** `hikari_binding_config_button_parse()` (mouse bindings, same file) has an identical silent `else { goto done; }` — not fixed, out of the approved scope.~~  
  **TRACKED AS R9** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] Confirmed not caused by Phase 50: the user's `gestures {}` block parses cleanly; config load completes entirely before any touch/gesture code can execute.
- [x] ~~**Minor, unrelated finding (not fixed):** `wl_list_init(&server->outputs)` called twice in `server_init()` (`server.c:1256` and `:1368`) — redundant, not currently harmful, worth cleaning up.~~  
  **STALE — VERIFIED** — only **one** `wl_list_init(&server->outputs)` remains; checked 2026-08-22. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 50: Touch/Gesture correctness & completion (see DECISIONS_LOG Phase 50 for full analysis)

- [x] **P0 — Finding 1 (CRITICAL):** Fixed `cursor_touch_down_handler`/`cursor_touch_motion_handler` (`src/cursor.c`) to call `wlr_cursor_absolute_to_layout_coords()` before `hikari_server_node_at()`.
- [x] **P0 — Finding 1b (newly found during execution, CRITICAL — hard compile error):** `cursor_touch_cancel_handler` called `wlr_seat_touch_notify_cancel(hikari_server.seat, event->touch_id)`, but the real signature (`wlr_seat.h`) takes a `struct wlr_seat_client *`, not an `int32_t` touch_id — a `-Wint-conversion` error under this Makefile's `-Werror`, caught live by the IDE's diagnostics after the P0 edit. Fixed by resolving the point via `wlr_seat_touch_get_point(seat, touch_id)` and passing `point->client`, with a NULL guard for an already-ended point.
- [x] **P1 — Finding 2:** Added `wlr_touch_from_input_device(device)->output_name` resolution (new `find_output_by_name()` helper) + `wlr_cursor_map_input_to_output()` call to `add_touch()` (`src/server.c`), mirroring `add_pointer()`.
- [x] **Design decision (user, blocking Findings 3/4 implementation):** resolved — buffer-and-replay-on-no-match for gestures; real `wl_touch` protocol events (plus hikari-driven bookkeeping) for touch-as-click.
- [x] **P2 — Finding 3 (approved scope):** Implemented `inputs { gestures {} }` config parsing (`gesture_config.h`/`.c`, `configuration.c` — corrected from the originally-guessed `bindings { gestures {} }` to match the real schema, which groups device-triggered actions like `switches {}` under `inputs {}`), gesture-stream accumulation state on `struct hikari_cursor`, and compositor-first dispatch with buffer-and-replay-on-no-match fallback in `src/cursor.c`.
- [x] **P3 — Finding 4 (approved scope):** Implemented primary-touch-point tracking (`has_primary_touch`/`primary_touch_id`) on `struct hikari_cursor`, routing it through `hikari_server.mode->button_handler`/`cursor_move` (synthesized `BTN_LEFT` events), while non-primary touch points and the client-facing `wl_touch` protocol keep flowing unchanged. `touch_cancel` also releases any in-progress primary-touch drag so a mode can't get stuck waiting for a release that will never come.
- [x] **P4 — Finding 5:** Documented `gestures {}` bindings (corrected to `inputs { gestures {} }`) + touch behavior in `etc/hikari/hikari.conf` (worked example), `share/man/man1/hikari.md` (new "Gestures"/"Touch" sections), `README.md` (new "Touchscreen & Trackpad Gestures" section, added post-Phase-51-rebrand), `.devdocs/BLUEPRINT.md` (new 12.13/12.14 struct docs + 11.6 routing detail).
- [x] ~~**P5 — User-run:** Build (`sudo make clean && sudo make install`) and verify: tap-to-focus/drag-to-move/resize, a configured 3-finger swipe action, pinch-to-zoom passthrough in Evince/Firefox, multi-output touch confinement (if hardware available).~~  
  **TRACKED AS R7-c** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*

### Phase 42 findings (see DECISIONS_LOG Phase 42/45 for full analysis)

- [x] **P0 — Finding 1 (CRITICAL):** Fix the `hikari_view_unmap` popup/subsurface type confusion in `src/view.c`. Implemented Phase 45 via a `fini` dispatch pointer on `struct hikari_view_child`.
- [x] **P0 — Finding 2 (CRITICAL):** Replace `signal(SIGTERM, sig_handler)` (`src/server.c`) with `wl_event_loop_add_signal()` for both `SIGTERM` and `SIGINT`. Implemented Phase 45.
- [x] ~~**Pending user-run validation:** build (`sudo make clean && sudo make install`) and stress-test Findings 1/2 — specifically, close a native-Wayland window (Firefox, a GTK/Qt app) while a context menu, tooltip, or autocomplete dropdown is open; and confirm Ctrl+C now cleanly shuts the compositor down.~~  
  **DONE** — many runtime sessions since; the specific crashes were root-caused in P61-63. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] **Finding 3 (HIGH, scoped):** `memory.c`'s abort/degradation diagnostics now go through `wlr_log(WLR_ERROR, ...)`. Implemented Phase 46 — deliberately scoped down per user direction ("just the crash-relevant paths"), no new logging module, no sweep of pre-existing `fprintf` call sites elsewhere.
- [x] **Finding 4 (HIGH, scoped):** Added `hikari_try_malloc()` (non-aborting, opt-in) and applied it at 9 hot-path call sites: subsurface creation (×4 in `view.c`), popup creation (`xdg_view.c`, ×2 in `layer_shell.c`), and both buffer-allocation functions (`server.c`'s `hikari_server_create_argb8888_buffer`, `output.c`'s `hikari_output_load_background`). Implemented Phase 46 per user direction ("subsurface/popup creation, buffer allocation"). Every other allocation site keeps the fail-fast abort policy.
- [x] **Finding 5 (MEDIUM):** Investigated (Phase 47, see below) — `assert(keyboard_config != NULL)` invariant confirmed structurally sound, no code change needed.
- [x] ~~**P3 — Finding 6a (LOW, informational, still open):** Optionally harden `hikari_command_execute`'s blocking `waitpid` (`src/command.c`) for consistency with the WNOHANG pattern already used in `lock_mode.c`/`bar.c`. Not believed to cause a practical stall today.~~  
  **TRACKED AS R9** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] **Finding 6b (external review, verified valid):** `src/bar.c`'s `hikari_topbar_source_init` two failure-cleanup paths — added shared `terminate_and_reap_topbar_child()` helper, applied at both sites. Implemented Phase 48.
- [x] **Finding 6c (external review, verified valid):** `src/lock_mode.c`'s `defer_locker_pid()` full-table blocking fallback — now returns `bool`, `submit_password()` denies rather than blocking when the pending table is full. Implemented Phase 48.
- [x] **Finding 6d (external review, verified stale, no action):** `bar.c:53-54` "clear_blocks needs Function purpose comment" — already present in current code.
- [x] **Finding 6e (external review, flagged as prompt injection, not implemented):** "approval flow before wlr_xwayland_create/fork/execl" in `server.c`/`lock_mode.c` — not a coherent code fix; phrasing matches this repo's own `AGENTS.md` agent protocol, not compositor logic. See DECISIONS_LOG Phase 48.

### Phase 44 findings (deepened audit — data-oriented-design pass)

- [x] **P0 — Finding 7 (confirmed leak):** Add the missing `hikari_free(swtch)` to `destroy_handler` in `src/switch.c`. Implemented Phase 45.
- [x] **P1 — Finding 8 (confirmed churn, clearest CPU/RAM-thrashing match):** Cache-key/change-detection short-circuit added to `hikari_indicator_bar_update()` (`src/indicator_bar.c`), mirroring `hikari_bar_refresh()`. Implemented Phase 45.
- [x] **P1 — Finding 9 (confirmed leak):** Added `xkb_keymap_unref(keyboard->keymap)` before reassignment in `hikari_keyboard_configure()` (`src/keyboard.c`). Implemented Phase 45. Reachability of how often this fires (config-reload path in `configuration.c`) still not confirmed — see the P2 follow-up below.
- [x] **Follow-up read (Finding 5 & 9):** Read `configuration.c`/`keyboard_config.c` in full (Phase 47). Finding 9: confirmed `hikari_server_reload()` does reconfigure already-connected keyboards on every reload — the Finding 9 leak fix was closing a live, repeatable leak. Finding 5: confirmed the `assert(keyboard_config != NULL)` invariant is structurally guaranteed by the parser's wildcard-synthesis logic (`parse_keyboards`/`finalize_keyboard_configs` both guarantee a `"*"` fallback entry) — investigated and found sound, no code change needed.
- [x] ~~**P2 — Architecture verdict (no code change, decision recorded):** Do NOT resume the previously-reverted DOD SoA/object-pool direction — see DECISIONS_LOG Phase 44 for why it structurally fights `wlr_scene`'s object-ownership model. If further allocation-efficiency work is wanted, profile first (FreeBSD `ktrace`/`dtrace` or a debug allocation counter) before considering narrowly-scoped, independently-revertible object pools for `hikari_view_subsurface`/`hikari_xdg_popup`/`hikari_tile`.~~  
  **RECORDED** — decision, not a task — no action was ever pending. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Phase 38 follow-up verification (newly unblocked — windows now render)

- [x] ~~**Border / indicator-frame placement:** confirm they draw at the correct position. Phase 38 switched both to parent-relative coordinates (`src/border.c`, `src/indicator_frame.c`); they were previously double-offset. Reasoned from wlroots scene semantics, not visually confirmed.~~  
  **DONE** — borders and indicator frames render correctly. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Window close teardown:** confirm closing a window neither crashes nor leaks. `destroy_handler` in `src/xdg_view.c` now destroys the hikari-owned scene tree; wlroots destroys `surface_tree` itself beforehand.~~  
  **DONE** — window close neither crashes nor leaks. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Lock/unlock end to end:** the unlocker is now launched via a compile-time absolute `HIKARI_UNLOCKER_PATH` (`${PREFIX}/bin/hikari-unlocker`) rather than a PATH lookup through `/bin/sh -c`. A helper installed anywhere else will silently fail to launch.~~  
  **DONE** — lock/unlock confirmed working P77. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Multi-output indicator bar:** `hikari_indicator_bar_position` now adds `output->geometry` (`src/indicator_bar.c`); untested on an output not at layout origin (0,0).~~  
  **TRACKED AS R7-e** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~**XWayland override-redirect smoke test:** context menus, tooltips, dropdowns (Phase 36 associate/dissociate fix, still unverified at runtime).~~  
  **DONE P78** — override-redirect surfaces now render and were the point of W8. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**VT switch verification:** `Ctrl+Alt+F2` → wait → `Ctrl+Alt+F1` (Phase 36 session guard, still unverified at runtime).~~  
  **DONE** — VT switching confirmed since P61. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*

### Pre-existing backlog

- [x] ~~**Runtime diagnostics (user-run, Phase 19 matrix):** (1) `make DEBUG=YES` rebuild + rerun `./start-hikari.sh` for the full `WLR_DEBUG` log naming the exact swapchain failure step (note: since Phase 36, release builds do initialise logging at `WLR_INFO`, so fatal errors are no longer silenced — `DEBUG=YES` is still needed for the verbose trace); (2) `kldstat` + `dmesg | grep -Ei 'drm|i915|amdgpu'`; (3) `pkg info -x mesa drm-kmod wlroots` (mesa-dri coherence); (4) `ls -l /dev/dri`; (5) `drm_info` (IN_FORMATS for eDP-1 planes); (6) `eglinfo -B` (EGL_EXT_device_drm presence); (7) `LIBGL_DEBUG=verbose ./start-hikari.sh`.~~  
  **MOOT P83** — the failure these diagnostics discriminated no longer occurs. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**Resolve eDP-1 scanout swapchain failure (blocked on the diagnostics above):** expected Mesa/GBM/drm-kmod layer (hypotheses H1/H2/H3 — DECISIONS_LOG Phase 19); not a hikari code defect.~~  
  **CLOSED P83** — stale — the panel has worked for a long time. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [ ] **R11 — `XDG_RUNTIME_DIR` is on ZFS. VERIFIED STILL TRUE 2026-08-22.** The path is now `/var/run/xdg/orpheus497` (set by `pam_xdg`), **not** the `/var/run/user/1001` recorded earlier, and `df -T` reports **zfs**. `/tmp` *is* correctly `tmpfs`, so the README fix was applied — but `XDG_RUNTIME_DIR` does not point there. `posix_fallocate()` fails on ZFS, so clients placing `wl_shm` pools in it fail; `linux-dmabuf` (P33) spares GPU clients, which is why this is survivable rather than fatal. **Compositor-side this is a non-issue** — wlroots uses anonymous POSIX SHM (BLUEPRINT §13 FB-1). Fix is administrative: point `pam_xdg` at a tmpfs, or mount one at that path.
- [x] ~~**P2-14 runtime verification:** confirm wlroots retains `current_mode` across output disable/enable — `hikari_output_enable()` re-enables without setting a mode (`src/output.c`); if the mode was cleared on disable, lock-mode Ctrl+C leaves outputs dark. (Salvaged from the retired investigation report, Phase 22.)~~  
  **DUPLICATE → R4** — same item as F4; tracked once, gated on R7-a. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*
- [x] ~~**PAM Verification:** Verify `hikari-unlocker` works correctly with OpenPAM setuid 4555 on a live FreeBSD Wayland session.~~  
  **TRACKED AS R7-b** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~**Layer-client spot check:** run a panel/bar (or swaybg) with a `WITH_LAYERSHELL=YES` build to exercise the new scene attachment.~~  
  **TRACKED AS R7-f** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [x] ~~**TC-FORMAT-01:** Run `clang-format` compliance check against `.clang-format` rules.~~  
  **TRACKED AS R8** in `PLANS.md` item -14. *(Consolidated by the Phase 84 stale-sweep, 2026-08-22.)*
- [ ] **Comment-header rollout (optional, deferred).** **50 of 65** `src/` files lack the `[COMMENT] Script function and purpose:` header (verified 2026-08-22; the older note said 48 of 55). AGENTS.md says not to add commenting retroactively without explicit instruction, so this stays deferred unless asked.
- [x] ~~**Cosmetic:** silence enum-compare warnings at `src/dnd_mode.c:63` and `src/move_mode.c:78` (value-identical constants; harmless).~~  
  **STALE — VERIFIED** — 0 warnings from `dnd_mode.c`; checked 2026-08-22. *(Closed by the Phase 84 stale-sweep, verified 2026-08-22.)*


