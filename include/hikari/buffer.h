#if !defined(HIKARI_BUFFER_H)
#define HIKARI_BUFFER_H

#include <wlr/types/wlr_buffer.h>

/* [COMMENT] Function purpose: Wrap a copy of caller-rendered ARGB8888 pixels in
a CPU-backed wlr_buffer suitable for wlr_scene_buffer_create().

This exists because wlroots exposes no allocator a compositor can write pixels
into. The only public entry points are wlr_allocator_autocreate() and
wlr_allocator_create_buffer(); there is no public shm/CPU allocator, and a
GBM-backed buffer cannot be mapped for writing. Every wlroots compositor that
draws its own UI therefore supplies a wlr_buffer_impl of its own, and this is
hikari's. It is NOT a FreeBSD workaround -- see BLUEPRINT.md section 13, FB-2,
which corrects the earlier record on that point.

`stride` is in bytes and must be at least width * 4. The pixels are copied, so
the caller keeps ownership of `data` and may destroy its cairo surface as soon
as this returns. The result is owned by the caller, who must release it with
wlr_buffer_drop() once the scene node holds its own reference.

Returns NULL on degenerate geometry, on a stride that would over-read `data`,
on size overflow, or on allocation failure. Callers are expected to skip that
one UI element's repaint rather than treat NULL as fatal. */
struct wlr_buffer *
hikari_buffer_create_argb8888(
    int width, int height, const unsigned char *data, int stride);

#endif
