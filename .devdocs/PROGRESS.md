# Project Progress tracking

*Last Updated:* 2026-08-13 05:41 (environment clock, corroborated by build mtimes)

| Phase | Description | Status |
|---|---|---|
| **Phase 18** | Runtime failure root-cause investigation | 100% ✓ — full report `.devdocs/INVESTIGATION_RUNTIME_FAILURE.md`. Found 4 release-blockers + 3 P1 + 8 P2 with file:line evidence. Earlier "93-99% wired" and TC-BUILD-01 claims superseded — see report §6. |
| **Phase 18b** | Remediation execution & build revalidation | 100% ✓ — all 15 defects + 3 build-discovered ones (popup geometry, xcb size hints, xwayland associate lifecycle) fixed across 12 files. Default `etc/hikari/hikari.conf` authored (parser-verified). TC-BUILD-01 passed (default clean build); TC-BUILD-02 passed (full-feature clean build+link) — first time feature configs ever compiled in this tree. Runtime TTY test pending. |
| **Phase 1** | Initialization & workspace setup | 100% ✓ |
| **Phase 2** | Documentation & codebase audit | 100% ✓ |
| **Phase 3** | FreeBSD header adaptations | 100% ✓ |
| **Phase 4** | wlroots 0.20 API migration | 100% ✓ (all API breaking changes resolved) |
| **Phase 5** | wlr_scene rendering migration | 100% ✓ (all visual elements use wlr_scene graph) |
| **Phase 6** | Build verification (initial) | 100% ✓ (clean `make` at time of Phase 6 — revalidation tracked under Phase 12) |
| **Phase 7a** | Runtime startup fixes | 100% ✓ (resolved black screen, scene output order, environment setup) |
| **Phase 7b** | FreeBSD runtime validation | Pending (crash fix applied — awaiting retest on FreeBSD Wayland session) |
| **Phase 8** | AGENTS.md code documentation compliance | Partial — amended 2026-08-13: `[COMMENT]` prefixes applied to modified files only; only 10/57 sources carry the mandated script-purpose header; full rollout tracked in TODOS.md |
| **Phase 9** | Runtime crash fix & final validation | 100% ✓ (removed `wlr_xdg_surface_ping`, fixed fullscreen handler, fixed unlocker framing) |
| **Phase 10** | wlroots 0.20 initial_commit lifecycle | 100% ✓ (commit listener relocated, initial_commit handler added, popup lifecycle added, review fixes applied) |
| **Phase 11** | Startup wiring deep investigation | 100% ✓ (5/7 bugs fixed; BUG-6 non-blocking PAM resolved in Phase 12; BUG-7 no action needed) |
| **Phase 12** | XDG/tmpfs/ZFS resolution & PAM fixes | 100% ✓ — All code changes complete. PAM config fixed. Non-blocking PAM I/O implemented. ZFS detection in start-hikari.sh. README updated. |
| **Phase 13** | Codebase wiring audit, bug fixes & handbook verification | 100% ✓ — Full 55-file/64-header audit scored 93% correct. Fixed: switch toggle cascading if, output cairo surface check, duplicate includes in server.c, blocking `wait()` → `waitpid(WNOHANG)` in lock_mode.c, `output->server` init robustness, main.c comment migration. FreeBSD Handbook Ch.6 cross-reference verified. |
| **Phase 14** | Comprehensive audit bug fixes & dead code cleanup | 100% ✓ — Fixed: BUG-1 move_resize_view dx/dy, BUG-2 outputs_disabled stale state, BUG-3 command.c waitpid, BUG-4 stale comment. Security: explicit_bzero. Robustness: write() check. Added 5 missing listener cleanups. Removed dead code (render.h deleted, mode_handler, unused struct members). Updated desktop entry and gitignore. |
| **Phase 15** | Review fix: start-hikari.sh binary resolution | 100% ✓ — Added SCRIPT_DIR derivation so sibling hikari binary is found regardless of CWD. Three-tier lookup: ${SCRIPT_DIR}/hikari → PATH → ./hikari. |
| **Phase 16** | Review fix: SCRIPT_DIR guard & README tmpfs check | 100% ✓ — Added fatal error guard on SCRIPT_DIR derivation; replaced `stat -f '%T'` with `mount` in README tmpfs verification step. |
| **Phase 17** | Review fix: table pipe escaping & README tmpfs troubleshooting clarity | 100% ✓ — Escaped literal pipes in SESSION_HANDOFF Phase 16 table (markdownlint column miscount); README `zfs` diagnosis broadened to all setup steps incl. fstab + reboot. Build revalidated (user-confirmed 2026-08-13). |


*Note: DOD SoA tables and object pool phases were implemented and subsequently REVERTED as incompatible with wlr_scene workflows.*
