# Analysis Results
*Last Updated:* 2026-07-31 05:01

Codebase state and build status:
- Build Status: Clean compilation and linking of both `hikari` and `hikari-unlocker` against `wlroots` 0.20
- Rendering: `wlr_scene` graph (custom renderer removed)
- Memory: Standard `hikari_malloc` heap allocation (custom pool allocator removed)
- View flags: Local `uint16_t` field on `struct hikari_view`
- FreeBSD: Native evdev headers, `epoll-shim` conditional in Makefile
- wlroots: Targeting 0.20 via `pkg-config` (`-I/usr/local/include/wlroots-0.20 -lwlroots-0.20`)

See [BRIEFING.md](../BRIEFING.md) for current project status.
