# Architectural and Structural Decisions Log

*Note: Most recent entries are listed at the top.*

---

## [2026-08-13 19:08] Phase 26: Phase 24 Hardening Backlog Completed — P2/P3 Batch Executed

*(Timestamp source: `date '+%Y-%m-%d %H:%M'` command.)*

* **Context:** User approved the full remaining Phase 24 backlog in order: P3 changelog typos → P2 CSD granular damage → P2 allocation hardening with fail-fast wrappers (the allocation-policy design question was resolved by the user in favour of fail-fast).
* **Decision:** (1) `CHANGELOG.md` `wloots` → `wlroots` (2 sites: 0.15.0 and 0.14.0 entries). (2) CSD damage TODOs resolved in `src/view.c`: `damage_whole_surface` now damages the CSD main surface by its buffer extents — client-drawn decorations/shadows live inside the client buffer, so the surface box is the correct granular region and CSD views carry no server border box — and both `hikari_view_damage_whole` and `hikari_view_damage_surface` lost their whole-output early-outs, unifying CSD onto the same per-surface granular path as SSD. Verified safe against the post-scene architecture: every damage sink reduces to `wlr_output_schedule_frame` (`include/hikari/output.h:83`, `include/hikari/output.h:103`, `src/output.c:136`), so hikari-level boxes are advisory; unification also gives CSD the pre-existing SSD mapped-view contract (`hikari_view_for_each_surface` asserts `surface != NULL`), which all damage-whole callers already satisfy (map/unmap handlers damage before `surface` is cleared). (3) Allocation policy implemented fail-fast: `hikari_malloc`/`hikari_calloc` emit a sized `error:` diagnostic on stderr and `abort()` on NULL (`src/memory.c`). `abort()` chosen over `exit()` — allocation failure is bug-class, not clean-shutdown; SIGABRT yields a core dump for postmortem and skips atexit handlers on a half-valid heap. No zero-size normalization: FreeBSD `malloc(0)`/`calloc(0, …)` never return NULL and the tree is FreeBSD-only. `hikari_free` keeps free(3) semantics. `src/memory.c` and `include/hikari/memory.h` gained the AGENTS.md-mandated comment headers documenting the never-NULL contract.
* **Impact:** Phase 24 hardening stream closed at 7/7. TC-BUILD-01 (default) and TC-BUILD-02 (full-feature) clean builds pass with 0 errors under `env -u DEBUG`; edited files warning-clean (only the pre-existing documented `xwayland_unmanaged_view.c` unused-function warnings remain). Callsite NULL checks are now unreachable-but-harmless; previously unchecked callsites are safe. Remaining queue: user-run Phase 19 diagnostics, eDP-1 swapchain (environmental), tmpfs/ZFS `XDG_RUNTIME_DIR`, runtime-blocked items (P2-14, PAM, layer-client spot check), TC-FORMAT-01, optional comment-header rollout, cosmetic enum-compare warnings.

---

## [2026-08-13 18:05] Phase 25: Phase 24 Hardening Backlog — P0/P1 Batch Executed

*(Timestamp source: `date '+%Y-%m-%d %H:%M'` command.)*

* **Context:** User approved execution of the Phase 24 P0/P1 hardening batch (4 items) with edits, commands, and devdocs updates permitted.
* **Decision:** (1) Unknown `outputs` keys now fail the parse — `goto done` added to the unknown-key branch in `parse_output_config` (`src/configuration.c`), matching the strict behaviour of every other unknown-key branch in the parser; previously a typo'd key (e.g. "postion") logged but the configuration loaded successfully, silently ignoring the intended rule. (2) `parse_switches` now frees its UCL iterator at the `done:` label (`ucl_object_iterate_free`, `src/configuration.c`), matching all sibling parsers — fixes a per-load/SIGHUP-reload leak. (3) The lock-helper child no longer `exit(0)` after a failed `execl("hikari-unlocker")`; it writes `error: could not execute hikari-unlocker` to stderr and calls `_exit(EXIT_FAILURE)` (`src/lock_mode.c`). `_exit` (not `exit`) because the forked child shares the compositor address space and must skip atexit handlers/stdio flushing; stderr (fd 2) survives the stdin/stdout pipe rewiring; the parent already treats pipe hangup as a terminal locker failure. (4) The failed initial modeset commit in `hikari_output_init` now names the output on stderr before the early return (`src/output.c`) — same silent-zombie class as the fixed P0-2 backend-start guard, whose `fprintf` style it matches; `<stdio.h>` included explicitly.
* **Impact:** TC-BUILD-01 (default) and TC-BUILD-02 (full-feature) clean builds pass with 0 errors under `env -u DEBUG`; the three edited files compile warning-clean. The eDP-1 swapchain failure path will now identify the failed output on stderr at the next runtime test. Remaining Phase 24 items: P2 CSD damage granularity, P2 allocation-policy decision (fail-fast wrappers vs caller checks — pending user input), P3 changelog typos.

---

## [2026-08-13 17:08] Phase 24: Deep Wiring Audit Ingested into Devdocs (Docs-Only)

*(Timestamp source: `date '+%Y-%m-%d %H:%M'` command.)*

* **Context:** User requested that the full deep analysis be persisted in `.devdocs/` per `AGENTS.md` after an extensive static audit of documentation and implementation wiring.
* **Decision:** Recorded the audit across all devdocs trackers with zero product-code edits. Canonical verdict: core compositor wiring is concrete and operational across startup, output/scene, input/mode dispatch, config/action parser, and FreeBSD launcher/PAM/session boundaries. No simulated or fake subsystem implementations were found in active code paths. Empty callbacks were reviewed and classified as predominantly intentional modal no-op handlers (input suppression), not unimplemented feature stubs.
* **Actionable backlog captured:**
  1. Unknown `outputs` configuration keys currently log but do not fail parse (`src/configuration.c:1432` path).
  2. `parse_switches` iterator lifecycle needs explicit free (`src/configuration.c:1260` block).
  3. Lock helper child process uses `exit(0)` after failed `execl("hikari-unlocker")` (`src/lock_mode.c:118-119`).
  4. Failed output commit path remains too quiet in the modeset failure branch (`src/output.c:350-353`).
  5. Two TODO-tagged CSD damage paths over-damage whole output instead of granular regions (`src/view.c:558`, `src/view.c:1746`).
  6. Allocation wrappers are pass-through; many callers assume success (reliability hardening opportunity under memory pressure).
* **Documentation drift noted:** `CHANGELOG.md` still contains `wloots` typo entries while the code/docs/build target wlroots 0.20.
* **Impact:** Devdocs now contain the complete analysis and remediation plan in the mandated 7-file structure. No runtime or source behavior changed in this phase.

