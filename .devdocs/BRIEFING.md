# Hikari Project Briefing

*Last Updated:* 2026-07-29 15:32

## Current Status
- **Phase:** wlr_scene migration (near complete)
- **Branch:** wlroots-0.17.1
- **Overall progress:** ~80% (scene migration complete for all visual elements, build untested)
- **Target OS:** FreeBSD 13.x/14.x+ exclusively

## What Works
- FreeBSD evdev headers done.
- Standard `hikari_malloc` allocation implemented system-wide.
- `wlr_scene` rendering for borders (`wlr_scene_rect`), lock indicator (`wlr_scene_buffer`), backgrounds (`wlr_scene_buffer`), indicator bars (`wlr_scene_buffer`), and indicator frames (`wlr_scene_rect`).
- XDG views and XWayland views both have `scene_tree` with border and indicator frame nodes.
- Makefile targets wlroots-0.20 via pkg-config.
- Stub files (`pool.c`, `pool.h`, `renderer.c`, `renderer.h`) deleted.

## What Was Removed
- Object pool allocator.
- Custom renderer pipeline.
- DOD SoA tables.
- All `struct hikari_renderer` forward declarations.

## Blocker
- FreeBSD build verification: Requires native FreeBSD environment with `bmake`.
