# Hikari Project Briefing

*Last Updated:* 2026-08-20 (session context date; `date` not executed this session)

## Current Status

- **Phase:** Phase 40 complete (agent-side; build/runtime verification pending user) — swept the `view->output` NULL-nullability window (Phase 38's root-cause class) into `queue_resize`, `hikari_view_move`, `hikari_view_move_absolute`, and the `MOVE(pos)` macro in `src/view.c`.
- **Branch:** wlroots-0.17.1 (PR source ref; the dependency itself is wlroots 0.20 — see Makefile:148-149)
- **Overall progress:** **Windows now open — user-confirmed as the most functional state to date.** This is the first runtime-verified milestone since the wlroots 0.20 migration began; prior phases were static-analysis fixes that never got far enough to validate.
- **Target OS:** FreeBSD 15.1-RELEASE (ZFS root)
- **Current step:** User to build (`sudo make clean && sudo make install`, root-owned `.o` artifacts block agent-side compilation) and stress-test the Phase 40 fix: multiple windows, multiple workspaces, many Firefox tabs, and interactive resize of Firefox. If crashes persist, capture a debug/ASan backtrace (`NDEBUG` unset) rather than continuing static investigation — the obvious lifecycle-wiring bugs (view-list balance, sheet/workspace array bounds, layer-shell teardown, process spawn, output damage) have all been read in full and are sound.
- **Blockers:** eDP-1 scanout swapchain test failure — Mesa/EGL/GBM ↔ drm-kmod layer (pre-existing, below hikari). The root-owned build-artifact blocker is resolved; `sudo make clean && sudo make install` is the working build path.

## Remaining Work

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
