# Forward Strategy & Plans

*Last Updated:* 2026-07-29 15:16

1. **Complete wlr_scene migration:** Verify all rendering paths go through the scene graph. Ensure no residual legacy rendering calls remain.
2. **Clean up dead files:** Remove `pool.c`, `pool.h`, `renderer.c`, and `renderer.h` from the repository entirely (currently they are 1-line stubs).
3. **Remove wlr_session:** Remove `wlr_session` from `hikari_server` if it is confirmed that wlroots 0.20 does not require it.
4. **FreeBSD build verification:** Validate the build using FreeBSD `bmake`.
5. **Runtime testing:** Perform functional runtime testing exclusively on FreeBSD.
6. **Code documentation compliance:** Ensure complete compliance with the `AGENTS.md` comment prefixes.
