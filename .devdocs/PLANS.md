# Forward Strategy & Plans

*Last Updated:* 2026-07-31 01:15

## Completed
1. ~~Complete wlr_scene migration~~ ✓
2. ~~Clean up dead files (pool.c, pool.h, renderer.c, renderer.h)~~ ✓
3. ~~wlroots 0.20 API migration~~ ✓ (7 breaking changes resolved)
4. ~~Build verification~~ ✓ (clean `make` with wlroots 0.20)
5. ~~Code documentation compliance~~ ✓ (Swept modified source files for `AGENTS.md` comment prefix standards)

## Next Steps
1. **Runtime testing:** Launch `hikari` on a FreeBSD Wayland session with `seatd`. Verify compositor startup, view management, tiling, sheet switching, and lock/unlock cycle.
2. **PAM verification:** Test `hikari-unlocker` with OpenPAM on FreeBSD. Verify screen lock and unlock with correct credentials.
3. **Official changelog:** Prepare CHANGELOG.md and UPDATING.md entries for the wlroots 0.20 release (deferred pending user decision on version number).
