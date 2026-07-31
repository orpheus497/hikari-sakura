# Granular Task List

*Last Updated:* 2026-07-31 13:16

- [x] **Scene migration:** Verify all rendering paths use `wlr_scene` (borders, lock indicator, backgrounds, indicator bars, indicator frames — all confirmed).
- [x] **Damage ring migration:** Remove manual `wlr_damage_ring_add_whole` and `wlr_damage_ring_add` calls. Confirmed removed; only `wlr_output_schedule_frame` remains.
- [x] **Dead file cleanup:** Deleted `pool.c`, `pool.h`, `renderer.c`, and `renderer.h` stub files.
- [x] **Build:** `make` completes cleanly — both `hikari` and `hikari-unlocker` compile and link against wlroots 0.20.
- [x] **API:** wlroots 0.20 API migration complete — 7 breaking changes resolved across 5 source files.
- [x] **Docs:** Performed `AGENTS.md` compliance prefix update on all modified source files.
- [x] **Native Environment:** Reverted false `setup_env()` implementation in `main.c`. Environment bootstrapping correctly relegated to `start-hikari.sh` wrapper script.
- [x] **Runtime:** Added explicit diagnostic messaging in `server.c` for `wlr_backend_autocreate` failures.
- [x] **Audit:** Full wlroots 0.20 + FreeBSD architecture audit completed (2026-07-31 13:08).
- [x] **CRITICAL FIX:** `clock_gettime` return value misused in `hikari_server_cursor_focus` — `server.c:439`. Fixed: extract from `now.tv_sec`/`now.tv_nsec`.
- [x] **CRITICAL FIX:** `wlr_drm_format` initialized with internal `.capacity` field — fixed to zero-init + `.format` only. `output.c`, `indicator_bar.c`, `lock_indicator.c`.
- [x] **MEDIUM FIX:** `wlr_xcursor_manager_load` hardcoded scale=1. Fixed: load 1 and 2 at init; per-output scale load in `hikari_output_init`.
- [x] **MEDIUM FIX:** Unsafe `wl_container_of` on `wlr_surface` in `server_decoration_handler` removed. `server.c`.
- [x] **LOW FIX:** `#if HAVE_XWAYLAND` → `#ifdef HAVE_XWAYLAND`. `server.c`.
- [x] **LOW FIX:** `start-hikari.sh` now resolves binary via `$PATH` with `./hikari` fallback for dev builds.
- [ ] **PAM:** Verify `hikari-unlocker` works correctly with OpenPAM setuid 4555.
