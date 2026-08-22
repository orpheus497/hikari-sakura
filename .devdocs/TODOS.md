# Granular Task List

*Last Updated:* 2026-08-22 11:59

## Active List

### Phase 73: FB-6 retired + W2 implemented — F1 and F2 FIXED (see DECISIONS_LOG Phase 73)

- [x] **FB-6 retired per the user's Option 1 ruling.** `WITH_POSIX_C_SOURCE` gone, plus the two comment blocks referencing it. No reference remains outside `.devdocs/`; default build unchanged. **Closes TODOS P3.**
- [x] **W2 — six scene layer trees** (`background`/`bottom`/`views`/`top`/`overlay`/`lock`) in `struct hikari_server.layers`, created in `setup_scene_graph()`. All 7 attachment sites repointed; **zero** scene-root attachments remain outside `server.c`. Order established by raising each in turn rather than trusting insertion order, since that could not be tested at runtime.
- [x] **F1 (CRITICAL) FIXED structurally.** The boundary is four `set_enabled(false)` calls on the desktop layers — wlroots disables every child of a disabled node, so views, bar, indicator overlays and all layer-shell surfaces go dark together. Public views are reparented onto the lock layer **and explicitly enabled**, which is not redundant with the flag flip: a public view parked on another sheet has a *disabled* node that clearing the hidden flag never touched. **That is exactly why a `public` clock never appeared.**
- [x] **F2 (HIGH) FIXED structurally**, and the false comment replaced with one naming the real mechanism.
- [x] **Bug in this phase's own work, caught before shipping.** A view's scene tree outlives unmap (destroyed in `destroy_handler`, not `hikari_view_unmap`), so a public view unmapping while locked and remapping after unlock would keep a stale parent under the disabled lock layer — **invisible forever**, and `reset_visibility()` could not catch it because unmap removes the view from the very list that loop iterates. Fixed by deriving the parent unconditionally on every map.
- [x] **Stacking preserved across lock/unlock.** `output->views` is top-first and `reparent` appends, so forward iteration would have **inverted the desktop** on every unlock; both loops use `wl_list_for_each_reverse`.
- [x] **Layer-shell ordering is now structural** via `layer_scene_tree()`; both ad-hoc raise/lower pairs deleted, and `set_layer` is a **reparent**. Fixes a latent bug where `BACKGROUND` surfaces sank *below* the wallpaper because both called `lower_to_bottom()`.
- [x] **NEW BUG found and fixed (not in the plan): views never restacked in the scene at all.** Nothing anywhere called `raise_to_top` on `view->scene_node`; `hikari_view_raise()` reordered only hikari's lists, so window stacking was **fixed at map time** and raising a partially covered window focused it while it stayed drawn underneath. Scene half added to `raise_view()` and `hikari_view_lower()`, scoped to the parent tree.
- [ ] **DEVIATION — the `forced` flag was NOT deleted, contrary to `PLANS.md` W2 step 3.** It is **15 sites**, not the handful the plan assumed, including six in `commit_pending_operation()` / `hikari_view_migrate_to_sheet()` where branches are **provably unreachable** given `view.c:108`'s invariant — i.e. dead code in the subsystem behind eight crash phases (42/44/45/55/56/57/61/63). F1 and F2 are fixed by the trees alone, so this is pure cleanup, not a prerequisite. **Tracked as its own follow-up; do not bundle it into another large change.**
- [ ] **RUNTIME VERIFICATION — the highest-priority test in the project's history (USER-RUN).** This change is unbuilt and unrun. Exercise, in order:
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
- [ ] **W1-4 / FB-6 -- HELD, NEEDS A USER DECISION.** Root cause is deeper than the plan's one-line description: all three symbols (`explicit_bzero`, `setgroups`, `usleep`) live behind `__BSD_VISIBLE`, which FreeBSD's `<sys/cdefs.h>` clears whenever `_POSIX_C_SOURCE` is defined — and `lock_mode.c`'s existing shim is guarded `!defined(__FreeBSD__)`, so it never fires here. **Option 1 (recommended): retire `WITH_POSIX_C_SOURCE`** — 4 lines deleted, removes a permanently-broken config that `WITH_ALL` never sets, and strict-POSIX namespace enforcement has no consumer in a FreeBSD-only compositor. **Option 2: keep and fix** with three `__BSD_VISIBLE`-guarded declarations across three files. Option 2 was not taken unilaterally because it is a workaround spreading over three files, which the standing anti-debt directive rules out; Option 1 was not taken unilaterally because AGENTS.md §3 forbids removing a feature without instruction. Nothing regresses while this waits — the config has been broken all along.
- [ ] **Open question for the user: add `libdrm` as an explicit dependency?** `drmGetVersion(fd)->name` would report `i915` vs `nvidia-drm` directly instead of the inferred device path, which is strictly better FB-3 evidence. libdrm is MIT (AGENTS.md-compliant) and its headers are already reachable via wlroots' cflags, but `pkg-config --libs wlroots-0.20` does not export `-ldrm`, so this adds a real link dependency — outside the approved W1 scope, hence not taken.
- [ ] **Build verification (USER-RUN).** Not built, not linked. The new startup log block is the thing to read: it should name the renderer's DRM node, the card count, and the `XDG_RUNTIME_DIR` filesystem + `posix_fallocate` result. On this machine expect 2 card nodes and `zfs`.

