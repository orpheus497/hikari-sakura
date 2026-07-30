# Granular Task List

*Last Updated:* 2026-07-29 15:16

- [ ] **Scene migration:** Verify all rendering paths use `wlr_scene` (specifically check `indicator_frame.c` and any remaining damage calls).
- [ ] **Damage ring migration:** Remove manual `wlr_damage_ring_add_whole` and `wlr_damage_ring_add` calls from `hikari_output_damage_whole()` and `hikari_output_add_effective_surface_damage()`. Once all visual elements are scene graph nodes, `wlr_scene_output_commit` handles damage automatically. Manual calls reach into `scene_output->damage_ring` which is an internal implementation detail (verified against tinywl and labwc — neither uses manual damage ring calls).
- [ ] **Dead file cleanup:** Delete `pool.c`, `pool.h`, `renderer.c`, and `renderer.h` stub files or remove them from the repository tracking.
- [ ] **Build:** Validate the `Makefile` with FreeBSD `bmake`.
- [ ] **API:** Verify if `wlr_session` removal is needed for wlroots 0.20 and implement if so.
- [ ] **PAM:** Verify `hikari-unlocker` works correctly with OpenPAM setuid 4555.
- [ ] **Docs:** Perform an `AGENTS.md` compliance sweep on all remaining source files.
