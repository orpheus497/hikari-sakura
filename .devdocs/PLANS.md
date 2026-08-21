# Forward Strategy & Plans

*Last Updated:* 2026-08-22 02:28

## Implementations to be Fully Implemented

-10. **Phase 68 build + runtime verification -- USER-RUN, single cycle, everything else is blocked behind it.**

   Phases 61-68 are all implemented and none have been compiled. The agent cannot build (`.o` files are `root:wheel`) and cannot run the compositor, so this one cycle has to answer every open question at once. Steps 1-3 are sequenced so a crash at any point still yields usable evidence.

   1. **Build.** `rm -f *.o && make DEBUG=YES && sudo make DEBUG=YES install`. Verified clean under the corrected clang check (0 warnings across 60 files), so `-Werror` will not block. `DEBUG=YES` gives `-g -O0` for readable backtraces and re-enables all 234 asserts -- treat any that fire as a real invariant violation the release build silently ignores. **Do not use `ASAN=YES`**: Makefile:90-96 documents that ASan intercepts wlroots/GBM `mmap` and dies before the DRM backend initialises.
   2. **Run with capture.** `export HIKARI_LOG=/tmp/hikari-$(date +%s).log`, then start the session normally. Optionally `WLR_DEBUG_LOG=1` for verbose wlroots output; `WAYLAND_DEBUG=1` only for a targeted protocol trace, since it is enormous.
   3. **XWayland verification -- the primary question.** `xterm`, then `xeyes`. Confirms or refutes the Phase 68 B diagnosis. If a window appears, the deadlock is real and fixed. If a *bordered but empty* window appears, the deadlock is fixed **and** the Phase 64 render-gap finding is confirmed in one shot.
   4. **On any crash:** `gdb /usr/local/bin/hikari /var/coredumps/hikari.<pid>.1001.core -ex 'bt full' -ex 'info registers' -ex 'thread apply all bt' -ex quit`. The core infrastructure is confirmed working (`kern.corefile`, `ulimit -c unlimited`, 3 existing cores).
   5. **Regression sweep for Phase 67/68 C:** the guarded paths are all allocation-failure branches and will not be exercised in a healthy run -- absence of a crash is the expected result, not proof the guards work. What *does* need exercising is the virtual-pointer per-device mapping (Phase 67 Finding 2): if any `zwlr_virtual_pointer_v1` client is available, confirm the physical mouse is no longer confined to the suggested output.

   **Blocked behind this cycle:** the Phase 64 XWayland content gap (untestable until XWayland starts), the Phase 50 touch/gesture runtime checks, and the Phase 40 multi-window guards.

-11. **Dead-assert remediation -- NOT STARTED, needs scoping (Phase 68 finding).**

   234 `assert()` calls across 32 files (`view.c`: 101) are compiled out by `-DNDEBUG` in every shipped binary. Phase 61 approved always-on `wlr_log(WLR_ERROR)` + safe bail as the replacement policy, but it has only reached a handful of sites; Phase 68 converted `server->seat` because it was guarding a live allocation. The remainder need triage into three buckets before any code changes: (a) guarding an allocation or external return value -- convert, (b) documenting an internal invariant that cannot fail given the current call graph -- leave, or (c) genuinely unreachable -- delete. Bucket (a) is the only one with a live crash risk. Do not sweep mechanically; Phase 47 already showed one `assert` (`add_keyboard`) to be a sound invariant that would have been wrong to rewrite defensively.

-9. **XWayland surface content — NOT YET APPROVED, not started (found in Phase 64).**

   `src/xwayland_view.c` creates `xwayland_view->scene_tree` and attaches only `hikari_border_init()` and `hikari_indicator_frame_init()` to it. No `wlr_scene_subsurface_tree_create()` (or equivalent) is called for `xwayland_surface->surface` anywhere in the file, so a managed X11 window should render its border and indicator frame with **no content inside**.

   1. **Confirm before implementing.** Launch a genuinely X11-only client (`xterm`, `xeyes`, `xclock`) and check whether an empty bordered rectangle appears. Everything below is conditional on that.
   2. Attach the surface tree at map time, not init time — the `wlr_surface` does not exist until `associate` fires, which is why this cannot mirror `hikari_xdg_view_init`'s init-time `wlr_scene_xdg_surface_create()` call.
   3. Store the returned tree on `struct hikari_xwayland_view` and tear it down on unmap, so a dissociate/re-associate cycle (X11 surface recreation) does not leak or double-attach.
   4. Check the same question for `xwayland_unmanaged_view.c` — override-redirect windows have no scene node either, and hit-testing already walks them via `output->unmanaged_xwayland_views`.
   5. Re-verify `surface_at()` in both XWayland files once content renders. Unlike xdg, XWayland surfaces carry no window-geometry offset, so the Phase 64 cursor correction should **not** be copied there — but that needs confirming against real rendering rather than assumed.

   **Ordering note:** this is the third "was never wired up" gap found in a row (popup scene nodes, layer-popup scene nodes, now XWayland content). Step 4 below — the headless smoke test — is what would have caught all three before release, and its value rises with each one found by hand.

-8. **Phase 61 Steps 3 and 4 — approved, not yet started.** Steps 1 (crash fix + Finding A) and 2 (Finding B) are implemented; see DECISIONS_LOG Phase 61. These two remain.

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

-7. **Phase 58 Issue 1 — Top bar: centre lane + real opacity (PLANNED, awaiting a decision on the config question; see DECISIONS_LOG Phase 58 for the analysis).** Issue 2 of Phase 58 is done (Phase 59). This is the remaining half.

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

-6. **Phase 55 REFACTOR: Single-Writer Visibility Transitions — the remediation (see DECISIONS_LOG Phase 55 for the root-cause analysis this derives from).**

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

-5. **Phase 54 View-Teardown Ownership-Graph Hardening — PLAN ONLY, awaiting approval (see DECISIONS_LOG Phase 54 for the measured basis and justification).** Addresses the structural problem that view teardown sequences seven `wl_list` links and six owning pointers by hand, from five entry points, with no mechanical verification. Four workstreams:

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

-4. **Phase 53 Close-Window / Popup-Button Crash — investigated, empirical reproduction needed (see DECISIONS_LOG Phase 53 for the full trace).** Re-audited the Phase 42/44/45 popup/subsurface `fini`-dispatch fix against the actual wlroots 0.20 signal-emission order (not assumption) and found it sound — ruled out as the cause of this specific crash. Live-system forensics found the compositor is aborting (SIGABRT, signal 6) 4 times today on the FreeBSD target, 3 of them on the current fully-patched, DEBUG=YES binary (assertions live, not stripped) — not segfaulting. No crash message was captured and no core dump exists. User-run steps, in order (all read-only/diagnostic, no code changes, low risk, a few minutes total):
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


