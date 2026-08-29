# Forward Strategy & Plans

*Last Updated:* 2026-08-29 16:46

## Implementations to be Fully Implemented

-23. **PHASE 96 CYCLE 2 -- cross-screen window motion, part II. Planned 15:22; fully ruled 15:41; implemented 15:44; RUNNING ON HARDWARE 16:46 (user, PROVISIONAL). Documentation moved with the behaviour at 16:46.**

   **Complete but for three verifications, and they are named rather than glossed: V-3** (span both screens, hold still, move only the cursor) **is the only check that exercises T-13, which is half the reported defect**; **V-4** (Firefox) matters because T-10 is proportional to the client's configure round-trip; **V-6 discharges R-3**, still open. **Documentation was moved by R-4's reasoning -- not deferrable to Phase 102 when prose becomes false -- and is NOT a Phase 102 pass: D-2/P-9 remains Phase 102's, untouched.**

   **Rulings taken from the user 2026-08-29 15:41: Q12 button release · Q13 yes, anchored window origin · Q14 unconditional.** All three as recommended, so nothing in the 15:22 analysis is invalidated and T-11, T-12 and T-13e are unblocked. **Two implementation hazards were found while shaping the fixes and are ruled inside their parent questions rather than tabled as new ones** -- **H-1**, the anchored origin leaves the layout far more readily than a cursor does, so Q13 falls back to the cursor's output when the origin resolves to none; and **H-2**, the Q12 hold is a query on move mode rather than a latch, so there is no flag to leak and automatic tiling cannot be silently killed by an unusual exit from move mode. Both in `DECISIONS_LOG.md` at 15:41.

   Full analysis in `DECISIONS_LOG.md` at 15:22; ordered task list in `TODOS.md` Phase 96 Cycle 2. **Does not supersede item -22's sequencing and does not renumber the programme** -- this is the second cycle of Phase 96, not a new phase. Phases 97-103 keep their numbers and their order.

   ### Why there is a cycle 2

   **Cycle 1 is delivered and the reported symptom changed rather than went away.** Commit `586be1e` landed T-1, T-4, T-5, T-6, T-7a and R-4; every one was re-read in the tree at 15:22. The user then reported: *"trying to move windows to the other display -- instead of the windows being moveable, when trying to move them across they constantly snap back to the display they are on, and also cause visual choppy tearing."*

   Cycle 1 fixed a **whip** -- a window flung to the far edge of the external monitor and eased back over 120 ms. This is a **rewind** -- the window tracks the pointer and is then yanked backwards. **T-1 did not cause T-10; it uncovered it.** The rewind happens inside a client round-trip, which is precisely the interval the 120 ms whip used to occupy.

   ### R-5 -- the trackers recorded cycle 1 as unstarted

   `BRIEFING.md` at 11:35, `PLANS.md` item -22 and `TODOS.md` Phase 96 all said *"Not started"* / *"0 of 6 code items started."* All six were delivered. **Third instance of the failure shape recorded as R-4 and as FB-4's ~60-phase survival: a recorded fact whose preconditions moved out from under it.** The rule adopted at 08:57 -- *a recorded finding is not a verified one* -- is what caught it, and is why every citation in this cycle was read in the tree before it was written down. Verification table in `DECISIONS_LOG.md` at 15:22. **T-8 and T-9 are genuinely unstarted and stay in Phase 98.**

   ### The four causes

   | # | What | Symptom half | Ruling | State |
   |---|---|---|---|---|
   | **T-10** | A queued geometry operation carries the **crossing-instant origin** across the client round-trip and overwrites the live drag position when it commits | **"snaps back"** | none needed | **Not started -- CRITICAL** |
   | **T-11** | The arrival reflow drains at the tail of that same commit and re-tiles the whole destination sheet, including the window under the pointer | **"snaps back"** (second jump) | **Q12 RULED** | Ready |
   | **T-12** | The migrate branch is chosen from the **cursor** while the window is placed at **cursor - anchor**, so the two disagree for the width of the grab offset and jitter re-fires a full migrate per motion event | thrash under both | **Q13 RULED** | Ready |
   | **T-13** | Damage and frame scheduling are routed to `view->output` **only**; the half of a spanning window on the neighbouring screen repaints only when something unrelated damages that output | **"choppy tearing"** | **Q14 RULED** | **Not started -- CRITICAL, on no prior tracker** |
   | T-14 | Release-time re-crop is correct (Q5 + Q11) but is currently the third of three stacked jumps | -- | Q5, Q11 | Verification only |
   | T-15a | `assert(focus_view != NULL)` in `move_mode.c` is a null-deref under `-DNDEBUG` | -- | -- | Not started |
   | T-15b | `step` under 9 would make a keyboard move unable to leave a screen, via T-6a's 9 px clamp | -- | -- | **Record only** |
   | R-5a | Correct the cycle-1 state records in all three trackers | -- | -- | Not started, no source |

   ### T-13 is the finding this cycle turns on, and it was on no tracker

   `hikari_output_add_damage()` (`include/hikari/output.h:104-114`) takes a `struct wlr_box *region`, checks it for NULL, and **never reads it**. Every view damage path passes `view->output` and nothing else. So a window straddling the seam schedules frames on one screen and the other half goes stale for whole frames at a time.

   **This is not T-3 and it does not reopen it.** T-3 -- eDP-1 at 60.026 Hz against DP-3 at 60.000 Hz, beating with a ~38 second period -- was measured, is irreducible in software, and **bounds the two halves to one frame apart.** T-13 has no such bound. T-3 remains the floor.

   **It does not reopen T-2a either.** T-2a asserted the single-output *animation* driver is correct by construction once Q1 makes arrivals instant, because no window is ever **in flight** across the seam. That is intact. T-13 is about *damage*: a window can be **at rest** spanning two screens -- **Q6 explicitly permits it for a floating one** -- with no animation active at all.

   **And the negative result from cycle 1 still holds, re-verified rather than carried forward: this is not scanout tearing.** `wlr_tearing_control_manager_v1_create()` at `src/server.c:1703` has no listener, `tearing_page_flip` is set nowhere, and every page flip hikari performs is vblank-synchronised. **T-8 is unchanged and stays in Phase 98.**

   ### Divisibility

   **T-10 and T-13 are independent and each accounts for exactly one half of the report. Neither alone closes it.** Fixing T-10 alone leaves a window that moves correctly and tears at the seam; fixing T-13 alone leaves a window that renders cleanly and refuses to go. **T-11 and T-12 are what make the result feel correct rather than merely be correct** -- they remove the per-motion-event migrate storm. T-12 is one expression but it changes *when* a crossing happens, so it lands with T-11 rather than alone. T-15a travels with any of them; R-5a is trackers only and can stand alone.

   ### The three open questions, with recommendations

   - **Q12 (T-11) -- the arrival re-tile.** Re-tile mid-drag as now, or defer both reflows to button release for pointer drags? **Recommended: defer, pointer drags only.** Preserves **Q3** (destination folds per `layout { on-insert }`) and **Q4** (source closes its hole per `reflow-on-close`) exactly and changes only *when*. The keyboard `view-move-*` path has no drag and keeps its immediate reflow. **No new configuration key.**
   - **Q13 (T-12) -- the branch test.** Pick the migrating output from the **anchored window origin** rather than the raw cursor? **Recommended: yes.** One expression; branch and placement then agree by construction. **The keyboard path already does it this way** (`src/server.c:2507-2532` probes the window's top-left), so this makes the two consistent rather than inventing a rule. **Behaviour change, named rather than slipped in: the crossing happens when the window's origin crosses, not when the cursor does.** **Q2 is not revisited** -- the grab point is kept, which is what T-4's anchor implements.
   - **Q14 (T-13e) -- damage fan-out shape.** Every intersecting output **unconditionally**, or only while `may_spill()` is true for that view? **Recommended: unconditionally.** Two-box arithmetic; correctness should not depend on a policy read; and a clipped window's box intersects only its own output, so the extra call is naturally absent rather than suppressed.

   ### Verification -- the user's, on hardware

   V-1 tiled drag across the seam: tracks the pointer, no rewind, lands once, no oscillation. V-2 floating drag: stays where dropped, straddling permitted, never re-tiled (Q6). **V-3 span both screens, hold still, move only the cursor -- both halves must repaint together; this is the T-13 test and nothing else exercises it.** V-4 repeat V-1 with **Firefox**, where the round-trip is longest and T-10 is most visible. V-5 keyboard `view-move-right` across and back -- the path T-12 must not regress. **V-6 -- R-3 falls due here**: M-V2 passed only provisionally and was to be re-run after Phase 96, and cycle 2 rewrites `move_mode.c` again.

   ### Where this leaves the programme

   Unchanged. **97** tiling manipulation (M-3 + P-5) -- and P-5 still builds on `hikari_server_migrate_focus_view()`, which this cycle touches, so 97 gains from waiting. **98** motion polish, T-8, T-9. **99** screen configuration. **100** screen management and memory. **101** build preflight. **102** documentation. **103** release. **Q10's ordering holds: nothing is tagged or versioned here.**


-22. **PHASE 96-103 -- THE PROGRAMME: cross-screen motion, tiling manipulation, screen configuration, then documentation and release. Planned 2026-08-29 11:19; re-verified and fully ruled 2026-08-29 11:35. TWELVE RULINGS TAKEN, NO OPEN QUESTIONS.**

   **CORRECTED 2026-08-29 15:22 (R-5a): PHASE 96 IS DELIVERED, NOT UNSTARTED.** Commit `586be1e` landed T-1, T-4, T-5, T-6, T-7a and R-4; every one was re-read in the tree at 15:22. The state column below and the "all six code items are unstarted" note at the end of this item were true when written and are now false. **T-8 and T-9 are genuinely unstarted and stay in Phase 98.** Phase 96 has a **second cycle** at item -23 -- the reported symptom changed rather than went away.

   Full analysis in `DECISIONS_LOG.md` at 11:19; ordered task list in `TODOS.md` Phase 96. Supersedes item -20's sequencing from P-3 onward. **Item -21 (P-1) is delivered, built, installed and running.**

   **Rulings taken from the user 2026-08-29 11:19:** Q1 **snap across screens, glide within one**; Q2 **keep the grab point**; Q3 **follow `layout { on-insert }`**; Q4 **follow `reflow-on-close`**; Q5 **spill while dragging, clip otherwise, plus a configuration key to force always-spill**; Q6 **floating stays floating wherever dropped**; Q7 **screen alignment configurable, with edge-alignment tuneables and an auto-centre form**; Q8 **stop announcing the tearing protocol**; Q9 **tiling manipulation before screen configuration, both required**; Q10 **tag after documentation, port against the tag**. Plus **R-2: the `install-user` wallpaper path is permanently deferred to v1 tagging time** and **R-3: M-V2 provisionally passes, re-run after Phase 96**.

   **Rulings taken from the user 2026-08-29 11:35:** **Q11 clip tiled windows only, never a floating one, always-spill key overrides** -- which unblocks T-6 and leaves Phase 96 with **no open questions** -- and **R-4 change the shipped `layout { auto }` default to `true`** so the template matches the configuration that is actually tested.

   ### Order, by necessity

   | Phase | What | Why here |
   |---|---|---|
   | **96** | Cross-screen window motion | The reported defect. Nothing else is worth doing while moving a window between screens looks broken |
   | **97** | Tiling manipulation (M-3) + send-to-screen (P-5) | Q9. With `auto = true` live, a tiled sheet **cannot be rearranged at all** -- no directional move, no split ratio |
   | **98** | Motion polish and loose ends | M-4b/c/d/e, M-9, T-8, T-9. Cheap, and all of it sits in code Phase 96 and 97 will have just touched |
   | **99** | Screen configuration (P-3, P-4 + Q7) | Depends on P-1's re-deriving handler, delivered |
   | **100** | Screen management and memory (P-7, P-6, O-8) | Depends on 99 |
   | **101** | Build preflight (P-8 / C-1..C-3, C-6) | Independent; before documentation so the documented install is the real one |
   | **102** | Documentation (D-2/P-9, D-3..D-7) | Q10 -- documents what is then true, not what was planned |
   | **103** | Release (P-2, D-8, version, tag, FreeBSD port) | Q10 -- last |

   ### Phase 96 -- cross-screen window motion

   | # | What | Ruling | State |
   |---|---|---|---|
   | T-1 | Cancel and re-base the animation on every screen change (`hikari_view_migrate`, `hikari_view_evacuate`); a cross-screen placement is instant | Q1 | **DELIVERED `586be1e`** |
   | T-2 | Verify the per-output animation driver is correct **by construction** once T-1 lands; guard, do not restructure | Q1 | **DELIVERED `586be1e`** |
   | T-4 | Carry the grab anchor through the crossing branch of `move_mode.c` | Q2 | **DELIVERED `586be1e`** |
   | T-5 | State-aware migrate: floating stays floating; tiled folds into the destination layout per `on-insert`; the source sheet closes its hole per `reflow-on-close` | R-1, Q3, Q4, Q6 | **DELIVERED `586be1e`** |
   | T-6 | Clip a resting **tiled** window to its own screen, never a floating one, lift the clip while dragging, add the always-spill configuration key | Q5, **Q11** | **DELIVERED `586be1e`** |
   | T-7a | Refuse to place a window's origin in the dead band between mismatched screen heights | Q7 (guard only) | **DELIVERED `586be1e`** |
   | R-4 | Change the shipped `layout { auto }` default to `true`, and move the config comment block and `hikari(1)` prose with it | R-4 | **DELIVERED `586be1e`** (`etc/hikari/hikari.conf:280`) |

   **T-1 is the phase.** It is the whip across the external monitor and it is the only item that alone accounts for the reported symptom. T-4 is independent, small, and is Phase 91's own fix applied to the branch it missed.

   **Q11 IS RULED (2026-08-29 11:35) and Phase 96 now has no open questions.** The collision between Q5 (clip at rest) and Q6 (a floating window rests where it was dropped, straddling permitted) resolves as the recommended reading: **clip tiled windows, never clip floating ones, and let T-6c's always-spill key override the whole behaviour.** T-6 is unblocked and lands in this cycle. The remaining work in T-6 is `T-6d`'s design note, unchanged: `wlr_scene_subsurface_tree_set_clip()` clips a surface tree, while a view's border and indicator-frame rects are separate `wlr_scene_rect` nodes in the same tree.

   **R-4 is new (11:35) and was found by cross-referencing the trackers against the tree.** `etc/hikari/hikari.conf:266` ships `auto = false` while the live file has `auto = true`, and **the entire Phase 96 analysis rests on the live value** -- a fresh checkout does not reach T-1's severe path, so the reported defect would appear not to reproduce. **Ruled: change the shipped default to `true`.** It touches no compositor source and is independent of every T-item, but **its documentation half is not deferrable to Phase 102** -- the config comment block at `:250-263` and `hikari(1)`'s LAYOUT text both assert the old default in prose and become false the moment the value flips.

   ### Phase 97 -- tiling manipulation

   **M-3**, the largest gap in daily use: `src/action.c` offers only list-order `next`/`prev`/`main` for a tiled sheet, and **no action anywhere adjusts a split ratio**. Add directional movement within a layout and split-ratio actions. Plus **P-5** (`view-move-to-output-next`/`-prev`) on top of `hikari_server_migrate_focus_view()`, which Phase 96 will have just made state-aware -- the same action family, and it reuses T-5 rather than duplicating it.

   ### Phase 98 -- motion polish and loose ends

   M-4b/c/d (remaining motion decisions) · **M-4e** (indicators teleport while the window travels -- newly visible and adjacent to Phase 96's work) · **M-9** (global modifier state; real defect, no observed symptom) · **T-8** (stop advertising `wp_tearing_control_v1`: delete `src/server.c:44` and `:1696`, per Q8) · **T-9** (`track_damage` is written, toggled by a bound action, and read nowhere -- either wire it or delete the action).

   ### Phase 99 -- screen configuration

   **P-3** -- `mode`, `refresh`, `scale`, `transform`, `enabled` through **one** `apply_output_config()` shared by startup and reload; today `src/output.c:544-581` and `src/configuration.c:2497-2521` are divergent copies and only the first sets a mode at all. **P-4 + Q7** -- relative positioning between screens with a topological sort, **plus the alignment tuneables Q7 rules**: an edge-alignment key that holds regardless of screen size, and an auto-adjust/centre form so mismatched heights line up without hand-computed offsets. Once `refresh` exists, pinning both panels to one rate becomes possible and would remove the 38-second beat recorded as T-3 -- worth knowing, not a reason to do anything now.

   ### Phase 100 -- screen management and memory

   **P-7** -- `wlr-output-management-v1` behind `WITH_OUTPUT_MANAGEMENT`, applied atomically through `wlr_output_swapchain_manager`; this is what makes `wlr-randr`, `kanshi` and `wdisplays` work. All three headers are present in the installed wlroots 0.20.2. **P-6** -- per-monitor memory on the ruled identity chain `make|model|serial` -> `make|model|connector` -> `connector`. **O-8** -- lock backdrop and clock for a screen hot-plugged while locked.

   ### Phase 101 -- build preflight

   **P-8 / C-1, C-2, C-3, C-6.** `make check-deps` probing every pkg-config module and every required tool, failing fast and naming the exact `pkg install` line, wired as a prerequisite of `all` and `install`. Installs nothing. Plus declaring the runtime dependencies somewhere machine-readable.

   ### Phase 102 -- documentation

   **D-2 / P-9** (the two statements `OUTPUTS` does not make, now rewritable as what is actually true after 99 and 100) · **D-3** (11 documented actions bound nowhere) · **D-4** (task-oriented cookbook) · **D-5** (troubleshooting) · **D-6** (README never states the Makefile is BSD make) · **D-7** (`XDG_CURRENT_DESKTOP` rationale).

   ### Phase 103 -- release

   **P-2 / X-4a / M-7d** -- the `install-user` wallpaper path, **deferred to here by R-2 and due here**. **D-8** CHANGELOG and release notes. Version bump and tag. **P-10 / C-4** the FreeBSD port against the tag, with package origins confirmed on FreeBSD. Q10 is recorded as *preferably* (a) and explicitly to be revisited at the time.

   ### Sequencing note for whoever picks this up

   **Phase 96 is not divisible below T-1 + T-4 + T-5.** T-1 removes the whip, T-4 removes the teleport at the crossing, and T-5 is what makes the arrival correct under `auto = true`; fixing any two of the three leaves a visibly wrong cross-screen move. **T-6 is now fully ruled and joins the same cycle** (it was previously listed as splittable only because Q11 was open); T-7a is a one-line guard that can travel with any of them; **R-4 is documentation and configuration only and can travel with any of them or stand alone.**

   **Re-verified against the tree 2026-08-29 11:35.** Every file-and-line citation in this item was re-read rather than trusted, and all of them resolve exactly; all six code items were unstarted **at that time**. **Superseded 15:22 by R-5a: all six are now delivered.** The verification table is in `DECISIONS_LOG.md` at 11:35. This follows the rule adopted at 08:57, when an uncited analysis was found to contain four false claims: a recorded finding is not a verified one.

   **Q1 is why this phase is small.** Snap-on-crossing means no window is ever in flight across the seam, so the per-output animation driver never needs unifying and T-2 is a verification rather than a change. Had Q1 gone the other way, Phase 96 would have been a rewrite of the animation driver that still could not have removed the 60.026/60.000 Hz beat.

   **What no fix can remove, recorded so it is never re-investigated:** a pointer drag of a *spilling* window will always show a boundary step bounded by one frame of travel, because the two panels flip independently. Under the Q5 default it is visible only while the button is held.

-21. **PHASE 95 P-1 -- DELIVERED 2026-08-29 10:21. Compiled and linked; NOT run. Everything else in item -20 is unchanged and still stands.**

   Decisions and the full verified account: `DECISIONS_LOG.md` at 10:21. Task state: `TODOS.md` Phase 95 SS-P-1.

   **Rulings taken before execution:** **L-1b = option (A)**; **N-2 folded into P-1**; **M-8j closed by the user** -- the specific keyboard, now working, which closes M-8 entirely.

   ### Delivered

   | # | What | Where |
   |---|---|---|
   | L-1a/b | `full_area` at the output's layout origin; `usable_area` translated back to output-local before the store | `src/layer_shell.c` |
   | L-1c/e | Verified correct once L-1a landed; no edit | -- |
   | L-1d | Sweep complete -- **no fifth omission** | -- |
   | L-2c | `focus()` resolves the layer's own workspace and assigns `hikari_server.workspace` | `src/layer_shell.c` |
   | X-1 | `hikari_output_update_geometry()` as the single entry point; the layout-change handler re-arranges layers and schedules a reflow | `src/output.c`, `src/server.c` |
   | X-2 | Noop output geometry initialised | `src/output.c` |
   | X-3 | Wallpaper re-decoded only on a dimension change | `src/server.c` |
   | N-2 | `cursor_move()` compares against `focus_layer` too | `src/normal_mode.c` |
   | Finding 5 | NULL-workspace guard in `hikari_layer_init()` | `src/layer_shell.c` |

   ### What this changes about the rest of the plan

   **P-3, P-6 and P-7 are now buildable on a handler that re-derives.** That was the reason P-1 was declared indivisible: all three mutate output geometry at runtime, and until now that path recomputed nothing. An output-management client can now move, rescale and hot-plug outputs without leaving the usable area and the layer arrangement stale from output init.

   **P-2 is untouched and is still one line** (`X-4a`, `Makefile:408-409`). It depends on nothing and remains the cheapest v1 blocker to close.

   **Two of the five v1 blockers are cleared in code and neither is closed.** V1-1 and V1-4 need the user's in-tree build and a run. V1-2 (Phase 92's M-1/M-2, still never run) is closed by the same build.

   ### Sequencing note for whoever picks this up

   **L-2c-iv is now testable for the first time and is the discriminating check.** Hover a layer surface on `DP-3` with no click and read `printf 'state\n' | nc -U $XDG_RUNTIME_DIR/hikari.sock`; it must report `output DP-3`. Before this phase it reported the other screen, and no amount of client-side work could have changed that.

-20. **PHASE 95 PLAN, RE-VERIFIED AND CORRECTED 2026-08-29 08:57. Supersedes item -19's sequencing and its estimates. NOTHING IMPLEMENTED.**

   Item -19 is left standing so a concurrent session can see what changed. Corrections and the full verified fact base are in `DECISIONS_LOG.md` at 08:57; the ordered task list is `TODOS.md` Phase 95 §P.

   **Four claims in -19 were false and are retracted:** that no default-keymap reference exists (`etc/hikari/hikari.conf:507-701` is one, grouped and commented by task); that `hikari_output_next`/`_prev` are dead (they are macro-generated and macro-called, `src/output.c:753-770` and `src/workspace.c:83-94`); that there are no keyboard commands for monitors (`workspace-cycle-next`/`-prev` are bound to `LS+n`/`LS+b` and to 3-finger swipes); and that `OUTPUTS` says nothing about multiple displays. **All time estimates in -19 are withdrawn** — they had no basis.

   **The verification step in -19 and in `TODOS.md` Phase 94 is struck.** Both directed the user to prove the layer-placement defect by setting `outputs { position }` and restarting. **The user's opening request for this work was to establish whether positioning screens is possible at all**, so the test assumes what it is meant to settle -- and it cannot work regardless, because panels are drawn at the layout origin irrespective of position and nothing downstream re-derives from a moved layout. The defect is already established by the `sofi` session's direct measurement: hikari's own IPC reported `output DP-3` active while `sofi` drew on `eDP-1`.

   ### The answer to the session's original question, stated plainly

   **`outputs { position }` is parsed, resolved and applied to the output layout -- and almost nothing downstream re-derives from it.** Verified end to end: parsed at `src/configuration.c:1758,1787-1789`; resolved by exact connector name then `"*"` at `:2744`, with `HIKARI_OPTION` merging only unconfigured fields (`include/hikari/option.h:41-50`); applied at startup at `src/output.c:570-581` with the configuration loaded (`src/server.c:1518`) before the backend starts (`:1857`); applied on reload at `src/configuration.c:2506-2516`. But `output_layout_change_handler()` (`src/server.c:1243-1288`) updates `output->geometry`, reloads wallpapers and repositions view scene nodes, then **stops** -- the function body contains no `usable_area`, no `arrange`, no `output_geometry` and no bar call. So the window layout box, the top bar's reservation and **every layer surface** stay where they were. **Positioning is a configuration key, not a working capability, and P-1 plus P-3 plus P-7 are what turn it into one.**

   ### Order

   | # | What | Depends on |
   |---|---|---|
   | P-1 | Layer placement, layout-change re-derivation, noop init, the focus gap, unguarded workspace deref, wallpaper re-decode | -- |
   | P-2 | `install-user` wallpaper path, one line | -- |
   | P-3 | `mode`/`refresh`/`scale`/`transform`/`enabled`, through one apply path shared by startup and reload | P-1 |
   | P-4 | Relative positioning between monitors | P-3 |
   | P-5 | Send a window to another monitor | -- |
   | P-6 | Windows return when their monitor does, keyed on the monitor not the port | P-1 |
   | P-7 | `wlr-randr` / `kanshi` support | P-1, P-3 |
   | P-8 | `make check-deps` | -- |
   | P-9 | The two statements `OUTPUTS` does not make | P-3, P-7 (so it documents what is then true) |
   | P-10 | FreeBSD port | P-9, a tag |
   | P-11 | User builds; M-V2 regression check on Phase 92's unrun fixes | -- |

   ### Sequencing note for whoever picks this up

   **P-1 is not divisible.** Fixing where panels are drawn without fixing when the arrangement is recomputed leaves the identical bug reachable through every hotplug and every position change -- and P-3, P-6 and P-7 all mutate output geometry at runtime, which is precisely the path that today re-derives nothing. Building any of them on the current handler would make the defect reachable from a GUI display panel instead of only from a config reload.

   **The estimates are absent deliberately.** The ones in -19 were invented and presented as measurements. Nothing here is estimated until it is scoped against the files it touches.

-19. **PHASE 95-99 -- THE PROGRAMME: coordinate space, dependencies, documentation, then multi-screen. Planned 2026-08-29; RULINGS TAKEN, NOTHING IMPLEMENTED, NOTHING BUILT.**

   Full analysis in `DECISIONS_LOG.md` Phase 95; task list in `TODOS.md` Phase 95; Phase 94's own tasks are unchanged and remain the first work item.

   **Rulings taken from the user 2026-08-29 08:28:** L-2 = **(c)**, additive to (a); dependency handling = **check-deps now, FreeBSD port at release**; phase order **as sequenced below**; per-monitor output identity; and the standing rule that **the agent never runs `sudo` and never installs -- every build, install and hardware test below is the user's.**

   **The v1 verdict, recorded: NOT YET, and the gap is five items** -- L-1 + L-2c; Phase 92's M-1/M-2 never run; the `install-user` wallpaper path; X-1; and the dependency preflight. Nothing was versioned, tagged or released. See `TODOS.md` Phase 95 §V1.

   ### Phase 95 -- coordinate space and output geometry *(contains four of the five v1 blockers)*

   | # | What | State |
   |---|---|---|
   | L-V1 | Confirm the layout topology by swapping absolute `outputs { position }` and **restarting** | **Not run -- user's.** See X-1d: reloading instead of restarting produces a **false negative** |
   | L-1a/b/c/e | The Phase 94 placement fix and its `usable_area` translate-back | Not started |
   | L-2c-i..iv | Close the layer focus gap at its cause, both halves | Not started -- ruled (c) |
   | L-1d | Sweep for a fifth scene-port omission | Not started -- X-1 and X-2 are two of them |
   | X-1a/b/c | Make `output_geometry()` the single entry point; re-arrange layers on layout change | Not started -- **this is what makes reload, mode change and hotplug correct at once** |
   | X-2a | Initialise the noop output's `geometry`/`usable_area` | Not started -- two lines |
   | X-3a | Cache the decoded wallpaper surface | Not started -- optional, do it while in the file |
   | X-4a | Fix the `install-user` sed ordering (M-7d) | Not started -- one line |

   **The L-1b hazard is bounded, and the audit that bounds it is already done.** `output->usable_area` has ~25 consumers -- `src/geometry.c:64-193`, `src/sheet.c:488`, `src/view_config.c:119-158`, and nine sites in `src/view.c` -- and **every one of them produces view geometry, which is output-local by construction** (`src/view.c:287-289` adds the output origin on the way to the scene). So the remedy is a translate-back before the store and the consumer audit is complete; what remains is verification, not discovery.

   ### Phase 96 -- dependencies and install

   `make check-deps` probing all nine pkg-config modules plus `wayland-scanner`, `pandoc`, `install` and `sed`, failing fast with the exact `pkg install` line, wired as a prerequisite of `all` and `install`. Plus declaring the runtime dependencies somewhere a reader can find them. **The failure mode being fixed was measured, not assumed:** bmake's `!=` turns a missing dependency into a *warning* and an empty variable, and the build proceeds to fail hundreds of lines later.

   ### Phase 97 -- v1 documentation

   D-1 (a `DEFAULT BINDINGS` section covering all ~115 shipped bindings, plus a "first ten minutes" table in README) and D-2 (a real multiple-displays section, **including what is not supported**, so nobody files `wlr-randr` as a bug), then D-6 and D-7. **This phase is about navigability, not coverage:** a mechanical audit found 70 of 70 configuration keys and 65 of 66 action names already documented in `hikari(1)`.

   ### The FreeBSD port

   `ports/x11-wm/hikari-sakura/{Makefile,distinfo,pkg-descr,pkg-plist}`, after Phase 97, against a tagged tree. This is the answer to "get all dependencies and install them during the installing process" and the release's distribution story. **Package origins must be confirmed on FreeBSD** -- they could not be queried from the environment this analysis was performed in.

   ### Phase 98 -- multi-screen configuration *(post-1.0)*

   O-2 (mode/refresh/scale/transform/enabled/adaptive-sync through **one** `apply_output_config()` shared by init and reload -- today two divergent copies), then O-5 (wire the dead `hikari_output_next`/`_prev`; add `view-move-to-output-*`), then O-3 (relative placement with a topological sort).

   ### Phase 99 -- output management and per-monitor memory *(post-1.0)*

   O-1 (`wlr-output-management-v1` behind `WITH_OUTPUT_MANAGEMENT`, applied atomically via `wlr_output_swapchain_manager` -- this is what makes `wlr-randr` and `kanshi` work; all three headers are present in the installed wlroots 0.20.2), then O-6 (per-monitor memory on the ruled EDID fallback chain), then O-8 (lock backdrop for an output hot-plugged while locked).

   ### Sequencing note for whoever picks this up

   **Phase 95 is not divisible and its order inside itself is fixed.** L-V1 measures the assumption the diagnosis rests on and must precede any code. L-1a is two lines; L-1b is the reason this is not a two-line phase; and X-1 is the reason L-1 alone is not enough -- fixing *where* layer surfaces are drawn without fixing *when* the arrangement is recomputed leaves the same bug reachable through every hotplug and every `outputs { position }` reload. They touch the same two boxes and must land together.

   **Phases 98 and 99 depend on X-1, not merely on L-1.** An output-management client will move, rotate and rescale outputs at runtime -- which is precisely the code path that today never re-derives `usable_area` or re-arranges layers. Building O-1 on the current geometry handling would make the defect trivially reachable from any GUI display panel instead of only from a config reload.

-18. **PHASE 94 -- LAYER SURFACES ARE DRAWN ON THE WRONG OUTPUT. Analysed 2026-08-29; NOTHING IMPLEMENTED, NOTHING APPROVED.**

   Raised in the `sofi` repository. Full analysis in `DECISIONS_LOG.md` Phase 94; task list in `TODOS.md` Phase 94.

   **No user ruling has been taken yet.** One question is tabled (L-2) and one non-code verification is outstanding (L-V1).

   ### The work, in dependency order

   | # | What | State |
   |---|---|---|
   | L-V1 | Confirm the layout topology by swapping absolute `outputs { position }` and restarting | **Not run** -- costs one restart, no code, and measures the one thing currently inferred |
   | L-1a | `full_area` origin becomes `output->geometry.x/y` in `arrange_layers()` | Not started -- ~2 lines, and the whole of the reported fix |
   | L-1b | Keep `output->usable_area` output-local across that change | Not started -- **this is where the difficulty actually is** |
   | L-1c/e | Verify the geometry read-back and popup unconstrain against corrected coordinates | Not started |
   | L-1d | Sweep for a fifth scene-port omission | Not started |
   | L-2 | Rule: unassigned layer surface follows **focus** (a, recommended) or **pointer** (b), or fix the layer-hover focus gap (c) | **Tabled, unruled** |

   ### Sequencing note for whoever picks this up

   **L-1a is two lines and L-1b is the reason this is not a two-line phase.** `full_area` and `usable_area` are the same box on entry; wlroots requires the pair it is given to share one coordinate space; and `output->usable_area` is read as **output-local** by `src/geometry.c` for every view position on the output. Change the origin without translating back and every window on every non-origin output moves by that output's layout origin -- silently, and only on a multi-output machine, which is the shape of bug this tree has already paid for twice.

   **Do L-V1 before writing any code.** The claim that `eDP-1` holds layout x=0 is inferred from `src/output.c`'s auto-placement rule, not measured -- `wl_output.geometry` reports `0,0` for both outputs because wlroots sends a hardcoded origin, and hikari's `zxdg_output_manager_v1` (`src/server.c:1588`) is what carries the real position. L-V1 measures it in one restart and its prediction is falsifiable.

   **The client side is already scoped and is deliberately not this project's work.** `sofi` has three genuine defects of its own in this area, recorded in its tree; **all three are unverifiable until L-1 lands**, because an explicit `-monitor DP-3` is today still drawn on `eDP-1`. Nothing here waits on them.

-17. **PHASE 91 -- LAYOUTS, MOTION, PALETTE. Planned and approved 2026-08-25; WP-A/B/C/D EXECUTED the same day. Remaining: WP-B3 only, deferred by user decision.**

   Four user asks. Full analysis in `DECISIONS_LOG.md` Phase 91; architecture in `BLUEPRINT.md` §18; task list and 24 user-run tests in `TODOS.md` Phase 91.

   **User rulings recorded:** resize animation **deferred**; hidden views are **unhidden and added to the layout**; sequencing in procedural order, not by ease.

   ### Delivered

   | WP | What | State |
   |---|---|---|
   | A | Automatic re-tiling on map/unmap, `layout { auto }`, default false | Done |
   | B1 | Grab anchor for move and resize modes; fixes the `border`-pixel shrink per resize entry | Done |
   | B2 | Position animation, `ui { animation }`, default off; `node_at()` offset so input follows what is drawn | Done |
   | B3 | **Resize animation** | **Deferred by user decision** |
   | C | 16-colour palette; three dead colourscheme keys revived; palette handed to `hikari-topbar` | Done |
   | D | `grid` border accounting; hidden views incorporated; config and man page rewritten | Done |

   ### What remains, and why

   * **WP-B3 (resize animation).** Only a stale-buffer scale is achievable, because a resize is a protocol round trip and the compositor never possesses intermediate sizes. It is visibly soft on text. **Do not build this speculatively** -- re-open only if the user asks for it after seeing position animation on hardware.
   * **Hardware verification.** Nothing in this phase has been run. 24 tests are listed in `TODOS.md`; **T9 (map while locked), T10 (rapid opens) and T15 (click a travelling window)** are the three that exercise the reasoning the design rests on, and are the ones to run first.
   * **The bar cannot re-theme on reload.** `hikari-topbar` is spawned once with no restart path, so a palette change reaches it only at the next compositor start. Making the bar re-readable is a separate, unscoped piece of work and was not attempted.

   ### Sequencing note for whoever picks this up

   The re-tile queue and the animation tick both hook paths that everything else in `view.c` converges on (`hikari_view_commit_pending_operation()` and `hikari_view_refresh_geometry()` respectively). **Both default to off**, so the risk of the hooks themselves is bounded -- but any future change to those two functions must keep the hooks, and `BLUEPRINT.md` §18 explains why each is where it is.

-16. **PHASE 90 -- CLIENT-DRIVEN FULLSCREEN + TOP-BAR MEDIA OVERFLOW. Planned 2026-08-24 09:11. CYCLE 1 EXECUTED 10:09; W-A/W-B EXECUTED 11:35. Remaining: cycle 2 (W-4 XWayland + W-5 foreign-toplevel) and W-3.5, both unstarted.**

   Two user-reported issues. Full analysis in `DECISIONS_LOG.md` Phase 90; task list in `TODOS.md` Phase 90.

   **User ruling recorded:** fullscreen covers the native top bar **now**; layer-shell coverage **tracked, not built** (FS-2, below). No new keybinding -- `L+f` stays maximize; fullscreen is answered where the client asks for it.

   ### Design decisions (D1-D8, rationale in DECISIONS_LOG)

   | # | Decision |
   |---|---|
   | D1 | No new keybinding. `L+f` / `view-toggle-maximize-full` untouched. |
   | D2 | `FLAG(fullscreen, 5UL)`, not a new `hikari_maximization` member -- that enum is switched on in 8 places; `flags` has 11 free bits. |
   | D3 | Fullscreen **shadows** maximize via one branch atop `refresh_geometry()` (`view.c:792`). Exit falls back with zero restore logic. |
   | D4 | New `HIKARI_OPERATION_TYPE_FULLSCREEN`. Exactly two exhaustive switches: `view.c:2206-2233`, `xdg_view.c:82-97`. |
   | D5 | `bool obscured` on `hikari_bar`, **separate from `enabled`** -- else `usable_area` changes and every tiled window reflows. |
   | D6 | Geometry from `output->geometry` dimensions, never `usable_area` (exclusive zones would shrink it). |
   | D7 | Client-reported geometry must not overwrite the fullscreen box (both commit handlers). |
   | D8 | Single entry point `hikari_view_set_fullscreen(view, bool)` for all three protocol paths. |

   ### W-1 -- Fullscreen state and geometry (`view.h`, `operation.h`, `view.c`)

   1. `FLAG(fullscreen, 5UL)` beside `FLAG(forced, 4UL)` (`view.h:176`); `struct wlr_box fullscreen_geometry` by value in `hikari_view`; `HIKARI_OPERATION_TYPE_FULLSCREEN` in `operation.h`.
   2. **The one line that does the real work** -- fullscreen branch at the top of `refresh_geometry()` (`view.c:792`), above the `maximized_state` test.
   3. `queue_fullscreen()` -- `geometry = {0, 0, output->geometry.width, output->geometry.height}`; **`op->center = false`** (Finding 7).
   4. `commit_fullscreen()` -- sets the flag, stores the box, `border.state = HIKARI_BORDER_NONE` unconditionally, **leaves `maximized_state` untouched**.
   5. `queue_unfullscreen()` -- clears the flag, re-queues whatever the view was (maximized / tiled / floating).
   6. `hikari_view_set_fullscreen()` -- the D8 entry point. Guards `is_mapped`, `!is_hidden`, `on != is_fullscreen`. On `is_dirty`, **defer** rather than drop (Finding 5).
   7. Two switch cases. `xdg_view.c:82-97` must give fullscreen **`WLR_EDGE_NONE`**, grouped with RESET/UNMAXIMIZE -- **not** with the maximize cases.
   8. **Flag-clearing audit -- 9 sites, each verified by reading:** `commit_reset` (`:810`), `commit_unmaximize` (`:1528`), `commit_tile` (`:1438`), `toggle_vertical_maximize` (`:1709`), `toggle_horizontal_maximize` (`:1734`), `toggle_floating` (`:1751`), `hikari_view_unmap` (`:1211`), `hikari_view_fini` (`:440`), `hikari_view_evacuate` (`:1610`). **A stranded flag leaves the bar hidden forever -- this audit is the deliverable, not a footnote.**
   9. **New guards required.** `move_view()` (`:194`) and `queue_resize()` (`:851`) already early-return for `FULLY_MAXIMIZED`, so a fullscreen-over-maximized window is protected for free -- but a **floating** window has no `maximized_state`, so both need an explicit `is_fullscreen` early-return or a client can move/resize itself out of fullscreen.

   ### W-2 -- Bar visibility (`bar.h`, `bar.c`, `view.c`, `lock_mode.c`)

   1. `bool obscured` on `struct hikari_bar`. `hikari_bar_reserve()` (`bar.c:650`) **must not read it**. `hikari_bar_refresh()` sets node enablement to `!obscured`, and its cache-hit early-return must move **below** the obscured check or a cached frame skips the change.
   2. `hikari_bar_update_visibility(struct hikari_output *)` -- walks `output->workspace->views` (the *visible* list, per BLUEPRINT section 15, so a fullscreen window parked on another sheet correctly does not hide the bar) for `is_fullscreen && !is_hidden`.
   3. **Five call sites, not four:** `hikari_view_show()` (`:1299`), `hikari_view_hide()` (`:1334`), `hikari_view_unmap()` (`:1211`), `hikari_view_commit_pending_operation()` (`:2236`), and **`reset_visibility()` in `lock_mode.c`** -- newly identified: it writes `set_hidden`/`unset_hidden` **directly, bypassing show/hide**, so the other four miss a window that mapped or unmapped while locked (Phase 70 F2). Plus a defensive re-assert inside `hikari_bar_refresh()`.
   4. Lock-mode interaction verified, no conflict: `override_visibility()` disables `layers.top` wholesale so the bar is already hidden while locked; `reset_visibility()` re-enables the **tree**, and the bar's own buffer-node bit is independent, so `obscured` survives a lock/unlock cycle correctly.

   ### W-3 -- xdg-shell (Findings 1, 4, 5, 6) (`xdg_view.c`)

   1. **Rewrite `apply_requested_fullscreen()` (`:656`)** -- delete the `fullscreen != hikari_view_is_fully_maximized(view)` guard, call `hikari_view_set_fullscreen()`. Keep the unconditional `wlr_xdg_toplevel_set_fullscreen()` (protocol obligation). **This is Finding 1, Paths B and B'.**
   2. Map-time reconciliation (`:252-255`) -- same substitution, second site.
   3. **Deferred re-apply** for Finding 5: `pending_fullscreen` on `struct hikari_xdg_view`, drained in the existing `commit_handler` after dirty clears.
   4. **`request_maximize` listener -- Finding 4, a protocol violation.** `grep -n request_maximize src/xdg_view.c` returns nothing; `wlr_xdg_shell.h:212-219` says the compositor **must** listen and configure regardless. Registered at new_toplevel time (not map), removed in `toplevel_destroy_handler` beside `request_fullscreen` or wlroots asserts on teardown (Phase 57). **This is what makes a client's own maximize button work; it never has.**
   5. `requested.fullscreen_output` (Finding 6) -- `wlr_output->data` is the `hikari_output`, so resolution is a cast.

   ### W-4 -- XWayland (Findings 2, 3) (`xwayland_view.h`, `xwayland_view.c`)

   1. **`request_fullscreen` listener -- Finding 2.** Registered beside `request_configure` (`:628-630`); removal added to the 9-link block at `:277-286`. Reads `surface->fullscreen` (`xwayland.h:182`), calls `wlr_xwayland_surface_set_fullscreen()` then `hikari_view_set_fullscreen()`.
   2. **`request_configure_handler` must not clamp a fullscreen view -- Finding 3.** `:333-366` currently ends in `hikari_geometry_constrain_absolute(&geometry, usable_area, ...)`, which walks an X11 client straight back under the bar.
   3. Map-time reconciliation mirroring `xdg_view.c:252` -- an X11 client can be fullscreen before it maps.
   4. **D7 guard** in `commit_handler`'s else-branch, which writes `surface->width/height/x/y` back through `hikari_view_geometry()`.
   5. All 10 -> 11 listeners re-verified for exactly-once removal (Phase 57/78 precedent).

   ### W-5 -- foreign-toplevel (`foreign_toplevel.c`)

   Split `set_full_maximize()` (`:177`): `request_fullscreen_handler` (`:205`) -> new `set_fullscreen()`; `request_maximize_handler` (`:190`) unchanged. `publish_state` (`:408-418`) reports each from its own state. **Delete two now-false comments** (`:167-171`, `:415-418`) which both assert *"hikari has no fullscreen state at all"*. `can_act()` already gates lock mode -- no change. **Closes the `TODOS.md` Phase 89 known consequence.**

   ### W-A / W-B -- top-bar media overflow (`bar.c`, `bar.h`, config, docs)

   **W-A, structural containment (ships first, makes W-B safe):** per-run right limits after the measure pre-pass; clamp `center_x`/`right_x` to `>= PADDING` (both can currently go negative); wrap each block's draw in `cairo_save`/`cairo_rectangle`/`cairo_clip`/`cairo_restore`. **`cairo_clip` rather than `pango_layout_set_width` + ellipsize** -- `set_width` without ellipsize wraps instead of truncating, and the interaction is too version-dependent to be the tool for a *guarantee*. Add a local `utf8_valid_prefix_len()` (~20 lines, no new dependency) before `pango_layout_set_text()`. Correct the two false comments (`bar.c:32-34`, `:41-44`).

   **W-B, cap and banner scroll:** `int scroll_offset` on `struct hikari_bar_block`; `parse_line()` (`:233`) reworked to build into a scratch array and carry the offset forward when the block at the same index has identical `full_text` (text change resets to 0); render a window of `max_chars` **codepoints** from `full_text + separator` taken modulo the combined length so the banner wraps continuously; `scroll_offset` added to **both** the sizing and writing `snprintf` calls in `build_cache_key()` (`:291`), which are duplicated and must stay in sync; `wl_event_source *scroll_timer` on `hikari_topbar_source`, armed only while a block overflows and torn down in `_fini`. New `ui { bar { max-block-chars = 26; scroll-interval = 300; scroll-separator = "   *   "; } }` following the `hikari_lock_config` pattern exactly; `0` disables capping.

   ### Sequencing

   | Cycle | Contents | Est. | Risk |
   |---|---|---|---|
   | 1 | W-1 + W-2 + W-3 + docs | ~5-6 h | MED-HIGH (`src/view.c`) |
   | 2 | W-4 + W-5 + docs | ~3 h | MEDIUM (XWayland) |
   | -- | W-A + W-B (independent subsystem) | ~4.5 h | LOW-MED |

   Cycle 1 fixes the reported symptom for native Wayland browsers; cycle 2 is required for mpv/VLC/Steam. **Build blocked until `sudo make clean`** -- `main.o` is root-owned. Compile in-tree for 0 warnings across three configurations, hand off **implemented, unbuilt**.

   ### Risk register

   | Risk | Sev | Mitigation |
   |---|---|---|
   | Stranded flag -> bar hidden permanently | **HIGH** | The 9-site audit (W-1.8). Test 7. |
   | `-Wswitch` miss | MED | Exhaustively enumerated; `-Wall` catches at compile time. |
   | wlroots teardown assertion on the new XWayland listener | MED | Phase 57 precedent; removal site identified. |
   | `src/view.c` regression | **HIGH** | Eight crash phases in this file. Cycle 1 ships alone. |
   | Bar reflows tiled windows | MED | D5. Test 13. |
   | Client rewrites the fullscreen box | MED | D7, both commit handlers. |

   **Rollback:** each workstream independently revertible. W-1+W-2 without W-3/W-4 is inert (nothing sets the flag). **Emergency single-line rollback: make `hikari_view_set_fullscreen()` an unconditional early return.**

   ### FS-2 -- TRACKED, NOT BUILT (the user's "(b) tracked")

   **Fullscreen over layer-shell surfaces.** Hiding the top bar does not lift a fullscreen view above `TOP`/`OVERLAY` layer-shell clients -- waybar, notification daemons, and the left-edge side panel of item -15, which BLUEPRINT section 16 specifies as a `TOP`-layer client.

   * **Why deferred:** no `TOP`-layer client runs on this system today, so it fixes nothing observable while carrying real risk.
   * **Why it will matter:** the moment the side panel lands it covers fullscreen video. **Gate FS-2 on that work, and do not build the panel without it.**
   * **Mechanism when built:** reparent the fullscreen view to `layers.top` and raise it.
   * **The blocker is not lock safety.** Verified: `override_visibility()` disables the whole `top` tree, so a fullscreen view parented there stays correctly hidden while locked, and `layers.lock` sits above `top` regardless. **The blocker is the map-time layer derivation at `view.c:1160-1167`**, which re-derives a view's parent on every map and would silently drop a remapped fullscreen view back into `layers.views`.

-15. **FUTURE INTENT (documented 2026-08-22, not planned or scheduled): left-edge sliding application panel.**

   User-stated goal: *a side panel that slides in and out from the left, listing applications.* Recorded so the intent is not lost and so later work does not accidentally foreclose it. **This is not an approved workstream and nothing is scoped.**

   **R2 is its enabling dependency and is now delivered and confirmed working (waybar, 2026-08-22).** `ext-foreign-toplevel-list-v1` is what lets anything -- including a panel -- ask the compositor what windows exist. Without it the question could not be asked at all.

   Notes for whoever builds it, from what this codebase already establishes:

   * **It can be an ordinary layer-shell client**, not compositor code. hikari advertises `zwlr_layer_shell_v1` v4, and Phase 73 gave layer surfaces genuine per-layer scene trees, so a `LEFT`-anchored panel on the `TOP` layer will stack correctly above windows and below the lock screen without any further compositor change.
   * **Slide animation belongs in the client.** wlroots' scene graph has no animation facility and hikari adds none; a panel animates by moving or resizing its own surface.
   * **It will be hidden while locked, automatically** -- lock mode disables the whole `top` layer tree (Phase 73), so a panel cannot leak window titles onto a locked screen. That property is worth preserving.
   * **A compositor-side panel is the alternative** and is deliberately *not* recommended: it would need its own cairo/Pango rendering, its own input routing, and would grow the compositor for something a client can do with protocols it already speaks.
   * **What is still missing for a fully-featured panel:** foreign-toplevel v1 exposes title and app_id only -- no icons, no activation, no minimise. Activating a window from a panel would need `zwlr_foreign_toplevel_management_v1` (not currently advertised) or `xdg-activation-v1` (which hikari **does** advertise). Worth checking which the chosen panel expects before writing one.

-14. **REMAINING WORK PROGRAMME -- proposed 2026-08-22 15:16, AWAITING APPROVAL. No step is approved for execution.**

   Everything still open after Phases 70-83, scoped and sequenced. The lock-screen programme (W1-W6), the scene-graph restructure, XWayland integration and the portal work are all delivered and hardware-confirmed; what follows is the remainder.

   **Sequencing principles, all learned the hard way this session:**
   * **One risky change per build cycle.** Phase 78 bundled two and a crash would have been ambiguous; Phase 75 bundled a guess with an evidence-backed fix and the guess survived a whole cycle on borrowed credibility.
   * **`src/view.c` gets its own cycle, always.** It is the file behind eight crash phases (42, 44, 45, 55, 56, 57, 61, 63). R2 and R3 both touch it and **must not ship together**.
   * **Re-verify before acting on any recorded environmental claim.** FB-4 was carried as CRITICAL for ~60 phases after it stopped being true (Phase 83). R1 exists because of this.
   * **Use a library's constructors; never hand-build its structs.** Two crashes, one root cause (Phase 76).

   ```
   R1 ─────────────────────────────► independent, do first (cheap, unblocks accurate prioritisation)
   R7 ─────────────────────────────► user-run verification, independent
   R6 ── R2 ── (separate cycle) ── R3
   R4 ◄── gated on R7-a (W0-6)
   R5 ─────────────────────────────► large, needs its own scoping decision
   R8, R9 ─────────────────────────► hygiene, any time
   ```

   ---

   **R1. Tracker stale-sweep -- DO FIRST. Agent work, ~1 h, zero risk.**

   `TODOS.md` carries roughly 30 unchecked items, and a sample shows **many are already resolved** -- the Phase 64/70 "XWayland renders no content" entries (fixed in Phase 78), "XWayland does not start" (fixed Phase 68), W0-1..W0-5 (moot since Phase 83), the libdrm and clock-offset questions (settled Phase 82), FB-6 (resolved Phase 73), and a P2 claiming `setup_idle_inhibit` is unguarded (it is guarded, verified 2026-08-22).

   This is the FB-4 disease at file scale: entries treated as open because nobody re-read them. Every subsequent prioritisation reasons from a list that is partly fiction.

   1. Walk every unchecked box in `TODOS.md`; for each, verify against current code or mark as superseded, citing the phase that closed it.
   2. Same pass over `PLANS.md` items -10 through 4 -- several describe work long since done.
   3. Add a **last-verified date** to each `BLUEPRINT.md` section 13 row, per the Phase 83 process note.
   *Acceptance:* every remaining unchecked item is confirmed live against the current tree.

   **R2. [DELIVERED + HARDWARE-CONFIRMED -- Phase 88] W7b -- `ext-foreign-toplevel-list-v1`.**

   The only undelivered workstream. Serves taskbars (waybar `wlr/taskbar`) and future window-selection portals; **not** required for screen sharing (verified Phase 78: portal-wlr captures outputs and has no window picker).

   1. `wlr_ext_foreign_toplevel_list_v1_create(display, 1)` in `server.c`.
   2. `struct hikari_view` gains a handle pointer and a stored `app_id` -- views currently receive `app_id` in `hikari_view_configure()` but do not retain it.
   3. Handle lifecycle: create in `hikari_view_map()`, destroy in `hikari_view_unmap()`, update in `hikari_view_set_title()`, release in `hikari_view_fini()`.
   *Risk:* six touch points in `view.c`. Follow the Phase 78 pattern -- audit every new listener/pointer for exactly-once release, and check destroy ordering.
   *Acceptance:* a taskbar client lists windows and tracks title changes; open/close/retitle leaks nothing.

   **R3. [DEFERRED INDEFINITELY -- user decision, Phase 87] Remove the `forced` flag. ~4-6 h. MUST NOT share a cycle with R2.**

   The declared Phase 73 deviation. 15 sites, several in **provably unreachable** branches given the `view.c:108` invariant (`forced` implies `hidden`). With the layer trees, the flag no longer decides visibility -- it is pure bookkeeping.

   1. Enumerate all 15 and classify: live, dead-given-invariant, or redundant-with-trees.
   2. Remove the dead branches **first, as a separate reviewable step**, before deleting the flag.
   3. Delete `FLAG(forced, 4UL)` and simplify `commit_pending_operation()` and `hikari_view_migrate_to_sheet()`.
   *Risk:* highest of any item here. Pure cleanup with no user-visible gain -- **defer indefinitely if the appetite for risk is low.** F1/F2 are already fixed by the trees.
   *Acceptance:* the Phase 73 lock-screen test list passes unchanged.

   **R4. F4 / P2-14 -- `hikari_output_enable()` sets no mode. ~30 min. GATED on R7-a.**

   `output.c:323-354` re-enables an output without `wlr_output_state_set_mode()`, unlike `hikari_output_init` (`:553-556`). If wlroots does not retain `current_mode` across a disable, the first keypress after the lock blank leaves the session dark.
   **Do not implement speculatively.** R7-a answers it in two minutes; if the screen returns, this needs no fix and the item closes.

   **R5. Dead-assert remediation. LARGE -- needs a scoping decision before any work.**

   **255 asserts across the tree, 101 in `view.c` alone**, all compiled out by `-DNDEBUG` in every shipped binary. Phase 61 approved always-on `wlr_log(WLR_ERROR)` + safe bail as the replacement policy; only a handful of sites have had it applied.

   **Do not sweep mechanically** -- Phase 47 showed one assert (`add_keyboard`) to be a sound invariant that would have been wrong to rewrite defensively. Triage into three buckets: (a) guarding an allocation or external return value -> convert; (b) documenting an internal invariant that cannot fail given the call graph -> leave; (c) genuinely unreachable -> delete. **Only bucket (a) carries live crash risk.**
   *Proposal:* scope to bucket (a) only, and only outside `view.c` in the first pass. `view.c`'s 101 are entangled with R3 and should follow it.
   *Needs a decision:* full programme, bucket (a) only, or defer.

   **R6. Retire the `hikari_server_create_argb8888_buffer` shim. ~20 min, near-zero risk.**

   W1 kept it "for one release". Three callers remain -- `lock_indicator.c:52`, `bar.c:836`, `indicator_bar.c:190` -- plus a stale comment reference at `bar.c:45`. Point them at `hikari_buffer_create_argb8888()` and delete the shim and its declaration.
   *Acceptance:* no reference remains; bar, indicator bars and lock indicator render unchanged.

   **R7. Verification backlog -- USER-RUN.**

   * **R7-a. W0-6 (~2 min, highest value here).** Lock, wait past the blank timeout (set `blank-timeout-ac = 10` to avoid waiting three minutes), press a key. Screen returns => R4 closes with no code change.
   * **R7-b. PAM unlocker on the real setuid path.** Verify `/usr/local/etc/pam.d/hikari-unlocker` exists and the binary is `4555`, then lock/unlock. Never runtime-verified.
   * **R7-c. Phase 50 touch/gesture checks.** Tap-to-focus, drag-to-move/resize, a configured 3-finger swipe, pinch passthrough, multi-output touch confinement.
   * **R7-d. Phase 40 multi-window guards.** Open/close/resize many windows across sheets.

   **R8. [RESOLVED -- user decision, Phase 87: the config is the target style; reformatting the tree is DEFERRED] `clang-format` / TC-FORMAT-01.**

   `.clang-format` was fixed in Phase 68 (`Language: Cpp`) so it now loads -- but the configured style (8-wide tabs, Allman) **does not describe this tree** (2-space, K&R-ish). Running it would reformat every file and destroy `git blame`.
   *Three options:* (i) rewrite `.clang-format` to describe the existing style and only then enforce; (ii) leave it unenforced and document that; (iii) reformat wholesale in one isolated commit. **Recommend (i).** No code change until chosen.

   **R9. Small hygiene. ~1 h total, batchable.**

   * `hikari_command_execute()`'s blocking `waitpid` (`command.c`) -- align with the `WNOHANG` pattern used in `lock_mode.c`/`bar.c`. Not believed to stall in practice (TODOS P3, Finding 6a).
   * Verify whether the "~14 open PR #1 review threads" recorded in earlier briefings still exist; close or drop the item.
   * Duplicate `wl_list_init(&server->outputs)` in `server_init()` (noted Phase 52, never actioned).

   10. Phase 54 view-teardown hardening -- STILL OPEN, and MISSING from the original R1-R9 list.**

   Surfaced by the R1 sweep: `PLANS.md` item -5 has been "awaiting approval" since Phase 54 and never resolved, and its sub-items are scattered across the Phase 55, 61 and 54 sections of `TODOS.md` as four separate live entries. **The Phase 84 plan omitted it entirely** -- which is precisely the failure R1 exists to catch.

   Its W2 (initialise all seven `wl_list` links in `hikari_view_init()`) **is already done** -- Phase 56 implemented it, verified 2026-08-22. What remains:

   * **R10-a.** Document the view ownership graph in `BLUEPRINT.md` -- seven links, six owning pointers, five teardown entry points. Docs only, zero risk. Also satisfies the Phase 55 "View Visibility State" section item.
   * **R10-b.** `hikari_view_check_invariants()` / `view_assert_visible_consistent()` -- the six-way consistency checker. **Needs the Phase 54 decision that was never taken:** debug-only, or always-on per the Phase 61 policy? Note the Phase 61 finding that `-DNDEBUG` makes every `assert()` dead in shipped binaries, which argues for always-on `wlr_log` -- and note that the layer trees (Phase 73) removed one of the six representations, so the checker is smaller than when it was specified.
   * **R10-c.** Headless smoke test for teardown sequences under `MALLOC_CONF=junk:true`, wired to a `make` target. **Caveat recorded in Phase 67:** the planned client binds `zwlr_virtual_pointer_v1`, so it must be written against the per-device mapping fixed there, not the cursor-wide call it replaced.

   *Relationship to R5:* R10-b and R5 (dead asserts) overlap -- both concern what `view.c` checks at runtime and how. **Decide R10-b first**, since it sets the policy R5 would then apply.

   **R11. `XDG_RUNTIME_DIR` on ZFS -- verified still true, administrative not code.**

   Verified 2026-08-22: the path is `/var/run/xdg/orpheus497` (set by `pam_xdg`), **not** the `/var/run/user/1001` recorded in older entries, and `df -T` reports **zfs**. `/tmp` *is* correctly `tmpfs`, so the README procedure was applied -- but `XDG_RUNTIME_DIR` does not point at it.

   `posix_fallocate()` fails on ZFS, so clients placing `wl_shm` pools there fail; `linux-dmabuf` (Phase 33) spares GPU clients, which is why this is survivable rather than fatal, and it is consistent with the 24 `firefox.*.core` files sitting in `/var/coredumps`. **Compositor-side it is a non-issue** -- wlroots uses anonymous POSIX SHM (BLUEPRINT section 13, FB-1).

   *No code change.* The fix is administrative: point `pam_xdg` at a tmpfs, or mount one at that path. Documented rather than implemented, since it is the user's system configuration.

   ---

   **Deliberately NOT planned, with reasons:**
   * **OBS ScreenCast** -- downstream of this project; every compositor-side requirement is verified working (Phase 81). portal-wlr is the adopted backend. `grim` remains the control that isolates compositor from client.
   * **libdrm** -- declined Phase 82; it was never a necessity.
   * **Configurable clock offset** -- declined Phase 82; current placement confirmed fine.
   * **Pinning a DRM device** -- explicitly rejected Phase 83; would hard-code a choice the stack is making correctly.
   * **`WITH_EXT_IMAGE_CAPTURE`** -- stays opt-in until a client can use it without black frames.

   **Suggested order:** R1 (done) -> R7-a -> (R4 if needed) -> R6 -> R10-a -> R2 -> R10-b -> R3 -> R5 -> R8/R9. R11 is administrative and independent.
   Rationale: R1 makes every later priority trustworthy; R7-a is two minutes and may close R4 outright; R6 is a safe warm-up; R2 and R3 are the two `view.c` cycles and must stay apart.


-13. **Phase 70 workstream programme -- STATUS as of 2026-08-22 14:46.**

   | WS | State |
   |---|---|
   | **W0** | **LARGELY MOOT (Phase 83).** Runs 1-5 discriminated the eDP-1 failure, now closed as stale. **Only W0-6 is still worth running** (~2 min) -- it gates F4/P2-14. |
   | **W1** | Delivered (Phase 72) -- platform capability layer, buffer consolidation, FB-8. |
   | **W2** | Delivered (Phase 73), **confirmed on hardware** -- scene layer trees; F1 and F2 fixed. |
   | **W3** | Delivered (Phases 74-76), **confirmed on hardware** -- capture + blur. |
   | **W4** | Delivered (Phases 74-77), **confirmed on hardware** -- backdrop, clock, power-aware blanking. |
   | **W5** | Delivered (Phase 71) -- F3, F5. **F4 still held on W0-6.** |
   | **W6** | Delivered (Phase 71) -- C1, C2, C3. |
   | **W7a** | Delivered (Phases 78-80), **confirmed on hardware** -- portal fix + capture protocols. `ext-image-copy-capture` made opt-in in Phase 80 after it was proven to cause black frames. |
   | **W7b** | **NOT STARTED, needs approval.** `ext-foreign-toplevel-list-v1`. Not required for screen sharing; serves taskbars and future window-selection. Six touch points in `src/view.c`, so it wants its own build cycle. |
   | **W8** | Delivered (Phase 78), **confirmed on hardware** -- XWayland renders content, managed and override-redirect. |

   **Also outstanding, each needing approval:**
   * The **`forced` flag removal** (W2 step 3; declared deviation in Phase 73). 15 sites, several provably unreachable, in `src/view.c`. Pure cleanup -- F1/F2 are already fixed by the layer trees.
   *(Resolved 2026-08-22, Phase 82: man page now documents the `lock` block and two stale entries were corrected; **libdrm declined** -- it was never a necessity, the device path already identifies the GPU; **clock offset left fixed** at the user's confirmation.)*

   **Left open deliberately (Phase 81):** OBS ScreenCast renders black although the compositor side is verified working end to end. portal-wlr is the adopted backend; the residual failure is in the portal-wlr -> PipeWire -> OBS path, not this project's code. `grim` is the control that isolates the two halves.

-12. **Phase 70: Lock-Screen Overhaul (Option B) + FreeBSD Native-Compatibility Track -- APPROVED IN PRINCIPLE, no step yet approved for execution.** Derives from the Phase 70 investigation; see DECISIONS_LOG Phase 70 for the findings (F1-F5, C1-C3, N1-N5), the four architectural decisions (D1-D4) and the user's four rulings (Q1-Q4). Nine workstreams.

   **Sequencing.** `W0` is user-run and gates nothing but re-ranks several items. `W5`/`W6` are small, independent and land any time. `W1 -> W2 -> W3 -> W4` is the lock-screen spine. `W7` and `W8` both depend on `W2`; **`W8` must not precede `W2`** or it widens the F1 security hole (XWayland content is invisible today, so fixing it exposes more).

   ```
   W0 ────────────────────────────────────► (re-ranks W5-F4; may close FB-3/FB-4)
   W1 ──┬── W2 ──┬── W3 ── W4    ← lock screen
        │        ├── W7
        │        └── W8          ← must not precede W2
        └── W5, W6               ← independent
   ```

   **Recommended order:** W0 (user) -> W5 + W6 -> W1 -> W2 -> W3 -> W4 -> W7 -> W8. Rationale: W5/W6 are small and independently verifiable, giving one clean build/test cycle before the two large refactors. Per the Q1 ruling there is **no interim F1 patch**, which makes W2 the highest-priority code workstream.

   ---

   **W0. FreeBSD/GPU diagnostic matrix -- USER-RUN, ~30 min, read-only.** Each run from a TTY with `HIKARI_LOG=/tmp/hikari-$N.log` (Phase 68's capture, now that Phase 69 made its exit status trustworthy). Full rationale in DECISIONS_LOG Phase 70 §B1.

   | # | Command | Tests | If it fixes eDP-1 |
   |---|---|---|---|
   | 1 | `WLR_DRM_DEVICES=/dev/dri/card0 start-hikari` | **H0 multi-GPU (new, prime suspect)** | Root cause found; fix is device pinning + docs |
   | 2 | `WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128 start-hikari` | render-node split | Renderer was on the wrong GPU |
   | 3 | `WLR_DRM_NO_MODIFIERS=1 start-hikari` | H2 `IN_FORMATS` | Modifier negotiation; i915 linear fallback |
   | 4 | `WLR_RENDERER=pixman WLR_RENDERER_ALLOW_SOFTWARE=1 start-hikari` | H1 Mesa/GBM | GBM/EGL layer at fault, not KMS |
   | 5 | `WLR_DRM_NO_ATOMIC=1 start-hikari` | atomic KMS | drm-kmod atomic path |
   | 6 | Lock, wait 4 min, press a key | **F4 / P2-14** | Screen returns => F4 is a non-issue |

   Also one line each: `sysctl kern.vt.machine_terminal`, `pkg info -x mesa drm-kmod`, `stat -f '%T' "$XDG_RUNTIME_DIR"`. The agent cannot run these -- the sandbox reports a Linux `uname` with Red Hat GCC while the host is FreeBSD 15.1-RELEASE with clang, and it cannot build. **Run 1 alone likely settles a blocker open since Phase 19.**

   **W1. Platform capability layer + buffer consolidation.** New: `include/hikari/platform.h`, `src/platform.c`, `include/hikari/buffer.h`, `src/buffer.c`. Modified: `src/output.c`, `src/server.c`, `Makefile` (+2 objects).
   1. Move `hikari_argb8888_buffer` verbatim from `src/server.c:2328-2400` into `buffer.c` as `hikari_buffer_create_argb8888()`. Keep `hikari_server_create_argb8888_buffer` as a thin wrapper for one release, then retire it.
   2. Delete `hikari_background_buffer` (`src/output.c:29-68`) -- byte-for-byte the same design -- and route `src/output.c:199-222` through the shared helper.
   3. `hikari_platform_probe()` per D3: renderer name, `render_buffer_caps`, DRM fd/device path, multi-GPU flag, `XDG_RUNTIME_DIR` fs type + `posix_fallocate` probe, dmabuf status. One `wlr_log(WLR_INFO)` block at startup.
   4. Settle **FB-6** while in the area: `WITH_POSIX_C_SOURCE=YES` currently yields 3 implicit-declaration warnings (2 security-relevant, incl. `explicit_bzero` at `src/lock_mode.c:70`) and fails `-Werror` (TODOS P3). Either feature-test-detect or retire the flag.
   5. Fold in **FB-8**: `.ifdef` tests definedness, not value, so `make WITH_XWAYLAND=NO` still enables XWayland. Use `.if defined(X) && ${X} != "NO"`.

   *Acceptance:* one `wlr_buffer_impl` in the tree; startup log names renderer + caps + DRM device; background and indicators render unchanged.

   **W2. Scene layer trees (D1) -- fixes F1 and F2. HIGHEST-PRIORITY CODE WORKSTREAM.** Modified: `include/hikari/server.h` (+`struct hikari_scene_layers`), `src/server.c` `setup_scene_graph` (`:953`), and the 8 root-attachment sites in `output.c`, `bar.c`, `indicator_bar.c`, `lock_indicator.c`, `layer_shell.c`, `xdg_view.c`, `xwayland_view.c`, plus `src/lock_mode.c` and `src/view.c`.
   1. Create the six trees in `setup_scene_graph`, in order, each guarded with the Phase 67/68 fatal-exit pattern.
   2. Repoint all 8 sites. **Delete** the ad-hoc ordering calls at `layer_shell.c:241-243`, `:599-601`, `bar.c:870`, `lock_indicator.c:229`, `output.c:228`, `:254`. Layer-shell maps to its four trees by `layer->layer`.
   3. Rewrite `override_visibility()`/`reset_visibility()` (`lock_mode.c:749`, `:616`) as tree toggles plus `wlr_scene_node_reparent` of public views into `layers.lock`. **Delete the `forced` flag entirely** -- with trees it has no remaining job, which removes a visibility representation rather than adding one and closes the Phase 55 finding for the lock path.
   4. Fix the false comment at `view.c:1041-1046` and make the lock-map branch reparent into `layers.lock` (**F2**).

   *Risk:* `hikari_view_show/hide` (`view.c:1144`, `:1175`) still drive per-node enable for sheet switching -- correct, and stays. Residual risk is a view left enabled inside a disabled tree; mitigated because a disabled ancestor wins in `wlr_scene`. Watch the assert-heavy visibility code (see item -11).
   *Acceptance:* lock with a terminal showing text -- only backdrop and indicator visible; `xterm` mapped while locked stays invisible; waybar submenus render above windows and below the lock layer.

   **W3. Screen capture + blur (D2, Option B). Per the Q3 ruling: CPU baseline first, GPU second.** New: `include/hikari/screen_capture.h`, `src/screen_capture.c`, `include/hikari/blur.h`, `src/blur.c`.

   *Capture*, per output, while still enabled:
   ```c
   sc = wlr_swapchain_create(server->allocator, w, h, &fmt);   /* XRGB8888 */
   wlr_output_state_init(&state);
   wlr_scene_output_build_state(output->scene_output, &state,
       &(struct wlr_scene_output_state_options){ .swapchain = sc });
   /* state.buffer holds the composited frame -- lock it, never commit */
   ```

   > **SPIKE, flagged rather than buried.** wlroots 0.20.2 exposes `wlr_renderer_get_texture_formats()` but **no** public render-format query, so the swapchain's `struct wlr_drm_format` must be chosen without one. Escalation ladder, each rung logged: implicit-modifier `XRGB8888` (`len=0`) -> explicit `DRM_FORMAT_MOD_LINEAR` -> `ARGB8888` -> abort capture and fall back to a solid `clear` backdrop. This is the one item that cannot be de-risked without building.

   *Blur -- CPU backend (baseline, ships first):* `wlr_texture_from_buffer` -> `wlr_texture_read_pixels` into `malloc`'d ARGB8888 -> three separable box-blur passes (standard Gaussian approximation) -> `hikari_buffer_create_argb8888`. `wlr_texture_read_pixels` is `glReadPixels`-backed, so this avoids `gbm_bo_map` (FB-2).

   *Blur -- GPU backend (second, same interface):* N-level bilinear downsample then upsample via `wlr_renderer_begin_buffer_pass` + `wlr_render_pass_add_texture` with `WLR_SCALE_FILTER_BILINEAR` and `blend_mode = WLR_RENDER_BLEND_MODE_NONE`. Dual-Kawase approximation; **no custom shaders**, no CPU mapping. Selected by the D2 probe, with the CPU result as the reference to diff against.

   *Config:* `ui { lock { blur = { radius = 12; passes = 3 } } }`; `blur = false` disables.
   *Acceptance:* locking yields a recognisably blurred still of the desktop on every output; `WLR_RENDERER=pixman` produces the same image via the CPU path; capture failure degrades to solid `clear` with a logged reason, never a black screen.

   **W4. Lock-screen composition (D4).** New: `src/lock_clock.c`. Modified: `lock_mode.c`, `lock_indicator.c`, `configuration.c`, `etc/hikari/hikari.conf`, `share/man/man1/hikari.md`.
   1. Backdrop node: blurred buffer -> `wlr_scene_buffer_create(layers.lock, buf)`, positioned at output origin, `set_dest_size` to logical size, lowered within `layers.lock`.
   2. Clock: cairo + Pango, same idiom as `bar.c` -- **render at `wlr_output->scale` and `set_dest_size` down**, per the HiDPI lesson at `bar.c:679`. Timer ticks on the minute boundary, not per second. Config: `format`, `date-format`, `font`, `color`, `position`.
   3. Indicator repositioned below the clock; `get_geometry` (`lock_indicator.c:183`) gains a vertical offset.
   4. **Q2 ruling -- power-aware blank timeout.** Replace the hardcoded `1000` at `lock_mode.c:827`. `ui { lock { blank-timeout-ac = 180; blank-timeout-battery = 60 } }`, seconds, `0` = never. Read `hw.acpi.acline` via `sysctlbyname()` (idiom already at `topbar.c:328-332`) **at arm time, never cached**, so unplugging mid-lock takes effect; the timer is already re-armed on every keypress (`lock_mode.c:600`), so no `devd` listener is needed. A machine with no battery falls back to the AC value, not to `0`. `<sys/sysctl.h>` needs the same non-FreeBSD guard the file applies to `explicit_bzero` at `lock_mode.c:16-18` (**FB-9**).
   5. Output hotplug while locked: capture+blur the new output in `hikari_output_init` (`output.c:580-583`), else solid `clear`.

   **W5. Lock-mode correctness -- independent, ~2 h.**
   * **F3:** NULL-guard `mode->disable_outputs` between `lock_mode.c:819` and `:827`, matching the existing guard at `:599`.
   * **F4:** *conditional on W0 run 6.* If the screen stays dark, add `wlr_output_state_set_mode()` to `hikari_output_enable` (`output.c:334`), mirroring `hikari_output_init` (`:553-556`). Closes TODOS **P2-14**.
   * **F5:** `write_success(false)` before the fatal return at `hikari_unlocker.c:143-146`, matching `:88`.

   **W6. Clipboard -- independent, ~2 h.**
   * **C1:** `wlr_xwayland_set_seat(server->xwayland, server->seat)` immediately after `setup_selection(server)` (`server.c:1541`). Ordering matters -- the seat does not exist at `setup_xwayland` (`:1498`). Add a `seat_destroy` guard for hot-reload. Restores X11 <-> Wayland copy/paste, broken since upstream.
   * **C2:** add `wlr_ext_data_control_manager_v1_create(display, 1)` alongside the `wlr-` variant; both coexist by design.
   * **C3:** guard the discarded return of `server.c:900` per the Phase 67/68 policy.

   **W7. Screencopy modernisation -- depends on W2.**
   1. Add `wlr_ext_output_image_capture_source_manager_v1_create` + `wlr_ext_image_copy_capture_manager_v1_create` beside the existing screencopy global. Keep screencopy for `grim` / `xdg-desktop-portal-wlr`.
   2. **Portal fix:** change `XDG_CURRENT_DESKTOP` (`start-hikari.sh:26`) to `"Hikari Sakura:wlroots"` -- colon-separated, so hikari keeps its identity *and* `xdg-desktop-portal-wlr`'s `UseIn=wlroots` matches. Mirror in `share/wayland-sessions/hikari.desktop`'s `DesktopNames`.
   3. Add `ext_foreign_toplevel_list_v1` so screen-share window pickers have something to enumerate.

   **W8. XWayland scene integration (N5) -- depends on W2, MUST NOT PRECEDE IT.** Supersedes and subsumes item -9 below, which is now **confirmed fact rather than a hypothesis**: `xwayland_unmanaged_view.c` contains no `wlr_scene` reference at all, and `xwayland_view.c:537` attaches only border and indicator frame. Add `wlr_scene_surface_create`/`wlr_scene_subsurface_tree_create` under the existing `scene_tree` at map time (not init -- the `wlr_surface` does not exist until `associate` fires), and give `xwayland_unmanaged_view.c` a tree in `layers.views` (or `layers.overlay` for override-redirect). Tear down on unmap so a dissociate/re-associate cycle neither leaks nor double-attaches.

-10. **[CLOSED -- Phase 84 sweep, 2026-08-22] Phase 68 build + runtime verification.** Built and running for many cycles since; XWayland starts and renders. Original text retained below for the record.

   Phases 61-68 are all implemented and none have been compiled. The agent cannot build (`.o` files are `root:wheel`) and cannot run the compositor, so this one cycle has to answer every open question at once. Steps 1-3 are sequenced so a crash at any point still yields usable evidence.

   1. **Build.** `rm -f *.o && make DEBUG=YES && sudo make DEBUG=YES install`. Verified clean under the corrected clang check (0 warnings across 60 files), so `-Werror` will not block. `DEBUG=YES` gives `-g -O0` for readable backtraces and re-enables all 234 asserts -- treat any that fire as a real invariant violation the release build silently ignores. **Do not use `ASAN=YES`**: Makefile:90-96 documents that ASan intercepts wlroots/GBM `mmap` and dies before the DRM backend initialises.
   2. **Run with capture.** `export HIKARI_LOG=/tmp/hikari-$(date +%s).log`, then start the session normally. Optionally `WLR_DEBUG_LOG=1` for verbose wlroots output; `WAYLAND_DEBUG=1` only for a targeted protocol trace, since it is enormous.
   3. **XWayland verification -- the primary question.** `xterm`, then `xeyes`. Confirms or refutes the Phase 68 B diagnosis. If a window appears, the deadlock is real and fixed. If a *bordered but empty* window appears, the deadlock is fixed **and** the Phase 64 render-gap finding is confirmed in one shot.
   4. **On any crash:** `gdb /usr/local/bin/hikari /var/coredumps/hikari.<pid>.1001.core -ex 'bt full' -ex 'info registers' -ex 'thread apply all bt' -ex quit`. The core infrastructure is confirmed working (`kern.corefile`, `ulimit -c unlimited`, 3 existing cores).
   5. **Regression sweep for Phase 67/68 C:** the guarded paths are all allocation-failure branches and will not be exercised in a healthy run -- absence of a crash is the expected result, not proof the guards work. What *does* need exercising is the virtual-pointer per-device mapping (Phase 67 Finding 2): if any `zwlr_virtual_pointer_v1` client is available, confirm the physical mouse is no longer confined to the suggested output.

   **Blocked behind this cycle:** the Phase 64 XWayland content gap (untestable until XWayland starts), the Phase 50 touch/gesture runtime checks, and the Phase 40 multi-window guards.

-11. **[SUPERSEDED by R5 -- count corrected to 255] Dead-assert remediation.** Still not started; scoping decision required. See item -14 R5.

   234 `assert()` calls across 32 files (`view.c`: 101) are compiled out by `-DNDEBUG` in every shipped binary. Phase 61 approved always-on `wlr_log(WLR_ERROR)` + safe bail as the replacement policy, but it has only reached a handful of sites; Phase 68 converted `server->seat` because it was guarding a live allocation. The remainder need triage into three buckets before any code changes: (a) guarding an allocation or external return value -- convert, (b) documenting an internal invariant that cannot fail given the current call graph -- leave, or (c) genuinely unreachable -- delete. Bucket (a) is the only one with a live crash risk. Do not sweep mechanically; Phase 47 already showed one `assert` (`add_keyboard`) to be a sound invariant that would have been wrong to rewrite defensively.

-9. **[CLOSED -- FIXED in Phase 78, verified 2026-08-22] XWayland surface content.** Both managed and override-redirect surfaces now attach via `wlr_scene_subsurface_tree_create()`.

   `src/xwayland_view.c` creates `xwayland_view->scene_tree` and attaches only `hikari_border_init()` and `hikari_indicator_frame_init()` to it. No `wlr_scene_subsurface_tree_create()` (or equivalent) is called for `xwayland_surface->surface` anywhere in the file, so a managed X11 window should render its border and indicator frame with **no content inside**.

   1. **Confirm before implementing.** Launch a genuinely X11-only client (`xterm`, `xeyes`, `xclock`) and check whether an empty bordered rectangle appears. Everything below is conditional on that.
   2. Attach the surface tree at map time, not init time — the `wlr_surface` does not exist until `associate` fires, which is why this cannot mirror `hikari_xdg_view_init`'s init-time `wlr_scene_xdg_surface_create()` call.
   3. Store the returned tree on `struct hikari_xwayland_view` and tear it down on unmap, so a dissociate/re-associate cycle (X11 surface recreation) does not leak or double-attach.
   4. Check the same question for `xwayland_unmanaged_view.c` — override-redirect windows have no scene node either, and hit-testing already walks them via `output->unmanaged_xwayland_views`.
   5. Re-verify `surface_at()` in both XWayland files once content renders. Unlike xdg, XWayland surfaces carry no window-geometry offset, so the Phase 64 cursor correction should **not** be copied there — but that needs confirming against real rendering rather than assumed.

   **Ordering note:** this is the third "was never wired up" gap found in a row (popup scene nodes, layer-popup scene nodes, now XWayland content). Step 4 below — the headless smoke test — is what would have caught all three before release, and its value rises with each one found by hand.

-8. **[SUPERSEDED by R10] Phase 61 Steps 3 and 4 — approved, not yet started.** Steps 1 (crash fix + Finding A) and 2 (Finding B) are implemented; see DECISIONS_LOG Phase 61. These two remain.

   **STEP 3 — Always-on invariant checks (user decision recorded: always-on `wlr_log(WLR_ERROR)` + safe bail, NOT `assert()`).**

   The justification is now measured rather than argued: `strings hikari` finds **zero** assert strings because the shipped binary is release `-DNDEBUG`, while `strings libwlroots-0.20.so` finds **280**. Every `assert()` hikari has accumulated over ~50 phases — including every safety net Phases 55/56/57 added — is dead code in the binary the user actually runs. Phase 55 deliberately deferred item 1c on the reasoning that "the user's binary is DEBUG=YES with asserts live"; that premise was false, and the check has been missing ever since.

   1. Add a shared `HIKARI_CHECK(cond, fmt, ...)` primitive: on failure, `wlr_log(WLR_ERROR, ...)` with file/line and return a failure indication the caller can bail on. Never `abort()`, never compiled out.
   2. Phase 55 item 1c — `view_assert_visible_consistent()`: verify all six representations of "this view is visible" agree (hidden flag; `workspace->views`; `hikari_server.visible_views`; `group->visible_views`; the `group->visible_server_groups` aggregate; the scene-node enabled bit). Call at entry and exit of `view_link_visible_at()` / `view_unlink_visible()`, and at entry of `hikari_view_unmap()` / `hikari_view_fini()`.
   3. Phase 54 W3 — `hikari_view_check_invariants(view, expected_lifecycle)`: assert the expected graph shape per lifecycle phase (which of the seven links must be linked vs. empty, which of the six owning pointers must be NULL vs. non-NULL), at all five teardown entry points.
   4. Extend the same treatment to the unmanaged XWayland view: a check that `unmanaged_output_views` is empty exactly when `hidden`, and that `workspace != NULL` implies the view's output still exists.

   **STEP 4 — Headless smoke test (carries over Phase 54 W4 and Phase 55 Step 4).**

   5. Driver client binding `zwlr_virtual_pointer_v1` (already enabled, `Makefile:141`) against a nested instance (`WLR_BACKENDS=headless`).
   6. Run under `MALLOC_CONF=junk:true,abort_conf:true`.
   7. **New mandatory case, from Phase 61:** deactivate/reactivate the session with a live override-redirect window open, exercising the output-destroy path that both the NULL deref and Finding A sat on. Plus the Phase 55 cases: close focused vs. unfocused; close last view of a group; close while tiled; close with popup open; lock-mode forced view; sheet switch with mixed hidden/visible; `view-lower` then close.
   8. Wire to a `make` target, replacing the `test.mk` stub.

   **Sequencing:** Step 4 before Step 3 would let the checks be demonstrated firing on the pre-fix tree, but Step 3 is cheap and independently valuable. Either order.

-7.5. **Phase 61 follow-ups, not yet scoped.**
   * **Cursor pointer offset (user-reported 2026-08-21 14:51, uninvestigated).** Pointer renders/hit-tests offset from its true position. First place to look is the interaction between the top bar's `usable_area` reservation and cursor layout coordinates.
   * **Orphaned `hikari-topbar` helpers.** Four alive at 14:56 from crashed sessions. `bar.c` forks them and nothing reaps them when the compositor dies; they survive as session leaders. Also observed in Phase 53.
   * **`XDG_RUNTIME_DIR` on ZFS.** Reclassify from cosmetic backlog to live crash amplifier: `posix_fallocate()` is unsupported there, so `wl_shm` clients fail their buffer allocation and disconnect abruptly, driving exactly the teardown storms that expose the defects above.

-7. **[CLOSED -- implemented in Phase 60] Phase 58 Issue 1 — Top bar: centre lane + real opacity.** Issue 2 of Phase 58 is done (Phase 59). This is the remaining half.

   **Part A — layout (clock/date to centre, WiFi/brightness/volume/battery to right).** Both files must change together; changing only `topbar.c` cannot work, because centre is not representable today.
   1. `include/hikari/bar.h`: replace `bool align_right` on `struct hikari_bar_block` with a three-valued alignment (`enum hikari_bar_align { LEFT, CENTER, RIGHT }`), so the parser can express centre at all.
   2. `src/bar.c` › `parse_line()` (`:252-254`): map the JSON `"align"` string to that enum — currently anything not exactly `"right"` silently becomes left, which is why the unaligned spacer and the WiFi group ended up in the left lane.
   3. `src/bar.c` › `hikari_bar_refresh()`: extend the existing measure pre-pass (`:710-720`) to total the **centre** run as well as the right run, then add a third origin `center_x = (width - center_width) / 2` alongside `left_x`/`right_x` (`:722-723`), and dispatch on the enum in the layout loop (`:742-749`).
   4. `src/topbar.c`: mark the clock `"align":"center"` (`:550-552`); mark network, backlight, volume and battery `"align":"right"` (`:528-547`); **delete the 400px spacer** (`:524`) — it becomes both unnecessary and harmful, since it would still pad the left lane.
   5. **Ordering caveat:** the right lane lays out in emission order flowing rightward from `right_x` (`src/bar.c:743-745`), so those four blocks must be emitted in the desired left-to-right visual order, not reversed.

   **Part B — opacity.** Three independent hardcodes, and the deepest one is global to the whole configuration system. **This is where a user decision is needed** (see below).
   6. `include/hikari/color.h` › `hikari_color_convert()` sets `dst[3] = 1.0` unconditionally, and config colours are 6-digit `0xRRGGBB`. **No configured colour anywhere in hikari can currently be translucent.**
   7. `src/bar.c:703` passes a literal `1.0` rather than `bg[3]`, discarding any alpha even if it existed.
   8. The bar has no colour of its own — it reuses `hikari_configuration->clear` (default `0x282C34`, `src/configuration.c:1878`), which is semantically the *output background* colour and is the slate the user is seeing.
   9. Confirmed achievable: `CAIRO_FORMAT_ARGB32` (`src/bar.c:688`) → `DRM_FORMAT_ARGB8888` (`src/server.c:2252`), both premultiplied, so they agree with no conversion. Block text is drawn opaque, which is the desired result over a translucent background.

   **Decision required before implementing Part B — three options, in increasing scope:**
   * **(i) Minimal:** hardcode a bar alpha constant in `src/bar.c` (e.g. `0.85`) and keep using `clear` for the RGB. One-line change, no config surface, not user-tunable, and the colour stays the slate.
   * **(ii) Scoped (recommended):** add a dedicated `bar` colour + `bar_opacity` (0.0-1.0) to `struct hikari_configuration` and the UCL parser, used only by the top bar. Leaves `hikari_color_convert()` and every other colour untouched, so there is no risk to borders/indicators.
   * **(iii) General:** extend `hikari_color_convert()` and the config parser to accept 8-digit `0xRRGGBBAA` everywhere. Most flexible and most invasive — it changes the alpha of *every* colour path in the compositor (borders, indicator bars, frames, output background), so it needs a careful audit of each consumer and is the only option that could regress unrelated visuals.

-6. **[CLOSED -- implemented in Phase 56] Phase 55 REFACTOR: Single-Writer Visibility Transitions.**

   **The flaw being remediated:** the fact "this view is visible" is stored in six places (hidden flag; `workspace->views`; `hikari_server.visible_views`; `group->visible_views`; `group->visible_server_groups` linked into `hikari_server.visible_groups`; scene-node enabled bit). No single function owns the transition — entry is split across `increase_group_visiblity()` + `place_visibly_above()`, exit is the single `hide()`, and `hikari_view_lower()` re-implements the whole linkage a third time inline. Group lifetime depends on an unwritten, unasserted invariant tying #4 and #5 together, and `hikari_view_unmap()` contains a branch that violates it.

   **The remediation, in one sentence:** make exactly two functions the *only* code in the tree permitted to mutate visibility linkage, and make every existing caller route through them.

   **This is explicitly NOT the Phase 44 DOD/SoA rewrite** (that decision stands, unrevisited). Nothing about allocation strategy changes; no object pools; no struct-of-arrays. Only *who writes the state* changes.

   ---
   **STEP 0 — Prerequisites (do first, independently revertible, no behavioural change).**

   0a. `src/view.c` › `hikari_view_init()` (l.417-462): `wl_list_init()` all seven links (`output_views`, `workspace_views`, `sheet_views`, `group_views`, `visible_group_views`, `visible_server_views`, `children`), not just `children`. Makes every later `wl_list_remove` structurally safe instead of guard-dependent.
   0b. `src/view.c` › `hikari_view_configure()` (l.2080-2081): the two now-redundant `wl_list_init` calls become no-ops; leave with a comment or delete. Decide while editing.
   0c. `src/group.c` › `hikari_group_fini()` (l.24-29): add `wl_list_remove(&group->visible_server_groups);`. Defensive — after the refactor this should always already be unlinked, but it converts a silent UAF into a no-op if it ever is not.
   0d. `src/view.c` › `decrease_group_visibility()` (l.186-196): add `wl_list_init(&view->visible_group_views);` after the remove, matching the `remove`+`init` convention `hide()` already uses for the other two links.

   ---
   **STEP 1 — Introduce the two authoritative transitions (`src/view.c`, new static functions, placed immediately after `decrease_group_visibility` at ~l.197).**

   1a. **`static void view_enter_visible(struct hikari_view *view, struct hikari_workspace *workspace)`** — performs, in this fixed order, the complete entry transition:
       1. `assert(hikari_view_is_hidden(view))` (precondition: currently not visible)
       2. group aggregate: `if (wl_list_empty(&view->group->visible_views)) wl_list_insert(&hikari_server.visible_groups, &view->group->visible_server_groups);`
       3. `wl_list_remove` + `wl_list_insert` for `visible_group_views` → `group->visible_views`
       4. same for `visible_server_views` → `hikari_server.visible_views`
       5. same for `workspace_views` → `workspace->views`
       6. `hikari_view_unset_hidden(view)`
       7. `if (view->scene_node != NULL) wlr_scene_node_set_enabled(view->scene_node, true);`
       Absorbs today's `increase_group_visiblity()` (step 2) and the visibility half of `place_visibly_above()` (steps 3-5).

   1b. **`static void view_exit_visible(struct hikari_view *view)`** — the exact inverse, in reverse order:
       1. `if (view->scene_node != NULL) wlr_scene_node_set_enabled(view->scene_node, false);`
       2. `hikari_view_set_hidden(view)`
       3. `wl_list_remove` + `wl_list_init` for `workspace_views`, `visible_server_views`, `visible_group_views`
       4. group aggregate: `if (wl_list_empty(&view->group->visible_views)) wl_list_remove(&view->group->visible_server_groups);` then `wl_list_init` that link
       Absorbs today's `hide()` and `decrease_group_visibility()` in full.

   1c. **`static void view_assert_visible_consistent(struct hikari_view *view)`** — debug-only (`#ifndef NDEBUG`) checker asserting all six representations agree: hidden flag vs. `wl_list_empty` on each of the three view links vs. group-aggregate linkage. Called at the top and bottom of both transitions above, and at the entry of `hikari_view_unmap`/`hikari_view_fini`. **This is what makes a future incorrect edit fail at the mistake instead of corrupting the heap.**

   ---
   **STEP 2 — Rewire every existing caller (`src/view.c` unless noted). Each is a small, individually testable edit.**

   | # | Function (line) | Change |
   |---|---|---|
   | 2a | `increase_group_visiblity()` (l.170) | **Delete.** Body absorbed into `view_enter_visible`. |
   | 2b | `decrease_group_visibility()` (l.186) | **Delete.** Body absorbed into `view_exit_visible`. |
   | 2c | `hide()` (l.198) | **Delete.** Replaced by `view_exit_visible`. |
   | 2d | `place_visibly_above()` (l.69) | Reduce to *stacking only* — it must no longer touch visibility linkage. Its remaining job (ordering within already-linked lists) merges into `move_to_top()`; see 2e. |
   | 2e | `move_to_top()` (l.53) | Becomes the single stacking-order function: reorders `sheet_views`, `group_views`, `output_views` **and** (when visible) `visible_group_views`, `visible_server_views`, `workspace_views` to front. Stacking is now cleanly separate from visibility. |
   | 2f | `raise_view()` (l.90) | Now just `move_to_top(view)`. Precondition `assert(!hikari_view_is_hidden(view) \|\| hikari_view_is_forced(view))`. |
   | 2g | `hikari_view_show()` (l.1039) | Body becomes `view_enter_visible(view, view->sheet->workspace); move_to_top(view); hikari_view_damage_whole(view);`. |
   | 2h | `hikari_view_hide()` (l.1066) | Body becomes `clear_focus(view); view_exit_visible(view); hikari_indicator_frame_hide(&view->indicator_frame); hikari_view_damage_whole(view);` — note `clear_focus` **must stay before** the exit (documented ordering constraint). |
   | 2i | `hikari_view_lower()` (l.1105-1138) | **Rewrite.** Delete the seven inline remove/insert pairs; call a new `move_to_bottom(view)` mirroring `move_to_top`. Eliminates the third hand-written copy of the linkage. |
   | 2j | `hikari_view_map()` (l.873-954) | Both branches route through the new functions: normal branch `hikari_view_show(view)` (unchanged call, new body); lock-mode branch `hikari_view_set_forced(view); view_enter_visible(...); move_to_top(view);` — and see 2k. |
   | 2k | `hikari_view_unmap()` (l.979-988) | **Resolve the forced/`!hidden` branch.** Replace the whole `if (forced)` block plus the following `if (!hidden)` block with a single unconditional `if (!hikari_view_is_hidden(view) \|\| hikari_view_is_forced(view)) { view_exit_visible(view); hikari_view_unset_forced(view); hikari_server_cursor_focus(); }`. This removes the branch that sets the flag without performing the transition — the concrete UAF path identified in DECISIONS_LOG Phase 55. |
   | 2l | `hikari_view_migrate()` (l.2003) / `migrate_view()` (l.1989) | `hide(view)` call at l.2018 becomes `view_exit_visible(view)`; the trailing `if (hidden) hikari_view_show(view)` is unchanged in meaning. |
   | 2m | `hikari_view_exchange()` (~l.1680) | Audit only — confirm it reorders via `move_to_top`/tile machinery and never touches visibility links directly. |
   | 2n | `remove_from_group()` (l.226) | Becomes `assert(!hikari_view_is_hidden(view)); view_exit_visible(view); detach_from_group(view);`. |
   | 2o | `detach_from_group()` (l.212) | Add `assert(wl_list_empty(&group->visible_views));` immediately before the free, making the previously-unwritten group-lifetime invariant explicit and checked. |
   | 2p | `hikari_view_fini()` (l.464) | Add `view_assert_visible_consistent(view)` at entry; keep existing asserts. |
   | 2q | `src/group.c` › `hikari_group_hide()` / `hikari_group_show()` | Audit only — they call `hikari_view_hide`/`show`, so they inherit correctness. Confirm no direct link manipulation. |
   | 2r | `src/workspace.c` › `hikari_workspace_clear()` (l.145), `display_sheet()` (l.157) | Audit only — both iterate with `_safe` and call `hikari_view_hide`/`show`. Confirm still correct once those bodies change. |

   ---
   **STEP 3 — Documentation (`BLUEPRINT.md`), mandatory, same commit series.**
   3a. New "View Visibility State" section: the six representations table, the statement that `view_enter_visible`/`view_exit_visible` are the only permitted writers, and the two ordering constraints (`clear_focus` before exit; children teardown before list unlinks).
   3b. Record the group-lifetime invariant now enforced by 2o.

   ---
   **STEP 4 — Verification (this is what has been missing for ~50 phases; carries over Phase 54 W4).**
   4a. Headless smoke test driving open → popup → click → close via `zwlr_virtual_pointer_v1` (already enabled, `Makefile:141`) against a nested instance (`WLR_BACKENDS=headless`, proven working per `hikari.log`).
   4b. Run it under `MALLOC_CONF=junk:true,abort_conf:true` so any surviving UAF faults at the access rather than surfacing later.
   4c. Cases that specifically exercise this refactor: close focused vs. unfocused window; close last view of a group (group free path); close while tiled; close with popup open; lock-mode forced view unmapped while locked; sheet switch with mixed hidden/visible; `view-lower` then close.
   4d. Wire to a `make` target, replacing the `test.mk` stub.

   ---
   **Ordering, risk, and effort.** Step 0 first (independently revertible, no behaviour change, and 0a/0c/0d each close a real latent hole on their own). Step 1 adds code without removing any, so the tree still builds and behaves identically until Step 2 begins. Step 2 is the only behaviour-changing step and each row is a separate reviewable edit — 2k is the one that changes observable behaviour, and is the one most likely to fix the reported crash. Step 3 is docs. Step 4 is what proves it and should ideally be built *before* Step 2 so it can demonstrate the before/after.

   **Confidence statement, stated honestly:** this refactor removes a *class* of defect and one *specific* identified UAF path (2k). It has not been proven to be the crash the user is hitting right now, because no crash output or core dump has yet been captured (see Phase 53 — `/var/coredumps` does not exist, and `start-hikari.sh` does not redirect stderr). Step 4 is what closes that gap. If the captured message turns out to name a different fault, this refactor is still correct and still worth doing, but it would not be the fix — and that should be stated plainly rather than assumed.

-5. **[SUPERSEDED by R10 -- its W2 is DONE (Phase 56, verified 2026-08-22)] Phase 54 View-Teardown Ownership-Graph Hardening.** Addresses the structural problem that view teardown sequences seven `wl_list` links and six owning pointers by hand, from five entry points, with no mechanical verification. Four workstreams:

   **W1 — Document the ownership graph (`BLUEPRINT.md`, docs only, zero regression risk).**
   1. Add a "View Ownership Graph" section: a table of all seven list links (`output_views`, `workspace_views`, `sheet_views`, `group_views`, `visible_group_views`, `visible_server_views`, `children`) — for each, the owning container, the function that links it, the function that unlinks it, and which lifecycle phases it is valid in.
   2. Add the equivalent table for the six owning pointers (`sheet`, `group`, `mark`, `output`, `tile`/`pending_operation.tile`, `decoration`, `maximized_state`).
   3. Add a lifecycle state diagram: `init → configure → map → [show/hide/tile/maximize …] → unmap → fini`, marking at which transition each link/pointer becomes valid and invalid.
   4. Record the two ordering constraints that currently exist only as tribal knowledge: (a) `clear_focus()` must run before `hide()` unlinks the view; (b) child teardown (`children` loop) must run before the view's own list unlinks.

   **W2 — Close the `wl_list_init` gap (~7 lines, mechanical, independently revertible).**
   5. In `hikari_view_init()` (`src/view.c:417-462`), `wl_list_init()` all seven links, not just `children`. Removes the latent arbitrary-write documented in DECISIONS_LOG Phase 54 (four links currently hold `hikari_malloc` garbage between init and map, guarded only by an unwritten two-function-adjacency invariant).
   6. Verify the now-redundant `wl_list_init()` calls in `hikari_view_configure()` (`src/view.c:2080-2081`) are harmless duplicates and either leave with a comment or remove — decide during implementation, not before.
   7. Re-check the three init-failure paths (`xdg_view.c:663`, `:676`, `xwayland_view.c:491`) now tear down safely by construction rather than by NULL-guard coincidence.

   **W3 — Explicit lifecycle state + invariant checker (the automated verification itself).**
   8. Add `enum hikari_view_lifecycle` (`INITIALISED`, `CONFIGURED`, `MAPPED`, `UNMAPPED`, `FINALISED`) as a field on `struct hikari_view`, set at each transition. Purely additive — existing accessors (`hikari_view_is_mapped()` etc.) are left untouched so nothing currently working can regress.
   9. Add `hikari_view_check_invariants(struct hikari_view *, enum hikari_view_lifecycle expected)` asserting the full expected graph shape per phase — which links must be linked vs. empty (`wl_list_empty`), which pointers must be NULL vs. non-NULL — rather than today's three scalar-flag asserts.
   10. Call it at every teardown boundary: entry/exit of `hikari_view_unmap()`, entry of `hikari_view_fini()`, and each of the five teardown entry points.
   11. Decide the release-build policy (**open question — see "Questions for the user" below**): compiled out under `NDEBUG` like existing asserts, or always-on with a `wlr_log(WLR_ERROR)` + safe bail instead of `abort()`.

   **W4 — Headless teardown smoke test + routine sanitiser run (makes W3 actually execute).**
   12. Write a minimal test client that binds `zwlr_virtual_pointer_v1` (already enabled: `Makefile:141` `HAVE_VIRTUAL_INPUT=1`) and drives scripted sequences against a nested hikari (`WLR_BACKENDS=headless`, already proven to initialise per `hikari.log`): open window → open popup → click popup button → close window; plus close-with-popup-still-open, close-while-tiled, close-focused vs. unfocused, multi-window close ordering.
   13. Run the suite under `MALLOC_CONF=junk:true,abort_conf:true` so a use-after-free faults at the access rather than surfacing later as an unrelated abort.
   14. Add an ASan variant where viable — respecting the documented DMA-BUF interception limitation (`Makefile:92-97`); headless/Pixman may avoid the GBM path that makes ASan unusable on the DRM backend. Verify, don't assume.
   15. Wire to a `make test` / `make smoke` target and replace the `test.mk` stub. No CI exists, so the target is the delivery mechanism.

   **Sequencing:** W1→W2 immediately and independently; W3 depends on W1's definitions; W4 parallel to W3. **W4 also directly serves the open Phase 53 crash** — a scripted reproduction under `junk:true` is precisely the empirical step Phase 53 concluded is needed, so doing W4 first would likely resolve both.

   **Questions for the user before implementation:**
   * **Scope/appetite:** all four workstreams, or start with W1+W2 (low risk, closes a real latent bug) and defer W3/W4?
   * **W3 release policy:** should the invariant checker be debug-only (consistent with existing `assert()` usage, zero release cost) or always-on with logged degradation (catches field failures, small runtime cost)? Relevant because Phase 53 showed release-stripped asserts turn a loud abort into a silent corruption.
   * **W4 priority:** do W4 first as the fastest route to the live Phase 53 crash, or keep the stated risk-reduction order?

-4. **[CLOSED -- root-caused in Phases 61-63] Phase 53 Close-Window / Popup-Button Crash.** Re-audited the Phase 42/44/45 popup/subsurface `fini`-dispatch fix against the actual wlroots 0.20 signal-emission order (not assumption) and found it sound — ruled out as the cause of this specific crash. Live-system forensics found the compositor is aborting (SIGABRT, signal 6) 4 times today on the FreeBSD target, 3 of them on the current fully-patched, DEBUG=YES binary (assertions live, not stripped) — not segfaulting. No crash message was captured and no core dump exists. User-run steps, in order (all read-only/diagnostic, no code changes, low risk, a few minutes total):
   1. `sudo mkdir -p /var/coredumps && sudo chmod 1777 /var/coredumps` — so a core dump is captured this time if the abort recurs (currently silently fails: `kern.corefile` points at a directory that doesn't exist).
   2. Reproduce with output captured: run `./start-hikari.sh 2>&1 | tee /tmp/hikari-crash.log` (or redirect however is convenient) from a nested/test session, then close a window or click a popup button as before.
   3. Read the captured message. It will be one of three shapes, each pointing to a different next step:
      * `Assertion failed: (...), function F, file src/X.c, line N.` — a hikari invariant violation; report the exact line back for a targeted fix.
      * `hikari_malloc of N bytes failed` / `hikari_calloc of N x M bytes failed` (from `src/memory.c`) — the fail-fast OOM abort; report the requested size (a huge/garbage size would itself indicate heap corruption upstream, not real memory pressure).
      * A jemalloc diagnostic (heap/redzone corruption, invalid free) — confirms an undiscovered use-after-free or out-of-bounds write elsewhere in the codebase corrupting the heap, surfacing later as an abort during an unrelated allocation (the same "delayed corruption" pattern Phase 42 documented for the bug it fixed); the core dump from step 1 becomes essential here for a `gdb`/`lldb` backtrace to localize the actual corrupting write.
   4. If a core file was produced, inspect with `gdb ./hikari /var/coredumps/hikari.<pid>.1001.core` (`bt full` for the backtrace) — this alone may be enough to pinpoint the exact function without needing the log message at all.
   Per AGENTS.md's Zero Unapproved Action rule, no code changes are proposed yet — there isn't a specific line to fix. The captured message/backtrace from this reproduction determines what Phase 54 actually implements.

-3. **Phase 52 Config Load Failure — RESOLVED.** Root cause: `~/.config/hikari/hikari.conf:160`'s bare-modifier keybinding, silently rejected by `hikari_binding_config_key_parse()`. User fixed their config; the matching hikari-side diagnostic gap was fixed in `src/binding_config.c` (see DECISIONS_LOG Phase 52). Branches A/D/E below were ruled out during the trace (file resolution was fine; launch method was irrelevant; the failure was a pure parse error). Remaining optional, unapproved follow-ups: the identical silent-`else` gap in `hikari_binding_config_button_parse()` (mouse bindings), and the duplicate `wl_list_init(&server->outputs)` in `server_init()`.

-2. **Phase 50 Touch/Gesture Completion (from the Phase 50 investigation — see DECISIONS_LOG for the full technical writeup):** Steps 1-6 implemented (touch coordinate-space fix, per-device output mapping, gesture-to-action config bindings, touch-driven window management, and documentation). Remaining:
   7. **Build + runtime verification (user-run)** — `sudo make clean && sudo make install`, then verify: single-finger tap-to-focus and drag-to-move/resize, a configured 3-finger swipe action, pinch-to-zoom passthrough in a real multitouch-aware client (Evince/Firefox), and — if multi-output touch hardware is available — that touch stays confined to its mapped output.

-1. **Phase 42 Memory/Crash/Error-Handling Remediation (from the deep audit — see DECISIONS_LOG Phase 42 for the full technical writeup):** User approved; findings 1, 2, 3, 4, 7, 8, 9 implemented with code changes across Phases 45-46 (see `PROGRESS.md`/`TODOS.md`/`DECISIONS_LOG.md` Phases 45-46 for the applied fixes — popup/subsurface type confusion, signal-safe shutdown, scoped logging, OOM/fail-fast policy scoping, and the switch/indicator-bar/keymap leak fixes). Finding 5 was investigated in Phase 47 and confirmed sound — no code change needed (the `assert(keyboard_config != NULL)` invariant is structurally guaranteed). Remaining:
   6. **Optional: harden `hikari_command_execute`'s blocking reap** (Finding 6, LOW) — not yet actioned, low-priority backlog item.
   10. **Explicit non-decision:** no SoA/object-pool rewrite. A prior attempt at exactly this (per `PROGRESS.md`'s footnote) was reverted as incompatible with `wlr_scene`'s object-ownership model. If pursued again at all, it must be profiling-driven and scoped to one narrowly-bounded object class at a time (e.g. `hikari_view_subsurface`/`hikari_xdg_popup`/`hikari_tile`), never a wholesale conversion.

0. **Phase 24 Hardening Stream (from deep wiring audit):** ✅ fully completed 2026-08-13 — P0/P1 batch in Phase 25, P2/P3 batch in Phase 26 (see Completed Implementations). Stream closed at 7/7.

1. **Runtime Bring-Up (diagnostics-driven):**
   - User runs the Phase 19 diagnostic matrix (TODOS active list); the DEBUG-build wlroots log discriminates H1/H2/H3 in one pass.
   - Fix the eDP-1 scanout swapchain failure (expected Mesa/GBM/drm-kmod layer, not this tree), then retest TTY bring-up: bindings, cursor movement, client launch, lock/unlock.
   - Resolve tmpfs/ZFS incompatibility for XDG_RUNTIME_DIR (escalated — dmabuf device feedback is unavailable, forcing client wl_shm).

2. **PAM Verification Execution (blocked on runtime bring-up):**
   - Deploy `hikari` and `hikari-unlocker` to a native FreeBSD environment.
   - Identify the canonical absolute installed path for `hikari-unlocker` (e.g. `${PREFIX}/bin/hikari-unlocker`).
   - Verify the binary has trusted package provenance and existing `root:wheel` ownership.
   - Only after verification, apply mode `4555` to the absolute installed path (e.g. `chmod 4555 /usr/local/bin/hikari-unlocker`).
   - Launch compositor and invoke `Meta+L` to trigger the lock screen.
   - Verify input characters render correctly and PAM unlocks upon Enter.

3. **Code Formatting:**
   - Run `clang-format` compliance check (TC-FORMAT-01).

4. **Agent Protocol Enforcement:**
   - Implement the "Ask → Explain → Justify → Wait for Approval" gate for all subsequent changes.
   - Wait for explicit user authorization before executing shell commands or editing codebase files.

*(Removed 2026-08-13: the `wlr_output_effective_resolution()` API-verification item — closed per TODOS completed list; the successful user build proves the symbol exists.)*