### Phase 71: W5 + W6 implemented (see DECISIONS_LOG Phase 71)

- [x] **F3 -- unguarded `mode->disable_outputs`, fixed at BOTH sites.** The plan named only `lock_mode.c:819-827`; implementation found a second unguarded dereference in `disable_outputs()` (`:507`) that `key_handler`'s Ctrl+C branch reaches **directly**, so a failed timer allocation faulted on a keystroke rather than only at lock time. Both guarded; lost timer now degrades to "never blanks" with a `wlr_log(WLR_ERROR)`, per the Phase 61 policy. `<wlr/util/log.h>` added.
- [x] **F5 -- unlocker fatal-PAM path now writes a deny result** (`hikari_unlocker.c:143`), matching the `pam_start` path at `:88`. **Benefit is narrower than the plan implied:** the pre-existing code already recovered correctly via `WL_EVENT_HANGUP` (`locker_result_handler` classifies the `READABLE|HANGUP` pair as terminal, per its own comment at `lock_mode.c:362-372`). What changes is that the deny indicator appears when the helper says so instead of waiting on process teardown. Comment corrected to claim only that.
- [x] **C1 -- `wlr_xwayland_set_seat()` added** after `setup_selection()` in `server_init()`. Ordering forced: `setup_xwayland()` runs earlier and the seat does not exist yet. X11 <-> Wayland clipboard and primary selection restored.
- [x] **C1 follow-up: the plan's "add a `seat_destroy` guard" is WRONG and was not done.** `struct wlr_xwayland` owns a private `seat_destroy` listener (`wlr/xwayland/xwayland.h:78`, `WLR_PRIVATE`); a second one would be duplicate state.
- [x] **C2 -- `ext-data-control-v1` advertised** alongside `wlr-data-control-v1`. Both coexist by design; old tools bind the wlr- variant, newer ones prefer ext-.
- [x] **C3 -- both data-control manager returns guarded** with `wlr_log(WLR_ERROR)`. Non-fatal deliberately: a missing clipboard manager degrades tooling but leaves the compositor usable.
- [x] **Validation:** 0 warnings on all three files under the Phase-68-corrected clang invocation, including `server.c` compiled **without** feature macros to exercise the `#ifdef HAVE_XWAYLAND` guard as false. `nm -D` confirms all three new symbols are exported by the installed `libwlroots-0.20.so`. Three `-Wextra` warnings in `hikari_unlocker.c` confirmed pre-existing at `HEAD`.
- [ ] **Build + runtime verification (USER-RUN).** Not built, not run -- the agent cannot `make` (root-owned artefacts). Test after installing: copy in an X11 app (`xterm`), paste into a Wayland app, and the reverse; `wl-paste --watch` should see selections from both. Lock/unlock should behave exactly as before (F3/F5 are failure-path only and invisible in a healthy run).

### Phase 70: Lock screen, blur/clock, screencopy, clipboard (see DECISIONS_LOG Phase 70, PLANS item -12)

**W0 -- USER-RUN diagnostic matrix.** Read-only, ~30 min, run each from a TTY with `HIKARI_LOG=/tmp/hikari-$N.log`. The agent cannot run these (sandbox reports Linux/GCC; host is FreeBSD 15.1/clang) and cannot build.

