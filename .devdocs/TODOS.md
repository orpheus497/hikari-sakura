# Granular Task List

*Last Updated:* 2026-08-25 11:17

## Active List

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


