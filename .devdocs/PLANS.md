# Forward Strategy & Plans

*Last Updated:* 2026-08-21 08:53

## Implementations to be Fully Implemented

-3. **Phase 52 Config Load Failure — RESOLVED.** Root cause: `~/.config/hikari/hikari.conf:160`'s bare-modifier keybinding, silently rejected by `hikari_binding_config_key_parse()`. User fixed their config; the matching hikari-side diagnostic gap was fixed in `src/binding_config.c` (see DECISIONS_LOG Phase 52). Branches A/D/E below were ruled out during the trace (file resolution was fine; launch method was irrelevant; the failure was a pure parse error). Remaining optional, unapproved follow-ups: the identical silent-`else` gap in `hikari_binding_config_button_parse()` (mouse bindings), and the duplicate `wl_list_init(&server->outputs)` in `server_init()`.

-2. **Phase 50 Touch/Gesture Completion (from the Phase 50 investigation — see DECISIONS_LOG for the full technical writeup):** Steps 1-6 implemented this session (uncommitted); step 7 (build + runtime verification) remains user-run. User approved both optional-scope items: gesture-to-action bindings and touch-as-click WM integration. Order:
   1. **Fix the touch coordinate-space bug** (Finding 1, CRITICAL) — `cursor_touch_down_handler`/`cursor_touch_motion_handler` in `src/cursor.c` pass raw 0..1 normalized `event->x`/`event->y` into `hikari_server_node_at()`, which expects layout-pixel coordinates everywhere else it's called. Touch hit-testing is currently broken at runtime (resolves near the layout's (0,0)-(1,1) corner). Fix: call `wlr_cursor_absolute_to_layout_coords()` first in both handlers. This is the single blocking item — nothing touch-related works correctly until it lands.
   2. **Add per-device output mapping for touch** (Finding 2) — `add_touch()` in `src/server.c` should resolve `wlr_touch_from_input_device(device)->output_name` against `server->outputs` and call `wlr_cursor_map_input_to_output()`, mirroring `add_pointer()`'s existing call, so multi-output touchscreen rigs map correctly.
   3. **Confirm two open implementation-detail questions** before coding Finding 3/4 (see DECISIONS_LOG "Design" sections): (a) gesture buffer-and-replay-on-no-match vs. a simpler alternative; (b) whether primary-touch WM interaction should notify the client as touch (`wl_touch`) or emulated pointer clicks.
   4. **Implement gesture-to-action config bindings** (Finding 3) — new `bindings { gestures {} }` config block, `gesture_config.h`/`.c`, gesture-stream accumulation state on `struct hikari_cursor`, compositor-first dispatch with client-forward fallback.
   5. **Implement touch-driven window management** (Finding 4) — primary-touch-point tracking on `struct hikari_cursor`, routing the primary point through the existing `hikari_server.mode->button_handler`/`cursor_move` state machine (same one the mouse uses) for focus/raise/move/resize, while non-primary touch points keep native multi-touch client forwarding.
   6. **Documentation** (Finding 5) — add a `gestures {}` example and touch behavior notes to `etc/hikari/hikari.conf` and `share/man/man1/hikari.md`, mention touchscreen/gesture support in `README.md`, and add a touch/gesture subsection to `.devdocs/BLUEPRINT.md`'s device model.
   7. **Build + runtime verification (user-run)** — `sudo make clean && sudo make install`, then verify: single-finger tap-to-focus and drag-to-move/resize, a configured 3-finger swipe action, pinch-to-zoom passthrough in a real multitouch-aware client (Evince/Firefox), and — if multi-output touch hardware is available — that touch stays confined to its mapped output.

   Step 1 is the sole CRITICAL, concretely-diagnosed item and is recommended to land first and independently of the rest — everything else (including the already-implemented Phase 49 skeleton) is inert without it.

-1. **Phase 42 Memory/Crash/Error-Handling Remediation (from the deep audit — see DECISIONS_LOG Phase 42 for the full technical writeup):** Awaiting user approval to execute. Proposed order:
   1. **Fix the popup/subsurface type confusion** (Finding 1, CRITICAL) — `hikari_view_unmap` in `src/view.c` blindly casts every `view->children` entry to `hikari_view_subsurface`, but `hikari_xdg_popup` shares the same list via the common `hikari_view_child` prefix. The two structs diverge after byte 88, so the cast reads/frees the wrong fields and leaves 4 live wlroots listener registrations pointing into freed memory — a delayed use-after-free. Fix: add a discriminator (enum tag or `fini` fn-pointer) to `hikari_view_child` so the teardown loop dispatches correctly. This is the leading candidate for the user's "closing windows / multiple browser tabs / random crashes" reports, and is independently corroborated by a real `swaywm/sway` issue (#5321) hitting the identical subsurface/popup lifecycle class via Firefox.
   2. **Make shutdown signal-safe and add SIGINT** (Finding 2, CRITICAL) — swap the raw `signal(SIGTERM, sig_handler)` (which calls non-async-signal-safe code directly from signal context) for `wl_event_loop_add_signal()` on both `SIGTERM` and `SIGINT`, routed through the existing (already-correct) `hikari_server_terminate` graceful-shutdown sequence. Matches the pattern used by Wayfire/labwc.
   3. **Add built-in leveled logging** (Finding 3, HIGH) — a `hikari_log()` wrapper, then a mechanical sweep converting `fprintf(stderr, ...)` call sites.
   4. **Decide the OOM/fail-fast policy's scope** (Finding 4, HIGH) — user decision, not a unilateral change.
   5. **Harden production-relevant `assert()` sites** (Finding 5, MEDIUM), starting with `add_keyboard()` in `src/server.c`.
   6. **Optional: harden `hikari_command_execute`'s blocking reap** (Finding 6, LOW).

   Steps 1 and 2 are the two CRITICAL, concretely-diagnosed items and are recommended to land first, independently of the rest.

   **Phase 44 additions (deepened audit, see DECISIONS_LOG Phase 44):**
   7. **`hikari_switch` leak on device unplug** (Finding 7) — one-line `hikari_free` addition in `src/switch.c`.
   8. **Indicator-bar allocation churn** (Finding 8) — port `bar.c`'s cache-key short-circuit into `indicator_bar.c`. This is the concrete, low-risk answer to "data-oriented design" for this codebase: reuse/compare instead of reallocate-every-call, using a pattern already proven here rather than a new abstraction.
   9. **Keymap ref leak on reconfigure** (Finding 9) — add the missing `xkb_keymap_unref` in `src/keyboard.c`; confirm reachability via `configuration.c`'s reload path first.
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