- [ ] **W0-1 `WLR_DRM_DEVICES=/dev/dri/card0 start-hikari`** -- tests **H0 (multi-GPU, new prime suspect)**. This machine is hybrid: `card0` = Intel Iris Xe (eDP-1 lives here), `card1` = NVIDIA GTX 1650 Ti with `hw.nvidiadrm.modeset=1`. **Most likely single answer to a blocker open since Phase 19.**
- [ ] **W0-2 `WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128 start-hikari`** -- render-node split.
- [ ] **W0-3 `WLR_DRM_NO_MODIFIERS=1 start-hikari`** -- H2 `IN_FORMATS` mismatch.
- [ ] **W0-4 `WLR_RENDERER=pixman WLR_RENDERER_ALLOW_SOFTWARE=1 start-hikari`** -- H1 Mesa/GBM.
- [ ] **W0-5 `WLR_DRM_NO_ATOMIC=1 start-hikari`** -- drm-kmod atomic KMS path.
- [ ] **W0-6 Lock, wait 4 min, press a key** -- resolves **F4 / P2-14** (`current_mode` retention across output disable/enable). Screen returning means F4 needs no fix.
- [ ] **W0-7** one line each: `sysctl kern.vt.machine_terminal`, `pkg info -x mesa drm-kmod`, `stat -f '%T' "$XDG_RUNTIME_DIR"`.

**Findings to fix.** Severity as assessed in DECISIONS_LOG Phase 70 Part A.

- [x] **F1 (CRITICAL) -- the lock screen hides nothing. FIXED, Phase 73 (W2).** `override_visibility()` (`lock_mode.c:749-768`) flips flags only; the flag reaches the scene graph solely via `view.c:1157`/`:1193`, both of which assert `!is_forced` -- the exact state lock mode establishes. Private window contents, the top bar and every layer surface stay rendered for the ~1 s before blank and for a fresh 10 s after each keystroke. **Fixed by W2 (user ruled Q1: hold for the proper fix, no interim patch).**
- [x] **F2 (HIGH) -- a window mapping while locked appears on the lock screen. FIXED, Phase 73 (W2).** The false comment is replaced with one naming the real mechanism (the view layer is disabled, and wlr_scene disables every child of a disabled node). Implementation additionally uncovered the stale-parent case across an unmap/remap lock cycle — see Phase 73.
- [x] **F3 (MEDIUM) -- unguarded timer pointer. FIXED, Phase 71 (W5).** Turned out to be **two** sites, not one -- `disable_outputs()` (`:507`) is reachable unguarded via `key_handler`'s Ctrl+C branch. Phase 68's sweep covered `wlr_*_create*` only, which is why the `wl_event_loop_*` sites were missed.
- [ ] **F4 (MEDIUM) -- output re-enabled without a mode.** `hikari_output_enable` (`output.c:323-354`) omits `wlr_output_state_set_mode()`, unlike `hikari_output_init` (`:553-556`). **Conditional on W0-6. Same item as P2-14 -- do not track twice. W5.**
- [x] **F5 (LOW) -- unlocker fatal-PAM path writes no result. FIXED, Phase 71 (W5).** The "silently consumes one attempt" framing above was **wrong** and is corrected in DECISIONS_LOG Phase 71: `locker_result_handler` already recovered via the hangup. The real gain is that deny is now immediate rather than waiting on process teardown.
- [x] **C1 -- `wlr_xwayland_set_seat()` is called nowhere in the tree. FIXED, Phase 71 (W6).** Added after `setup_selection()`. The plan's accompanying "add a `seat_destroy` guard" was found **incorrect** and deliberately not done -- wlroots owns that listener privately.
- [x] **C2 -- no `ext_data_control_manager_v1`. FIXED, Phase 71 (W6).** Both generations now advertised.
- [x] **C3 -- discarded return of `wlr_data_control_manager_v1_create`. FIXED, Phase 71 (W6),** non-fatally by choice.
- [ ] **N5 -- XWayland renders no content, CONFIRMED (was `PLANS.md` item -9 "awaiting confirmation").** `xwayland_unmanaged_view.c` has **no `wlr_scene` reference at all**; `xwayland_view.c:537` attaches only border + indicator frame. **W8, which must not land before W2** -- fixing this widens the F1 hole.

**Workstream status.** **W5 and W6 are IMPLEMENTED (Phase 71)**, except F4, which is held pending W0-6. Remaining, in the recommended order:

