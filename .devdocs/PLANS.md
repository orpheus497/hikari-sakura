# Forward Strategy & Plans

*Last Updated:* 2026-08-13 05:41 (environment clock, corroborated by build mtimes)

## Implementations to be Fully Implemented

1. **PAM Verification Execution:**
   - Deploy `hikari` and `hikari-unlocker` to a native FreeBSD environment.
   - Identify the canonical absolute installed path for `hikari-unlocker` (e.g. `${PREFIX}/bin/hikari-unlocker`).
   - Verify the binary has trusted package provenance and existing `root:wheel` ownership.
   - Only after verification, apply mode `4555` to the absolute installed path (e.g. `chmod 4555 /usr/local/bin/hikari-unlocker`).
   - Launch compositor and invoke `Meta+L` to trigger the lock screen.
   - Verify input characters render correctly and PAM unlocks upon Enter.

2. **Runtime Testing:**
   - Resolve tmpfs/ZFS incompatibility for XDG_RUNTIME_DIR.
   - Launch hikari on FreeBSD Wayland session.
   - Verify PAM non-blocking unlock (implemented — needs live test).

3. **API Verification:**
   - Verify `wlr_output_effective_resolution()` exists in installed wlroots 0.20 headers (`src/layer_shell.c:177` depends on it).

4. **Code Formatting:**
   - Run `clang-format` compliance check (TC-FORMAT-01).

## Completed Implementations

0. **Phase 18/18b Runtime Failure Investigation & Remediation:** ✅ Completed 2026-08-13.
   - Full static root-cause report (`.devdocs/INVESTIGATION_RUNTIME_FAILURE.md`): 4 P0 + 3 P1 + 8 P2 defects with file:line evidence; 3 further build-discovered defects fixed during validation.
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
