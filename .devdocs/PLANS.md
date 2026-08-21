# Forward Strategy & Plans

*Last Updated:* 2026-08-21 10:56

## Implementations to be Fully Implemented

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