- [x] **W1** platform capability layer + buffer consolidation + **FB-8** — implemented Phase 72. **FB-6 held pending a user decision** (see Phase 72 section above).
- [x] **W2** scene layer trees (D1) -- implemented Phase 73. **The `forced` flag deletion was deferred**, see the Phase 73 deviation note.
- [ ] **W3** capture + blur. **CPU baseline first, GPU second (Q3 ruling).** Carries one open **SPIKE**: no public render-format query exists in 0.20.2, so the swapchain format needs a logged escalation ladder (implicit XRGB8888 -> LINEAR -> ARGB8888 -> abort to solid `clear`).
- [ ] **W4** backdrop + cairo/Pango clock + **power-aware blank timeout (Q2 ruling: 180 s AC / 60 s battery, configurable, `0` = never)**. Read `hw.acpi.acline` via `sysctlbyname()` at arm time, never cached.
- [ ] **W7** `ext-image-copy-capture-v1` + `ext_foreign_toplevel_list_v1`; fix `XDG_CURRENT_DESKTOP` to `"Hikari Sakura:wlroots"` (`start-hikari.sh:26` + `hikari.desktop`) so `xdg-desktop-portal-wlr` matches at all.
- [ ] **W8** XWayland scene integration (see N5).

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
- [ ] **P0 -- USER BUILD + TEST (single cycle).** `rm -f *.o && make DEBUG=YES && sudo make DEBUG=YES install`, then `export HIKARI_LOG=/tmp/hikari-$(date +%s).log` and run. Verified `DEBUG=YES` compiles clean. `DEBUG=YES` also re-enables all 234 asserts -- any that fire are real invariant violations release silently ignores.
- [ ] **P0 -- XWayland verification:** `xterm`, then `xeyes`. Confirms or refutes the B diagnosis.
- [ ] **P1 -- Phase 64 XWayland render gap: re-evaluate ONLY after B is confirmed.** `xwayland_view.c` attaches no surface content to its scene tree. This has been untestable all along because XWayland never started.
- [ ] **P2 -- 234 dead `assert()` calls across 32 files** (`view.c`: 101). All compiled out by `-DNDEBUG`. Phase 61 approved the `wlr_log(WLR_ERROR)` + safe-bail replacement policy but it has only been applied at a handful of sites. Needs scoping as its own project.
- [ ] **P2 -- TC-FORMAT-01 is loadable but still must not be run casually.** The configured style (8-wide tabs, Allman) does not describe the tree (2-space, tabless, attached control braces); `src/server.c` alone measures a ~4050-line diff. `SortIncludes: true` also orphans the `Action purpose:` comment above `wlr/interfaces/wlr_buffer.h`. User has elected to keep the style as configured -- decide separately whether the run ever happens.
- [x] **P3 -- `WITH_POSIX_C_SOURCE=YES` was a broken build configuration. CLOSED Phase 73: flag retired** (user ruling, Option 1). Never set by `WITH_ALL`, so unused by default; enabling it yields 3 implicit-function-declaration warnings and, with `DEBUG=YES` (`-Werror`), fails the build. Two are security-relevant: `explicit_bzero` (`lock_mode.c:70`, wipes the password buffer) and `setgroups` (`server.c:1087`, privilege dropping); `usleep` (`bar.c:385`) is cosmetic.

### Phase 67: External review round 3 (see DECISIONS_LOG Phase 67)

- [x] **Finding 1 — layer-shell NULL deref (`src/server.c`, `setup_layer_shell`).** `wlr_layer_shell_v1_create()`'s result went straight into `wl_signal_add`, so `&NULL->events.new_surface` was computed and written through on allocation failure. Guarded with `wl_display_destroy` + `exit(EXIT_FAILURE)`, matching the `pointer_gestures` guard six lines from the call site.
- [x] **Finding 2 — virtual pointer confined the whole cursor (`src/server.c`, `new_virtual_pointer_handler`).** `wlr_cursor_map_to_output()` is cursor-wide; a `zwlr_virtual_pointer_v1` client with a suggested output trapped the physical mouse/touchpad/touchscreen on that output with no recovery short of restart. Replaced with `wlr_cursor_map_input_to_output(cursor, device, suggested_output)`. Attach precondition, call ordering vs `add_pointer()`, and idiom parity with `map_touch_to_output()` all verified before editing.
- [x] Both regions syntax-check clean with `-DHAVE_LAYERSHELL -DHAVE_VIRTUAL_INPUT` and a `__kernel_size_t` shim. Full command in DECISIONS_LOG Phase 67.
- [ ] **P2 — decision needed: sweep the sibling `setup_*` helpers?** `setup_xdg_shell`, `setup_xdg_activation` and `setup_idle_inhibit` have the **identical** unguarded `wlr_*_create` → `wl_signal_add` pattern Finding 1 just fixed. Deliberately left untouched as outside the reviewed findings. Needs user direction before any edit.
- [ ] **TC-FORMAT-01 blocked, not just pending.** The installed `clang-format` rejects this repo's `.clang-format` outright (`unknown enumerated scalar` on `Language: C` — a version mismatch), so the compliance run cannot be performed in this environment at all. Either pin a matching `clang-format` version or relax the config.
- [ ] **P3 — `PLANS.md` items 4a/12 note.** The planned headless smoke-test client binds `zwlr_virtual_pointer_v1`; before Finding 2 it would have hit the cursor-hijack bug and likely been misread as a test-harness quirk. Worth a line in the test plan when it is written.

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
- [ ] **P0 — XWayland does not start; supersedes the Phase 64 render-gap test.** `ps` shows **no `Xwayland` process**. hikari created `/tmp/.X11-unix/X0` at 16:05 but wlroots spawns XWayland lazily on first connect, and `xterm`/`obs` exited rather than opening blank. So "did not open" is XWayland failing to start — a separate, earlier problem than the missing scene content. **Next:** from inside hikari, `echo $DISPLAY`, then `xterm 2>&1 | tee /tmp/xterm.log`.
- [ ] **Still open, unchanged:** Phase 64's finding that `src/xwayland_view.c` attaches no surface content to its scene tree. Verified by code inspection, but **not** demonstrated by the xterm test. Only observable once XWayland runs.

