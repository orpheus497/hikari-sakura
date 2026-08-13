# Forward Strategy & Plans

*Last Updated:* 2026-08-13 17:08

## Implementations to be Fully Implemented

0. **Phase 24 Hardening Stream (from deep wiring audit):**
   - Enforce strict parse failure for unknown `outputs` keys (`src/configuration.c`).
   - Fix `parse_switches` iterator lifecycle (`ucl_object_iterate_free`) in `src/configuration.c`.
   - Correct lock helper child exec-failure semantics (`src/lock_mode.c`: failed `execl("hikari-unlocker")` must not `exit(0)`).
   - Add explicit stderr diagnostics for failed output modeset commit return path (`src/output.c:350-353`).
   - Replace TODO-marked CSD whole-output damage fallback with granular damage in `src/view.c`.
   - Decide and apply allocation-failure policy across `hikari_malloc`/`hikari_calloc` callsites.

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

*(Removed 2026-08-13: the `wlr_output_effective_resolution()` API-verification item — closed per TODOS completed list; the successful user build proves the symbol exists.)*

## Completed Implementations

0. **Phase 18/18b Runtime Failure Investigation & Remediation:** ✅ Completed 2026-08-13.
   - Full static root-cause investigation: 4 P0 + 3 P1 + 8 P2 defects with file:line evidence; 3 further build-discovered defects fixed during validation. (Findings consolidated into BLUEPRINT.md §5/§6 and the trackers in Phase 22.)
   - All 18 items remediated and annotated; default `etc/hikari/hikari.conf` authored against the verified parser grammar; layer-shell scene integration implemented; xwayland 0.20 lifecycle migrated.
   - TC-BUILD-01 (default) and TC-BUILD-02 (full-feature) pass from clean trees with 0 errors.

1. **Non-Blocking PAM I/O Refactor:** ✅ Completed 2026-07-31.
   - Replaced blocking `read()` in `submit_password()` with `wl_event_loop_add_fd()`.
   - Added `locker_result_handler()` callback.
   - Added `locker_event_source` field to `struct hikari_lock_mode`.
   - Cleanup in both handler and `cancel()` path.

2. **start-hikari.sh Binary Resolution:** ✅ Completed 2026-08-02.
   - Added `SCRIPT_DIR` derivation for reliable sibling binary resolution.
   - Three-tier lookup: `${SCRIPT_DIR}/hikari` → PATH → `./hikari`.
   - Error message includes `${SCRIPT_DIR}` for diagnostics.

3. **ZFS Detection Warning:** ✅ Completed 2026-07-31.
   - `start-hikari.sh` detects ZFS-backed `XDG_RUNTIME_DIR` and warns users.
   - README updated with step-by-step tmpfs fix instructions.

4. **PAM Config Fix:** ✅ Completed 2026-07-31.
   - Changed `hikari-unlocker.FreeBSD` from `auth include passwd` to `auth include system`.

5. **Final Build Validation:** ✅ Completed 2026-08-13.
   - User-confirmed `make` builds clean on the FreeBSD target (includes all Phase 13-17 fixes).
   - Agent-sandbox `bmake` failure was an environment artifact (missing `libucl` pkg-config entry), not a project defect.