---

## [2026-08-13 16:50] Phase 23: Review-Findings Verification — 6 Fixed, 4 Skipped as Stale

*(Timestamp source: `date '+%Y-%m-%d %H:%M'` command.)*

* **Context:** User supplied 10 review findings (documentation + code) with a verify-first directive: fix only still-valid issues, skip the rest with reasons, keep changes minimal, validate.
* **Decision:** Every finding re-verified against the current tree before any edit. Fixed: (1) `version.h` rule gained a phony `FORCE` prerequisite plus write-to-`version.h.tmp`/atomic-rename (the old comment claimed atomicity that was never implemented); (2) `binding_config.c` numeric mouse bindings validate `strtol` via end pointer — no-digits and trailing-junk specs rejected, errno + UINT32 checks retained; (3) `layer_shell.c` popup damage offsets use the flat `base->geometry` (tree's 0.20 convention, matching `xdg_view.c`; both fields confirmed present in installed wlroots 0.20.2 headers); (4) `xwayland_view.c` validates `wlr_scene_tree_create` and bails through the destroy path's cleanup (`hikari_view_fini` + `hikari_free`, both verified safe before listener registration) — the caller holds no reference; (5) wallpaper PNG authored (1920x1080 8-bit gradient on the config's own `0x282C34` background; the cairo loader at `src/output.c:76` needs 8-bit) and the install rule made unconditional; (6) 17 missing `[COMMENT] Function purpose:` headers added across 8 files (`init_noop_output` already compliant). Skipped as stale: "future-dated timestamps" (system clock 16:50 postdates all stamps, max 14:35; no date >= 2026-08-14 anywhere in `.devdocs/`; newest-first ordering intact); `INVESTIGATION_RUNTIME_FAILURE.md` (retired in Phase 22); PLANS/BRIEFING `wlr_output_effective_resolution` duplicates (already removed in Phase 22 — only audit annotations remain); SESSION_HANDOFF line-referenced records (stale line numbers; historical timestamps are past, sequential, and provenance-noted — retroactively re-sourcing them would falsify the sequence).
* **Impact:** Default and full-feature clean builds pass with 0 errors; `version.h` regenerates every build with atomic rename and no residue; wallpaper installs to the config-referenced path. Historical ledger timestamps deliberately untouched; this entry's stamp is `date`-sourced.

---

## [2026-08-13 14:00] Phase 22: Devdocs Consolidation — Standalone Report Retired, 7-File Structure Restored

*(Timestamp source: environment clock — user barred shell commands this session.)*

* **Context:** User directive: too many bloated reports — consolidate into the AGENTS.md devdocs structure with no repetition, verifying everything against the actual codebase. The only file outside the mandated 7 was `the archived runtime investigation` (the Phase-20 analysis artifact had already been merged into BLUEPRINT.md and removed).
* **Decision:** Archived runtime investigation content was redistributed and redistributed its still-valid content with zero repetition: launcher/session architecture analysis → BLUEPRINT.md §6; corrected eDP-1 failure analysis → BLUEPRINT.md §5; residual open item P2-14 → TODOS active list; P2-15 → BLUEPRINT known limitations. The fixed-defect catalog remains recorded in the Phase 18/18b SESSION_HANDOFF and DECISIONS_LOG entries. All earlier `the archived runtime investigation` references in the historical ledgers (SESSION_HANDOFF, DECISIONS_LOG) are superseded pointers to these consolidated locations; living trackers (BRIEFING/PROGRESS/TODOS/PLANS/BLUEPRINT) were updated in place. During consolidation the Phase-20 BLUEPRINT §5 draft was found factually wrong — failure misattributed to `wlr_backend_start` (live-proven to succeed), a non-existent diagnostic string quoted (`error: failed to start backend`; actual: `error: could not start backend`, `src/server.c:1071`), and permissions/seatd listed as candidate causes though ruled out live in Phase 19 — and was corrected. Codebase re-verification this session: mlock/munlock present (`src/lock_mode.c:522/542`); double-fork+setsid exec (`src/command.c:14-21`); layer-shell exclusive zones (`src/layer_shell.c:88-172`); 26-mark registry (`src/mark.c:10-50`); sheet array (`include/hikari/workspace.h:22`).
* **Impact:** Devdocs are back to the mandated 7 files; the archived runtime investigation has been deleted after its content was redistributed. No product code changed.

---

## [2026-08-13 13:44] Phase 21: Launcher Duality Confirmed as Architecture; Report Validity Audit

* **Context:** User asked why both `start-hikari` and `hikari` exist, and whether a PAM-using Wayland compositor should "naturally and natively" resolve dbus, seatd, PAM, the XDG socket, and portals. Full evidence-backed analysis added to `the archived runtime investigation` §10–§11.
* **Decision:** The duality stands. Verified native in-tree: seat/seatd (`wlr_backend_autocreate` → libseat, `src/server.c:821`); Wayland socket (`wl_display_add_socket_auto`, `src/server.c:961`) with `WAYLAND_DISPLAY` exported to children (`src/server.c:967`) and `DISPLAY` under XWayland (`src/server.c:507`); PAM usage is auth-only (`pam_start`/`pam_authenticate`/`pam_end`, `hikari_unlocker.c:85/134/153`). Verified absent tree-wide (grep): any dbus usage; `pam_open_session`/`pam_setcred`/`pam_acct_mgmt`. The D-Bus session bus, portal activation environment (`XDG_CURRENT_DESKTOP`), and XDG_RUNTIME_DIR creation are session-layer responsibilities (login PAM stack/pam_xdg, dbus-daemon, display manager) that precede or surround the compositor; the Phase 11 `setup_env()` native-bootstrap experiment was correctly reverted 2026-07-31 12:47 and that revert is now re-affirmed on complete evidence rather than architectural appeal. The wrapper's blocks are conditional/idempotent — under a DM it reduces to a pass-through plus the dbus guard; on a bare TTY it supplies the missing link between the PAM login and the compositor.
* **Impact:** No product-code changes. Report §10 records the current validity of every Phase 18 finding (all P0/P1 remain fixed; P2-14 open pending a usable session; P2-15 present by design inheritance; §7 attributions superseded by Phase 19 live evidence). Residual open set unchanged: eDP-1 swapchain failure (environmental), output-commit silent-return hardening, tmpfs/ZFS XDG_RUNTIME_DIR, wallpaper PNG.

---

## [2026-08-13 07:34] Phase 19: Live Runtime Test — Failure Localized Below hikari; Diagnostics-First

* **Context:** First live TTY run after Phase 18b remediation (user-pasted logs; triage session was read-only). Two wlroots errors: `eglQueryDeviceStringEXT(EGL_DRM_DEVICE_FILE_EXT) failed` (non-fatal — dmabuf device feedback lost, clients degrade to wl_shm) and `Swapchain for output 'eDP-1' failed test` (output-fatal). Session, backend start, renderer, allocator, and connector probe verified working live for the first time.
* **Decision:** Classified as environmental/driver-layer (Mesa/EGL/GBM ↔ drm-kmod), not a hikari defect — the enable+mode commit sequence (`src/output.c:350`) matches the wlroots 0.20 contract. Ranked hypotheses: H1 (Mesa DRI/GBM broken — explains both log lines), H2 (`IN_FORMATS` modifier mismatch), H3 (FB-import EINVAL). Discrimination requires a `DEBUG=YES` rebuild (release compiles out `wlr_log_init(WLR_DEBUG)`, `main.c:236`) plus system checks — full matrix in TODOS. No product code changed. Run-1 anomaly (direct `./hikari`: no swapchain error, 28s idle) attributed to nested-backend selection from leaked display vars, which `start-hikari.sh:13-14` exists to prevent. Tabled optional hardening: loud diagnostic on the silent failed-commit return (`src/output.c:351-353`) — same zombie class as the fixed P0-2. Note: branch label `wlroots-0.17.1` is stale; the tree builds against installed wlroots 0.20.x.
* **Impact:** Runtime blocker queue re-ordered: (1) eDP-1 scanout swapchain failure, (2) tmpfs/ZFS `XDG_RUNTIME_DIR` (escalated — Error 1 forces clients onto wl_shm). Awaits user-run diagnostics before any remediation is proposed.

---

## [2026-08-13 05:41] Phase 18b: Remediation Execution & Build Revalidation

* **Context:** User approved the Phase 18 remediation plan. Execution covered all 9 plan steps plus the recorded P2 batch, followed by clean-tree build validation in both the default and full-feature configurations.
* **Decision:** Applied 14 fixes across 11 files (register in `the archived runtime investigation` §9). Notable engineering choices: (1) default `etc/hikari/hikari.conf` authored against the verified parser grammar — every action verb cross-checked against `src/action.c`, every colorscheme key against `parse_colorscheme`, `PREFIX` token retained for the install-time sed; (2) layer-shell scene integration parents layer surfaces at the scene root with z-order by layer class and layout-global positioning in `calculate_geometry()`; (3) xwayland map/unmap registration deferred to the `associate` event because `wlr_xwayland_surface.surface` is NULL at `new_surface` time under the 0.20 lifecycle; (4) popup geometry migrated to `popup->current.geometry` after the linker disproved `wlr_xdg_popup_get_geometry()` — verified against the installed 0.20 header's documented semantics.
* **Impact:** TC-BUILD-01 passed (default clean build, 0 errors); new TC-BUILD-02 passed (full-feature clean build + link, 0 errors). Three further stale-API defects (P1-16 popup geometry, P1-17 xcb_size_hints_t, P1-18 associate lifecycle) found and fixed during validation — the feature configurations had never compiled in this tree before. Compositor should now fail loudly (diagnostic + exit) instead of presenting a black screen with dead input when the backend cannot start.

---

## [2026-08-13 04:40] Phase 18: Runtime Failure Root-Cause Investigation

*(Timestamp source: environment clock — user declined shell command execution.)*

* **Context:** User reported that after login hikari either (A) crashes/fails or (B) loads to a black screen with dead keypresses and a frozen mouse, and directed an "extremely deep and analytical investigation" of wiring, false-vs-real logic, stubs, placeholders, simulations, simplifications, poor implementations, and hallucinations. Static-only investigation (no shell access this session); full evidence in `the archived runtime investigation`.
* **Decision:** Recorded 15 defects (4 P0, 3 P1, 8 P2) with file:line citations. P0-1: hallucinated `xkb_map_new_from_names` symbol (`src/keyboard_config.c:354`) — tree cannot link cleanly; deployed binary predates tree. P0-2: unchecked `wlr_backend_start()` (`src/server.c:1054`) — primary symptom-B root cause. P0-3: `wlr_headless_backend_create(server->display)` type error + false API comment (`src/server.c:853-857`) — contradicts the Phase-4 fix record in BRIEFING.md. P0-4: `etc/hikari/hikari.conf` and wallpaper asset missing though referenced by install/dist. P1: xkb-file type-tag lie, unstored numeric mouse bindings, layer shell never scene-attached. Documentation-only session: no product code touched; remediation plan (report §8) awaits user approval.
* **Impact:** Devdocs truth ledger corrected — TC-BUILD-01 back to Pending (clean-tree revalidation required), prior "93–99% wired" assessments superseded, BRIEFING status set to BLOCKED on 4 P0s. Runtime symptoms now have deterministic, testable attributions: A ← P0-1/P0-3/P0-4/P1-5; B ← P0-2 (primary), P0-4-empty-config (secondary).

---

## [2026-08-13 03:57] Phase 17b: Deep Codebase Wiring Verification & Devdocs Truth Corrections

* **Context:** User-directed independent verification of every engineering claim in the devdocs against the actual codebase: Makefile↔source↔header structure (exact 1:1, zero orphans), all 14 claimed fixes from Phases 4-16 (BUG-1/2/3, explicit_bzero, non-blocking PAM, switch else-if, wlroots 0.20 initial_commit lifecycle, 7-arg axis notify, preferred-mode output state, listener symmetry in `hikari_server_stop`, PAM/desktop/unlocker wiring, 11-mode init block). All verified present and correct in code. Three meta-claims were untrue: (1) Phase 8 "100% comment compliance" — only 10/57 sources carry the mandated script-purpose header; (2) BLUEPRINT modal index listed phantom `src/grab_keyboard_mode.c` (a 169-byte vestigial header with zero references tree-wide) while omitting the existing `src/dnd_mode.c`; (3) the `wlr_output_effective_resolution()` API-check TODO was stale — the successful user build proves the symbol exists.
* **Decision:** Amended the PROGRESS Phase 8 row to reflect actual scope; corrected the BLUEPRINT modal index (dropped phantom Grab Keyboard row, added DnD Mode row); closed the stale API-check and obsolete `.core`-cleanup TODOs; recorded the optional comment-header rollout as a deferred TODO. No code changes (user-approved scope).
* **Impact:** devdocs meta-claims now match the verified state of the codebase. Engineering claims were ~95% accurate; the codebase wiring itself is sound.

---

## [2026-08-13 02:29] Phase 17: Review Fixes — Markdown Table Pipes & README tmpfs Troubleshooting

* **Context:** Two review findings verified against current files. (1) `SESSION_HANDOFF.md` Phase 16 Modified Files table embedded unescaped literal pipes inside code spans (the `||` error guard and `mount | grep`), which GFM parses as column separators before inline code spans — markdownlint counted the rows as having extra columns. A repo-wide sweep confirmed these were the only two offending cells. (2) `README.md` attributed a `zfs` mount result for `/tmp` solely to step 1 (`canmount=noauto`), though a missing fstab entry (step 2) or skipped reboot (step 3) produces the identical symptom.
* **Decision:** Escaped the offending pipes (backslash-pipe) inside the table cells, preserving the exact shell syntax while restoring the two-column structure. Rewrote the README diagnosis to state `/tmp` is still ZFS-backed and to direct users to re-check every setup step, including `/etc/fstab` and the reboot.
* **Impact:** markdownlint-clean handoff ledger; troubleshooting guidance no longer misdiagnoses non-step-1 causes.

---

## [2026-08-11 11:42] Phase 16: Review Fixes — SCRIPT_DIR Guard & README tmpfs Verification

* **Context:** Two review findings verified against current code. (1) `start-hikari.sh` derived `SCRIPT_DIR` via a `cd`/`pwd` pipeline with no error handling — on failure the variable would be silently empty and the binary lookup would run with a blank prefix. (2) `README.md` documented `stat -f '%T' /tmp` as the tmpfs verification, which is macOS-BSD-only; on FreeBSD `%T` reports the file type, not the filesystem type.
* **Decision:** Appended a fatal `|| { echo ...; exit 1; }` guard to the `SCRIPT_DIR` assignment (POSIX-portable, no bash-isms). Replaced the verification command with `mount | grep ' on /tmp '` (spaced pattern avoids false matches such as `/tmp/hikari-runtime-1001`).
* **Impact:** Wrapper fails loudly instead of mis-resolving the binary; the documented verification now works on FreeBSD.

---

## [2026-08-02 13:23] Phase 15: start-hikari.sh SCRIPT_DIR Binary Resolution

* **Context:** Review finding identified that `start-hikari.sh` resolved the `hikari` binary by checking PATH first (`command -v hikari`), then `./hikari` relative to the caller's CWD. The `./hikari` fallback is fragile — it only works if the user's working directory is the build tree. The Makefile installs both `start-hikari` and `hikari` as siblings in `${PREFIX}/bin/`, so the script should look beside itself first.
* **Decision:** Added `SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)` to derive the wrapper's own directory. Changed resolution order to: `${SCRIPT_DIR}/hikari` (sibling) → PATH (`command -v hikari`) → `./hikari` (legacy edge case). Updated error message to include `${SCRIPT_DIR}` for diagnostics.
* **Impact:** Reliable binary resolution in both installed (`/usr/local/bin/start-hikari` + `/usr/local/bin/hikari`) and in-tree development (`./start-hikari.sh` + `./hikari`) scenarios, regardless of the caller's working directory.

---

## [2026-08-01 01:20] Phase 14: Comprehensive Codebase Audit — Bug Fixes and Cleanup

* **Context:** Deep file-by-file investigation of all 55 source files, 64 headers, Makefile, start-hikari.sh, hikari_unlocker.c, PAM config, and desktop entry. The audit verified wiring, memory handling, D-Bus/IPC/XDG systems, FreeBSD integration, wlroots 0.20 API compliance, and searched for stubs/placeholders/fake logic.

### BUG-1 (MEDIUM): `move_resize_view()` dx/dy confusion

* **File:** `src/server.c:1617`
* **Bug:** Both `lx` and `ly` added `dy`. The `lx` calculation should add `dx`. This caused incorrect output-crossing detection during resize-and-move operations (e.g., `decrease_view_size_right`, `increase_view_size_left`).
* **Fix:** Changed `+ dy` to `+ dx` in the `lx` calculation.

### BUG-2 (LOW): `outputs_disabled` stale state in lock mode

* **File:** `src/lock_mode.c`
* **Bug:** `outputs_disabled` was never initialized in `hikari_lock_mode_init()` and never reset in `cancel()`. After a lock-cancel cycle where outputs were disabled, re-entering lock mode could inherit stale state because `enable_outputs()` checks `!mode->outputs_disabled` and returns early.
* **Fix:** Added `outputs_disabled = false` in both `hikari_lock_mode_init()` and `cancel()`.

### BUG-3 (LOW): `command.c` waitpid infinite loop

* **File:** `src/command.c:24-31`
* **Bug:** The waitpid loop checked `errno == EINTR` unconditionally after `waitpid()`, but `errno` is only meaningful when `waitpid` returns `-1`. A stale `EINTR` from a prior syscall could cause an infinite loop.
* **Fix:** Replaced with `while (waitpid(child, &status, 0) == -1 && errno == EINTR) {}`.

### BUG-4 (LOW): Stale debug comment in server.c

* **File:** `src/server.c:451`
* **Bug:** `// CAN FAIL WITH NULL POINTER. HOW?` — misleading comment indicating an unresolved crash. `event->source` can be NULL (client clearing selection), which is valid.
* **Fix:** Removed the comment.

### Security: Password buffer zeroing

* **File:** `src/lock_mode.c:48`
* **Issue:** `memset(input_buffer, 0, BUFFER_SIZE)` could be optimized away by the compiler since the buffer is immediately reused. The unlocker correctly uses `explicit_bzero`.
* **Fix:** Replaced with `explicit_bzero(input_buffer, BUFFER_SIZE)`.

### Robustness: Unchecked pipe write

* **File:** `src/lock_mode.c:244`
* **Issue:** `write()` to the unlocker pipe had no return value check. If the pipe is broken or full, the password is silently lost.
* **Fix:** Added EINTR-retrying write with stderr warning on failure.

### Cleanup: Missing listener removal in `hikari_server_stop()`

* **File:** `src/server.c`
* **Issue:** `new_decoration`, `new_toplevel_decoration`, `new_layer_shell_surface`, `new_virtual_keyboard`, and `new_virtual_pointer` listeners were registered but never removed in `hikari_server_stop()`.
* **Fix:** Added `wl_list_remove()` for all five, with proper `#ifdef` guards.

### Cleanup: Dead code removal

* **Files:** `include/hikari/render.h` (deleted), `src/output.c`, `include/hikari/output.h`, `include/hikari/xdg_view.h`, `include/hikari/server.h`
* **Removed:** Empty render.h (vestige of removed renderer — deleted from disk), commented-out `mode_handler` block, commented-out `struct wl_listener mode` member, unused `request_move`/`request_resize`/`request_maximize` listener declarations in `hikari_xdg_view`, "DESTORY" typo → "DESTROY".
* **Migrated:** `server.h` comment prefixes from `##` to `[COMMENT]` per AGENTS.md.

### Desktop entry and gitignore

* Added `DesktopNames=Hikari` to `hikari.desktop` for XDG portal backend identification.
* Updated `.gitignore` with `*.core` wildcard, `compile_flags.txt`, and `.clangd`.

---

## [2026-07-31 20:38] Cleanup: Remove glibc-isms from hikari-unlocker and dead Linux PAM file

* **Context:** Full FreeBSD stack audit revealed `_GNU_SOURCE`, `_DEFAULT_SOURCE`, and a manual `void explicit_bzero(void *, size_t)` prototype in `hikari_unlocker.c`. These are glibc-specific — on FreeBSD, `explicit_bzero` is declared in `<strings.h>` (already included) without feature macros. Additionally, `etc/pam.d/hikari-unlocker.Linux` remains despite Linux support being removed from the project.
* **Decision:** Removed `_GNU_SOURCE`, `_DEFAULT_SOURCE` defines and the redundant prototype. The Linux PAM file has been deleted (`rm etc/pam.d/hikari-unlocker.Linux`).
* **Impact:** Cleaner FreeBSD-native code. No functional change — `explicit_bzero` was already available via `<strings.h>`.

---

## [2026-07-31 20:30] Fix: `xdg_surface->data` Type Confusion in Decoration Handler

* **Context:** `hikari_xdg_view_init()` sets `xdg_surface->data = xdg_view->scene_tree` (the wlroots popup parenting convention). However, `server_decoration_handler()` read `xdg_surface->data` as if it were a `hikari_xdg_view*`. Since it's actually a `wlr_scene_tree*`, every decoration event caused heap corruption or segfault by dereferencing a scene tree pointer as a view struct.
* **Decision:** Fixed the decoration handler to follow the correct lookup chain: `xdg_surface->data` → `scene_tree`, then `scene_tree->node.data` → `xdg_view`. Removed the dead store `xdg_surface->data = xdg_view` (line 536 in xdg_view.c) that was immediately overwritten.
* **Impact:** Eliminates crash-level type confusion on every server decoration negotiation.

## [2026-07-31 20:30] Fix: Layer Shell Popup Missing `initial_commit` Handler

* **Context:** In wlroots 0.20, all XDG surfaces (including popups spawned by layer shell surfaces like waybar) require `initial_commit` handling — the compositor must respond with `wlr_xdg_surface_schedule_configure()` on the first commit. The layer shell `commit_popup_handler()` only called `damage_popup()`, skipping this lifecycle step. Popups from layer shell clients could fail to map.
* **Decision:** Added `initial_commit` guard matching the existing XDG view popup handler pattern.
* **Impact:** Layer shell popups (e.g., waybar right-click menus) now correctly map in wlroots 0.20.

## [2026-07-31 20:30] Fix: Cairo Context Leak in `render_image_to_surface()`

* **Context:** `render_image_to_surface()` called `cairo_create(output)` then checked the image surface status with an early return on failure. The early return path did not call `cairo_destroy()`, leaking the cairo context.
* **Decision:** Added `cairo_destroy(cairo)` before the early return.
* **Impact:** No memory leak when loading an invalid PNG background.

## [2026-07-31 20:30] Fix: Noop/Headless Output Missing `wlr_output_init_render()`

* **Context:** `init_noop_output()` created a headless output for the fallback workspace but never called `wlr_output_init_render()`. The regular `new_output_handler()` does call it for real outputs. Without render initialization, any rendering path touching the noop output (e.g., running with no physical monitors) could fail.
* **Decision:** Added `wlr_output_init_render(wlr_output, server->allocator, server->renderer)` before `hikari_output_init()` in `init_noop_output()`.
* **Impact:** Noop/headless output can now safely handle rendering operations.

---

## [2026-07-31 20:14] Fix: Retryable vs Terminal Unlocker Lifecycle in Lock Mode

* **Context:** `hikari-unlocker` runs a `while (!success)` loop: on wrong password (`PAM_AUTH_ERR`) it writes `false`, stays alive, and reads the next password. The previous `locker_result_handler()` unconditionally reaped the child after any result, which would block-deadlock on a retryable `false` (child still alive waiting for stdin).
* **Decision:** Classify results as *terminal* (success, hangup-without-result, read failure) or *retryable* (got `false` result with child still alive). Only terminal results trigger `waitpid(locker_pid, &status, 0)` with EINTR retry and pipe cleanup. Retryable results just show the deny indicator — the child stays alive and `submit_password()` will send the next attempt. `start_unlocker()` now returns `bool`; `submit_password()` guards against `locker_pid <= 0` to prevent writing to invalid descriptors.
* **Impact:** Wrong-password retries no longer deadlock or orphan the unlocker. Fatal failures still guarantee child reaping and pipe cleanup.

## [2026-07-31 16:45] Fix: `output->server` Not Initialized in `hikari_output_init()`

* **Context:** `struct hikari_output` has a `server` field used by `frame_handler` (`output.c:263`: `output->server->scene`). This field was only set by the caller in `new_output_handler` (`server.c:226`), not inside `hikari_output_init()`. If any other code path called `hikari_output_init()` without setting `output->server`, a NULL dereference would occur.
* **Decision:** Added `output->server = &hikari_server;` inside `hikari_output_init()`. Since `hikari_server` is a global singleton, this is safe and idempotent with the caller's assignment.
* **Impact:** Defensive robustness — init is now self-contained.

## [2026-07-31 16:45] Fix: Duplicate `#include` Directives in `server.c`

* **Context:** `src/server.c` had duplicate `#include <wlr/types/wlr_data_device.h>` (lines 19, 31) and `#include <wlr/types/wlr_seat.h>` (lines 25, 32). No functional impact, but violates code hygiene standards.
* **Decision:** Removed the duplicate includes on lines 31-32.
* **Impact:** Cosmetic cleanup — no behavioral change.

---

## [2026-07-31 16:34] Fix: Switch Toggle Handler Cascading If Bug

* **Context:** Full codebase wiring audit discovered that `toggle_handler` in `src/switch.c` used two sequential `if` statements instead of `if/else if`. After the first block set `state = WLR_SWITCH_STATE_ON`, the second `if (state == ON)` immediately fired because there was no `else`. Both begin AND end actions executed on every toggle event.
* **Decision:** Changed the second `if` to `else if` so only one branch executes per toggle event.
* **Impact:** Switch-based operations (e.g., laptop lid toggle actions) now fire correctly — begin on OFF→ON, end on ON→OFF.

## [2026-07-31 16:34] Fix: Output Cairo Surface Status Check (Wrong Surface)

* **Context:** In `hikari_output_load_background()` (`src/output.c:85`), after creating `output_surface` via `cairo_image_surface_create`, the status check was `cairo_surface_status(image)` instead of `cairo_surface_status(output_surface)`. This meant an `output_surface` allocation failure would go undetected if `image` was valid.
* **Decision:** Changed to `cairo_surface_status(output_surface)`.
* **Impact:** Prevents use of a failed cairo surface for background rendering.

## [2026-07-31 16:17] Decision: Non-blocking PAM Authentication I/O (BUG-6 Resolved)

* **Context:** `submit_password()` in `lock_mode.c` used a synchronous `read(locker_pipe[1][0], &success, sizeof(bool))` that blocked the entire Wayland event loop during `pam_authenticate()`. PAM's `pam_unix.so` may delay 1-3 seconds on failure, freezing all rendering and input.
* **Decision:** Replaced blocking `read()` with `wl_event_loop_add_fd()`. A new `locker_result_handler()` callback fires asynchronously when `hikari-unlocker` writes the result boolean. The compositor event loop continues processing frames and input during authentication.
* **Implementation:** Added `locker_event_source` field to `struct hikari_lock_mode`. The fd source is registered per-submission and cleaned up in both the result handler and the `cancel()` path.
* **Impact:** Resolves BUG-6 from Phase 11. Compositor remains responsive during password verification.

---

## [2026-07-31 16:17] Decision: PAM Config — Use `auth include system` Instead of `auth include passwd`

* **Context:** `hikari-unlocker.FreeBSD` contained `auth include passwd`. Live system verification confirmed FreeBSD's `/etc/pam.d/passwd` explicitly states: "passwd(1) does not use the auth, account or session services." The file contains only a `password` stack (for changing passwords), not an `auth` stack. This means `pam_authenticate()` would always fail because OpenPAM finds no auth rules in the include chain.
* **Decision:** Changed to `auth include system`. The `/etc/pam.d/system` file contains the correct auth chain: `auth required pam_unix.so no_warn try_first_pass nullok`.
* **Impact:** Screen unlock authentication will now work correctly on FreeBSD.

---

## [2026-07-31 16:12] Research Finding: ZFS Automount Overrides fstab tmpfs on /tmp

* **Context:** Live system testing revealed that although `/etc/fstab` contains a `tmpfs /tmp` entry, `stat -f '%T' /tmp` reports `zfs`. The mount table shows BOTH `tmpfs on /tmp` and `zroot/tmp on /tmp` — ZFS automount runs after fstab and mounts the dataset on top of the tmpfs.
* **Technical Finding:** `posix_fallocate()` returns `EOPNOTSUPP (45)` on both `/var/run/user/1001` (ZFS) and `/tmp` (ZFS over tmpfs). However, `shm_open()` + `posix_fallocate()` and `memfd_create()` + `posix_fallocate()` both succeed — POSIX SHM and anonymous memory bypass ZFS entirely. wlroots 0.20 uses `shm_open()` (confirmed via `nm -D`), not filesystem-backed temp files.
* **Decision:** Fix is `sudo zfs set canmount=noauto zroot/tmp` (system admin, one command). Added ZFS detection warning to `start-hikari.sh` and expanded README with step-by-step instructions.
* **Impact:** Users with ZFS root will get actionable warnings instead of silent client failures.

---

## [2026-07-31 15:46] Research Finding: XDG_RUNTIME_DIR on ZFS Incompatible with Wayland

* **Context:** Deep investigation into whether mounting XDG on tmpfs works with ZFS or needs re-addressing. System analysis revealed FreeBSD 15.1-RELEASE with full ZFS root (`zroot`). `/var/run/user/1001` (set by `pam_xdg` via `/etc/pam.d/system`) resides on the root ZFS dataset. `/tmp` is also ZFS-backed (`zroot/tmp`). The only tmpfs mount on the system is `/compat/linux/dev/shm` (Linux compat layer).
* **Technical Finding:** ZFS on FreeBSD does not support `posix_fallocate()` — returns `EINVAL` (since FreeBSD r325320, 2017). ZFS's Copy-on-Write architecture cannot provide the pre-allocation guarantees POSIX requires. Wayland clients use `posix_fallocate()` to pre-allocate `wl_shm` shared memory buffers inside `XDG_RUNTIME_DIR`. When `XDG_RUNTIME_DIR` is on ZFS, these allocations fail, causing client crashes or rendering failures.
* **Impact on hikari:** The `start-hikari.sh` fallback to `/tmp/hikari-runtime-$UID` does not resolve the issue because `/tmp` is also ZFS. The fallback also never triggers because `pam_xdg` already sets `XDG_RUNTIME_DIR`. Runtime testing is blocked until this is resolved.
* **Decision:** Must implement a tmpfs mount for `XDG_RUNTIME_DIR`. Four options identified:
  - **Option A (Recommended):** Mount tmpfs at `/var/run/user` via `/etc/fstab`
  - **Option B:** Update `start-hikari.sh` to use a verified tmpfs-backed path (e.g., `/dev/shm`)
  - **Option C:** Replace `zroot/tmp` with tmpfs at `/tmp`
  - **Option D:** Mount tmpfs in the wrapper script (requires privileges)
* **Status:** Pending implementation — awaiting user direction on preferred option.

---

## [2026-07-31 14:49] Decision: wlr_session Ownership — Do Not Destroy Separately

* **Context:** `hikari_server_stop()` and `hikari_server_prepare_privileged()` error path both called `wlr_session_destroy()` after `wlr_backend_destroy()`. Reading the wlroots 0.20 `backend.h` header confirmed `wlr_backend_autocreate` creates a session that is **owned by the backend**. The `wlr_session` struct has an internal `event_loop_destroy` listener for cleanup. The tinywl 0.20 reference implementation never calls `wlr_session_destroy`. The double destroy is a use-after-free.
* **Decision:** Removed both `wlr_session_destroy` calls. The session is destroyed by the backend automatically.
* **Impact:** Eliminates crash/heap corruption on compositor shutdown.

## [2026-07-31 14:49] Decision: Use wlr_output_preferred_mode Instead of Manual First Mode

* **Context:** Output initialization manually picked the first mode from `wlr_output->modes` via `wl_container_of(wlr_output->modes.next, mode, link)`. This is not guaranteed to be the EDID-preferred mode. The tinywl reference uses `wlr_output_preferred_mode()`.
* **Decision:** Replaced with `wlr_output_preferred_mode(wlr_output)` which returns the mode flagged as preferred by the monitor.
* **Impact:** Ensures native resolution on monitors that report a preferred mode.

## [2026-07-31 14:49] Decision: Desktop File Should Use start-hikari Wrapper

* **Context:** `hikari.desktop` had `Exec=hikari` which bypasses the wrapper script that provides D-Bus session wrapping and XDG_RUNTIME_DIR bootstrapping. Display managers launching via this file would silently lack D-Bus, breaking portals/clipboard/secrets.
* **Decision:** Changed to `Exec=start-hikari`. Added `start-hikari` installation to Makefile.
* **Impact:** Display manager launches now get proper D-Bus session and XDG_RUNTIME_DIR.

---

## [2026-07-31 14:20] Decision: wlroots 0.20 Initial Commit Lifecycle Pattern

* **Context:** The compositor crashed with `Assertion failed: (surface->initialized)` in `wlr_xdg_surface_schedule_configure`. Deep analysis revealed this is NOT just a single bad call — it's a missing lifecycle pattern. In wlroots 0.20, the `new_toplevel` signal fires before the surface is initialized. The compositor must register a commit listener at `new_toplevel` time and handle `initial_commit` by calling `wlr_xdg_toplevel_set_size(0, 0)`, which sets `initialized = true`. Without this, the surface can never map, and any configure call will crash. Cross-referenced against tinywl 0.20.
* **Decision:** Moved commit listener registration from `map()` to `hikari_xdg_view_init()` (new_toplevel time). Added `initial_commit` guard at the top of `commit_handler` that calls `wlr_xdg_toplevel_set_size(0, 0)` and returns early. Added `popup_commit_handler` with `initial_commit` → `wlr_xdg_surface_schedule_configure`. Guarded `request_fullscreen_handler` with `surface->initialized` check. Added `commit` member to `hikari_xdg_popup` struct.
* **Impact:** Resolves the `surface->initialized` assertion crash. XDG surfaces now follow the correct wlroots 0.20 initialization handshake and can successfully map.

---

## [2026-07-31 12:47] Decision: Revert Native Environment Bootstrapping

* **Context:** The previous decision to inject `setup_env()` in `main.c` violated Wayland architectural standards. Compositors should not generate their own IPC bus (`XDG_RUNTIME_DIR`) or wrap themselves in `dbus-run-session` natively.
* **Decision:** Removed `setup_env()` from `main.c`. Added detailed diagnostic error messages to `server.c` for `wlr_backend_autocreate` failures. Created `start-hikari.sh` to handle dbus/XDG environment bootstrapping externally.
* **Impact:** `hikari` complies with proper `wlroots` daemon and wrapper architectures. C code is cleaner and adheres to the separation of concerns.

## [2026-07-31 12:21] Decision: Native Environment Bootstrapping [REVERTED]

* **Context:** `hikari` failed to run natively on FreeBSD, falling back to a nested Wayland session that caused assertion crashes because `seatd`, `dbus`, and `XDG_RUNTIME_DIR` were not configured properly.
* **Decision:** Implemented `setup_env()` in `src/main.c` before parsing options to dynamically generate `XDG_RUNTIME_DIR` if missing, encapsulate execution via `execvp("dbus-run-session", ...)`, and strictly unset `WAYLAND_DISPLAY` and `DISPLAY` to prevent accidental nesting.
* **Impact:** `hikari` is guaranteed to launch on native DRM/libinput backends and avoids wlroots Wayland-backend bugs.

## [2026-07-31 12:21] Decision: Remove Manual Damage Ring Hooks

* **Context:** Manual `wlr_damage_ring` logic was left as migration debt. `wlr_scene` handles surface damage implicitly.
* **Decision:** Removed `wlr_damage_ring_add_whole` and `wlr_damage_ring_add` from output utilities (`src/output.c`, `include/hikari/output.h`). Retained `wlr_output_schedule_frame`.
* **Impact:** Eliminates redundant damage tracking and aligns fully with `wlr_scene` architecture.

---

## Architectural Decisions

### Architecture: XDG Shell Surface Initialization

* **Context:** In wlroots 0.17+, `wlr_xdg_shell.events.new_surface` emits before the surface role (toplevel or popup) is assigned, causing `xdg_surface->toplevel` to be NULL and leading to segmentation faults when clients connect.
* **Decision:** Migrated `wlr_xdg_shell` event binding from `new_surface` to `new_toplevel`, guaranteeing that the surface is fully initialized as a toplevel before `hikari` processes it. Popups are already correctly handled internally via the toplevel's `new_popup` event.
* **Impact:** Prevents compositor crashes when XDG clients (like `foot`) map their windows.

### Architecture: Background Buffer Allocation (FreeBSD)

* **Context:** Forcing `DRM_FORMAT_MOD_LINEAR` during background buffer allocation caused `wlr_allocator_create_buffer` to fail or return CPU-unmappable buffers on FreeBSD's GBM backend, leading to a permanent black screen for the wallpaper.
* **Decision:** Removed hardcoded modifiers (`.len = 0, .modifiers = NULL`), aligning the background allocator with the UI text allocator (`indicator_bar.c`).
* **Impact:** Allows the allocator to implicitly select the optimal fallback (e.g., SHM), resolving the black screen without requiring a custom `wlr_buffer` implementation.



### Architecture: Scene Output Initialization Order

* **Context:** Moving to `wlr_scene` revealed a timing flaw where `wlr_output_layout_add` emitted signals causing frames to be scheduled *before* `scene_output` was created. This caused early frames to damage without a valid output backing, leading to a black screen and unresponsiveness.
* **Decision:** Moved `wlr_scene_output_create` and `wlr_scene_output_layout_add_output` to occur *before* `wlr_output_layout_add` inside `hikari_output_init`.
* **Impact:** Resolves compositor black-screen failures on startup, ensuring the damage ring is properly attached before layout changes trigger initial frames.

### API Migration: wlroots 0.20 Output State Management

* **Context:** wlroots 0.20 removed the implicit enablement and mode-setting from standard output signals.
* **Decision:** Adopted `wlr_output_state` and `wlr_output_commit_state` explicitly during `hikari_output_enable` and `hikari_output_disable`.
* **Impact:** Restores normal monitor power-management and resolution negotiation.

### API Migration: Preserve `xdg_surface->data = scene_tree` Convention

* **Context:** During code review, the assignment `xdg_surface->data = xdg_view->scene_tree` appeared to overwrite the `xdg_view` back-reference. Cross-referencing against tinywl (wlroots master) revealed this is the standard wlroots popup parenting convention: `xdg_surface->data` stores the scene_tree so `wlr_scene_xdg_surface_create` can find the parent scene node for popups via `parent->data`.
* **Decision:** Reverted the removal. `scene_tree->node.data = xdg_view` (for view lookup) and `xdg_surface->data = scene_tree` (for popup parenting) are on different objects and serve different purposes. Both are required.

---

## [2026-07-30 01:45] Decision: Track Manual Damage Ring Calls as Migration Debt [SUPERSEDED]

* **Context:** `hikari_output_damage_whole()` and `hikari_output_add_effective_surface_damage()` reach into `scene_output->damage_ring` directly. Verified against tinywl and labwc — neither uses manual damage ring calls when using `wlr_scene`. The scene graph handles damage tracking internally via `wlr_scene_output_commit`.
* **Decision:** Retain the manual calls for now (hikari is mid-migration, some damage sources may not be scene-managed). Tracked in TODOS.md for removal once all visual elements are scene graph nodes. *(Note: Manual damage ring calls were removed in the 2026-07-31 12:21 decision once scene graph migration was fully adopted.)*

---

## [2026-07-29 15:16] Decision: Revert DOD SoA Tables and Object Pool Allocator

* **Context:** The custom object pool allocator and DOD SoA view state/geometry tables added complexity without proven benefit. The wlr_scene migration made the custom renderer (which DOD optimized for) obsolete.
* **Decision:** Removed pool.c/pool.h, reverted view flags to local struct field, removed all dod_id/view_state indirection.

---

## [2026-07-29 15:16] Decision: Migrate Rendering to wlr_scene Graph

* **Context:** wlroots 0.18+ provides wlr_scene for automatic damage tracking and composition, eliminating the need for manual renderer passes.
* **Decision:** Gutted renderer.c, migrated borders to wlr_scene_rect nodes, lock indicator and backgrounds to wlr_scene_buffer nodes. Scene graph handles damage tracking and output composition automatically.

---

### Architecture: Continuous Quad Batch Rendering [SUPERSEDED]

* **Context:** Wayland rendering overhead via multiple wlroots API calls per frame impacted FreeBSD native performance.
* **Decision:** Implemented a single-pass `hikari_renderer` loop that buffers texture/color quads and flushes them in a single batch. Note: This was implemented but subsequently REVERTED as `wlr_scene` natively handles optimal rendering without requiring a manual batching pipeline.
* **Impact:** Removed 30+ internal API roundtrips per compositor frame.

---

### Architecture: Hybrid DOD (Data-Oriented Design) View State [SUPERSEDED]

* **Context:** The proliferation of linked lists for view states hampered cache coherency.
* **Decision:** Replaced scattered structs with a centralized SoA (Struct-of-Arrays) layout in `hikari_server`. Note: This was implemented but subsequently REVERTED as it was incompatible with wlroots 0.20's `wlr_scene` graph requirements.
* **Impact:** State mutations require table lookups rather than pointer dereferences.

---

## [2026-07-29 04:47] Decision: Sheet Pool Capacity & Array Contiguity [SUPERSEDED]

* **Context:** `hikari_workspace` allocates its 10 sheets simultaneously via `calloc(HIKARI_NR_OF_SHEETS, sizeof(struct hikari_sheet))` to hold them in a contiguous array format. Our Slab allocator traditionally manages single instances per block.
* **Decision:** To guarantee array contiguity without modifying `struct hikari_workspace` pointer mechanics or breaking `wl_list`, the `sheet_pool` `item_size` in `src/server.c` is initialized to `HIKARI_NR_OF_SHEETS * sizeof(struct hikari_sheet)`. A single allocation from the pool yields the contiguous block necessary for the workspace arrays.
* **Superseded by:** The object pool allocator (`pool.c`, `pool.h`) was removed entirely in the 2026-07-29 15:16 decision "Revert DOD SoA Tables and Object Pool Allocator". Sheet allocation now uses standard `hikari_malloc`/`calloc`.

---

## [2026-07-29 03:15] Decision: Data-Oriented Design (DOD) Orientation & FreeBSD Primary Target [SUPERSEDED in part]

* **Context:** The user requested modernizing the `hikari` Wayland compositor with primary focus on FreeBSD compatibility, thorough documentation inside `docs/`, and adoption of Data-Oriented Design (DOD) principles.
* **Decision:**
  1. (Historical Intent) Structure core data layouts (views, sheets, groups, outputs, tiles) into cache-aligned contiguous arrays / struct-of-arrays (SoA) where appropriate to minimize pointer chasing during render/layout loops. *Note: The DOD architecture was superseded by the wlr_scene migration.*
  2. Isolate FreeBSD platform integration requirements (`evdev`, `epoll-shim`, `tmpfs` `/tmp` `posix_fallocate`, PAM unlocker, `seatd`) in system setup documentation (`.devdocs/docs/freebsd_requirements.md`) and build definitions (`Makefile`).
  3. Strict adherence to `AGENTS.md` operational cycle: Ask → Explain → Justify → Wait for Approval → Execute.

---

## [2026-07-29 03:15] Decision: Devdocs Separation of Concerns

* **Context:** `AGENTS.md` mandates absolute separation of AI tracking docs (`.devdocs/`) from product documentation (`docs/`) and code in root.
* **Decision:** Keep all operational and tracking files inside `.devdocs/` and user/product technical documentation inside `.devdocs/docs/`.

---

## Design Implementation Requests

### 1. Non-Blocking PAM I/O for `hikari-unlocker` (Bug 6) [SUPERSEDED]

* **Status:** Fully implemented (2026-07-31 16:17). See decision entry "Non-blocking PAM Authentication I/O (BUG-6 Resolved)" above.
* **Resolution:** Child-process fork with pipe IPC and `wl_event_loop_add_fd()` callback. All tabled design questions resolved by the implementation.
* **Remaining:** Live-test PAM non-blocking unlock on FreeBSD target to confirm end-to-end behavior.

### 2. PAM Verification (`hikari-unlocker`)

* **Context / Clarification:** The unlocker requires root privileges to read `/etc/master.passwd` via OpenPAM on FreeBSD. We must ensure the binary is owned by `root:wheel` and has the `4555` setuid bit. Testing this requires a live FreeBSD system; it cannot be simulated inside an unprivileged sandbox.
* **Tabled Questions:**
  * Q: Are there specific native FreeBSD testing harnesses for Wayland surfaces we should use instead of manual Wayland clients?