### Phase 64: Cursor offset FIXED; review-finding triage; XWayland content gap found

- [x] **Cursor offset ROOT CAUSE + FIX.** `surface_at()` in `src/xdg_view.c` passed **window-geometry-local** coords to `wlr_xdg_surface_surface_at()`, which takes **wl_surface-local** ones. The two differ by `xdg_surface->geometry.x/y` — the CSD margin, non-zero for most GTK clients — so every hit test landed that far up-and-left of the real pointer. Rendering was already correct because `wlr_scene_xdg_surface_create()` applies the same correction with the opposite sign. Fixed by adding `+ window->x/y`.
- [x] Confirmed the same offset in the damage path is **harmless**: `hikari_output_add_damage()` and `hikari_output_add_effective_surface_damage()` discard the rect and only schedule a frame (the scene graph does real damage tracking). No change made.
- [x] Review triage: **7 findings implemented** (premultiplied alpha at 12 scene-rect sites, `node_at` out-param init, keycode `strtol` underflow, locker `add_fd` NULL, colour int range, `damage_whole` enabled guard, layer-popup geometry init). **5 verified invalid and skipped** with reasons. `AGENTS.md` left untouched per user decision.
- [ ] **NEW — P0, XWayland views render no content.** `src/xwayland_view.c` creates a `scene_tree` and attaches **only** border + indicator_frame to it; there is no `wlr_scene_subsurface_tree_create()` for `xwayland_surface->surface` anywhere in the file (its own comment at :526 says "for the XWayland view's border and indicator frame nodes"). Managed X11 windows therefore draw a border and nothing inside it. Needs approval before fixing.
- [ ] **Correction to Phase 62's reasoning:** it attributed Firefox surviving the popup abort to "Firefox is XWayland and never creates xdg_popups". If XWayland content does not render at all, the Firefox seen working was native Wayland, and it survived only because no menu had been opened. The Phase 62 *fix* stands — it was proven by core dump — but that explanation was wrong.

### Phase 63: Popups never had a scene node + shutdown NULL deref (see DECISIONS_LOG Phase 63)

