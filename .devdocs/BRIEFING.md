# Hikari Project Briefing

*Last Updated:* 2026-08-21 10:30

## Current Status

- **Phase:** Phase 52 (Post-Install Config Load Failure) — **RESOLVED**. Root cause: a bare-modifier keybinding (`"L" = action-menu`, no key) in the user's `~/.config/hikari/hikari.conf:160`, silently rejected by `hikari_binding_config_key_parse()` with zero diagnostic anywhere in the call chain — the only message that ever printed was `server.c`'s generic load-failure wrapper, which is why it looked unexplained. User fixed their config; the matching hikari-side silent-failure bug was fixed in `src/binding_config.c` (added the missing `fprintf`). Confirmed unrelated to Phase 50's touch/gesture work — the user's `gestures {}` block parses cleanly, and config load completes before any touch/gesture code can run. Full trace in `DECISIONS_LOG.md` Phase 52. Two low-severity, unapproved follow-ups noted but not actioned: the identical silent-failure gap in the mouse-binding parser, and a redundant duplicate `wl_list_init(&server->outputs)` in `server_init()`.
- **Phase 51** (Documentation Rebranding & FreeBSD Overhaul, concurrent session) complete. **Phase 50** (Touch/Gesture Correctness & Completion) steps 1-6 implemented (uncommitted); step 7 (user-run build + runtime verification) is now unblocked since the compositor can actually start again. See `PLANS.md` item -2 and `DECISIONS_LOG.md` for the full writeup. Summary: fixed the CRITICAL touch coordinate-space bug plus a hard compile error found live in `touch_cancel`; added per-device output mapping; implemented a full `inputs { gestures {} }` config-binding system with buffer-and-replay dispatch; implemented touch-as-click window management; documented all of it in `hikari.conf`/`hikari.md`/`README.md`/`BLUEPRINT.md` on top of the Phase 51 rebrand.
- **Branch:** wlroots-0.17.1 (PR source ref; the dependency itself is wlroots 0.20.2 — see Makefile:148-149, confirmed via `pkg-config --modversion wlroots-0.20`)
- **Overall progress:** Documentation rebranded to Hikari Sakura (Phase 51) and now also documents touch/gesture support (Phase 50 Finding 5, closed). Windows render and the Phase 40 NULL-output guards are in place. Phase 42's audit found a concrete root cause for crashes (resolved in Phase 45-47).
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** Awaiting a user build (`sudo make clean && sudo make install`) and runtime test of Phase 50's touch/gesture changes (`PLANS.md` item -2 step 7 / `TODOS.md` P5).
- **Blockers:** eDP-1 scanout swapchain test failure — Mesa/EGL/GBM ↔ drm-kmod layer (pre-existing, below hikari), unrelated to Phase 42/50. The root-owned build-artifact blocker from prior phases is resolved.

## Remaining Work

- **Phase 50 build + runtime verification (user-run):** See `PLANS.md` item -2 step 7 and `TODOS.md`'s "Phase 50" P5 checklist — tap-to-focus/drag-to-move/resize, a configured 3-finger swipe action, pinch-to-zoom passthrough in Evince/Firefox, multi-output touch confinement.
- **Phase 42 fixes (user approval pending):** See `PLANS.md` item -1 and `TODOS.md`'s "Phase 42 findings" section for the full ordered action list.
- **Phase 40 build + runtime verification (user-run):** Confirm the `queue_resize`/`hikari_view_move*`/`MOVE` guards compile cleanly and eliminate the reported multi-window/multi-workspace/Firefox/resize crashes. If a crash survives, get a backtrace (debug build or ASan) rather than more static review.
- **CRITICAL (pre-existing):** Resolve the eDP-1 scanout swapchain failure (run Phase 19 diagnostics matrix). Phase 28 guard eliminates the spurious disable commit; underlying GBM/drm-kmod issue may still require Mesa/kernel-side action.
- **Phase 38 visual verification (user-run):** Border and indicator-frame placement — the parent-relative coordinate fix is reasoned from wlroots scene semantics but has not been visually confirmed.
- **Phase 38 teardown verification:** Window close should neither crash nor leak (new XDG scene tree destroy path).
- **Lock/unlock re-verification:** The unlocker now resolves to `${PREFIX}/bin/hikari-unlocker` via a compile-time absolute path (PATH-hijack fix). A helper installed anywhere else will fail to launch.
- **Multi-output check:** `src/indicator_bar.c` now adds the output origin; untested on an output not at layout origin (0,0).
- XWayland override-redirect smoke test (context menus, tooltips) — Phase 36 fix, still unverified at runtime.
- VT switch verification — Phase 36 session guard, still unverified at runtime.
- PAM unlocker live verification (setuid 4555 path).
- P2-14 `current_mode` retention across output disable/enable.
- Layer-client spot check (waybar with sub-menus).
- TC-FORMAT-01 `clang-format` compliance run.
- Optional hygiene (pending user direction): comment-header rollout to non-compliant `src/` files; cosmetic enum-compare warnings (`src/dnd_mode.c:63`, `src/move_mode.c:78`).
- Open PR #1 review threads: ~14 unresolved, all documentation/comment-style nits (no correctness findings outstanding).

*Granular tasks are tracked in `.devdocs/TODOS.md`.*
