# Granular Task List

*Last Updated:* 2026-07-31 12:47

- [x] **Scene migration:** Verify all rendering paths use `wlr_scene` (borders, lock indicator, backgrounds, indicator bars, indicator frames — all confirmed).
- [x] **Damage ring migration:** Remove manual `wlr_damage_ring_add_whole` and `wlr_damage_ring_add` calls from `hikari_output_damage_whole()` and `hikari_output_add_effective_surface_damage()`. Once all visual elements are scene graph nodes, `wlr_scene_output_commit` handles damage automatically. Manual calls reach into `scene_output->damage_ring` which is an internal implementation detail (verified against tinywl and labwc — neither uses manual damage ring calls).
- [x] **Dead file cleanup:** Deleted `pool.c`, `pool.h`, `renderer.c`, and `renderer.h` stub files.
- [x] **Build:** `make` completes cleanly — both `hikari` and `hikari-unlocker` compile and link against wlroots 0.20.
- [x] **API:** wlroots 0.20 API migration complete — 7 breaking changes resolved across 5 source files.
- [x] **Docs:** Performed `AGENTS.md` compliance prefix update on all modified source files (`cursor.c`, `output.c`, `server.c`, `switch.c`, `xdg_view.c`).
- [x] **Native Environment:** Reverted false `setup_env()` implementation in `main.c`. Environment bootstrapping and `dbus-run-session` are correctly relegated to `start-hikari.sh` wrapper script per wlroots guidelines.
- [x] **Runtime:** Added explicit diagnostic messaging in `server.c` for `wlr_backend_autocreate` failures to instruct users to run `seatd` and export `XDG_RUNTIME_DIR`.
- [ ] **PAM:** Verify `hikari-unlocker` works correctly with OpenPAM setuid 4555.