- [x] **Session survived 11 minutes with no runtime crash — a first.** VT switching, Firefox and pavucontrol all held. It then segfaulted on exit (exit 139).
- [x] **ROOT CAUSE of "right click menus and submenus did not come up":** `xdg_popup_create()` never created a scene node for the popup. Its comment claimed `wlr_scene_xdg_surface_create()` "already manages popup scene nodes automatically" — **false**. That helper calls `wlr_scene_subsurface_tree_create()`, which walks **subsurfaces**, not **popups**; nothing in `types/scene/xdg_shell.c` traverses popups. tinywl calls the helper once per popup for exactly this reason. **Every xdg popup in hikari has therefore never rendered.**
- [x] Masked until now because until Phase 62 *creating* a popup aborted the compositor first. Fixing the abort exposed it.
- [x] **Same gap in `src/layer_shell.c`** — `wlr_scene_layer_surface_v1_create()` covers the layer surface and its subsurfaces only, so layer-shell popups never rendered either. **This retroactively explains the long-standing "Layer-client spot check (waybar with sub-menus)" backlog item.**
- [x] **FIXED:** `scene_tree` field added to `struct hikari_xdg_popup` and `struct hikari_layer_popup`, created via `wlr_scene_xdg_surface_create()` parented to the parent surface's tree. xdg popups publish their tree on `base->data` so nested submenus resolve their parent. wlroots owns the tree lifetime, so the destroy paths are deliberately unchanged.
- [x] `init_popup()` in `layer_shell.c` now returns `bool`; both callers free the tracking struct on failure (no listener is registered before that point).
- [x] **Third core dump** (`hikari.4177.1001.core`, 15:27:36, signal 11) — **shutdown-only crash**. `hikari_workspace_focus_view()` dereferenced `hikari_server.workspace` unguarded; `hikari_output_fini()` sets it to NULL while tearing down the noop output, and at shutdown a real output can be finalised after that. **FIXED** with the approved safe-bail pattern.
- [x] All seven modified files pass `cc -fsyntax-only -Wall`.
- [ ] **P0 — USER-RUN, NEXT ACTION:** `sudo make clean && sudo make install`, then right-click menus, submenus, combo-box dropdowns, and quit cleanly to confirm exit status 0.
- [ ] **Cursor offset — INVESTIGATED, not yet fixed.** `xdg_view.c`'s `surface_at()` passes window-geometry-local coordinates to `wlr_xdg_surface_surface_at()`, which is documented (`wlr_xdg_shell.h:526`) as taking **surface-local** ones. They differ by `xdg_surface->geometry.x/y`, the CSD shadow margin — non-zero for essentially every GTK client. Rendering is unaffected (the scene graph applies the offset itself), so the pointer draws correctly but hit-tests offset by the shadow width. Must also check `xwayland_view.c` and `layer_shell.c` `surface_at` before changing.

### Phase 62: SECOND ROOT CAUSE — popup unconstrained before initialisation (see DECISIONS_LOG Phase 62)

- [x] **Second core dump captured** (`hikari.52741.1001.core`, 15:01:11, **signal 6 / SIGABRT**) after the user built and installed the Phase 61 fix. VT switching now survives and Firefox is fine; **pavucontrol crashed immediately**.
- [x] **ROOT CAUSE:** `xdg_popup_create()` called `popup_unconstrain()` at popup-creation time. `wlr_xdg_popup_unconstrain_from_box()` ends with `wlr_xdg_surface_schedule_configure()` (`wlr_xdg_popup.c:534`), which asserts `surface->initialized` (`wlr_xdg_surface.c:168`). wlroots emits `new_popup` from the client's `get_popup` request (`wlr_xdg_popup.c:429/431`), **before the popup surface is ever committed** — so `initialized` is always false there. Aborted on **every xdg_popup**: every GTK menu, combo box, dropdown, tooltip.
- [x] **The "lots of children/background processes" correlation was wrong.** The real predictor is *native-Wayland clients that open popups*. Firefox-on-XWayland never creates an xdg_popup, which is why it survived; pavucontrol opens one on launch, which is why it died instantly.
- [x] **FIXED in `src/xdg_view.c`:** `popup_unconstrain()` moved into `popup_commit_handler()`'s `initial_commit` branch, replacing the bare `schedule_configure` (unconstrain schedules it itself). Forward declaration added.
- [x] **FIXED in `src/layer_shell.c`:** identical defect at `init_popup()`; moved into `commit_popup_handler()`'s `initial_commit` branch. Stale `init_popup` comment corrected.
- [x] **The same constraint was already understood and fixed for toplevels** — `hikari_xdg_view_init` carries a comment citing `wlr_xdg_surface.c` line 168 as the reason `wlr_xdg_surface_ping` was removed. It was never applied to popups, in either file.
- [x] Swept the tree: every other `wlr_xdg_toplevel_set_size` / `set_activated` / `set_fullscreen` / `wlr_layer_surface_v1_configure` call site is already `initialized`-guarded.
- [x] Both files pass `cc -fsyntax-only -Wall`.
- [ ] **P0 — USER-RUN, NEXT ACTION:** `sudo make clean && sudo make install`, then open pavucontrol, then any GTK menu / combo box / right-click context menu.
- [ ] **Housekeeping:** 14 `firefox.*.core` files (~8 GB) in `/var/coredumps` from 15:01 — Firefox's own children dying (ZFS `posix_fallocate`), not a hikari fault. Safe to delete.


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


