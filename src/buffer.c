// [COMMENT] Script function and purpose: The compositor's single CPU-backed
// wlr_buffer implementation. Everything hikari draws itself with cairo/Pango --
// the top bar, the indicator bars, the lock indicator, the output background --
// reaches the scene graph through this one file.

#include <hikari/buffer.h>

#include <string.h>

#include <drm_fourcc.h>

#include <wlr/interfaces/wlr_buffer.h>

#include <hikari/memory.h>

struct hikari_argb8888_buffer {
  struct wlr_buffer base;
  unsigned char *data;
  uint32_t format;
  size_t stride;
};

static void
argb8888_buffer_destroy(struct wlr_buffer *wlr_buffer)
{
  struct hikari_argb8888_buffer *buffer =
      wl_container_of(wlr_buffer, buffer, base);
  hikari_free(buffer->data);
  hikari_free(buffer);
}

static bool
argb8888_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
    uint32_t flags,
    void **data,
    uint32_t *format,
    size_t *stride)
{
  struct hikari_argb8888_buffer *buffer =
      wl_container_of(wlr_buffer, buffer, base);

  // [COMMENT] Action purpose: The pixels are a snapshot owned by the buffer;
  // callers re-render and create a new buffer rather than mutating this one.
  if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) {
    return false;
  }

  *data = buffer->data;
  *format = buffer->format;
  *stride = buffer->stride;

  return true;
}

static void
argb8888_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer)
{}

static const struct wlr_buffer_impl argb8888_buffer_impl = {
  .destroy = argb8888_buffer_destroy,
  .begin_data_ptr_access = argb8888_buffer_begin_data_ptr_access,
  .end_data_ptr_access = argb8888_buffer_end_data_ptr_access,
};

struct wlr_buffer *
hikari_buffer_create_argb8888(
    int width, int height, const unsigned char *data, int stride)
{
  // [COMMENT] Action purpose: Reject degenerate geometry and guard the size
  // computation against overflow before allocating.
  if (width <= 0 || height <= 0 || stride <= 0 || data == NULL) {
    return NULL;
  }

  // [COMMENT] Action purpose: ARGB8888 is 4 bytes/pixel; a stride shorter than
  // that would make the flat memcpy below (stride * height bytes) read past
  // the end of the source buffer. Guard the width*4 multiplication against
  // overflow before comparing.
  size_t min_stride = (size_t)width * 4;
  if (min_stride / 4 != (size_t)width || (size_t)stride < min_stride) {
    return NULL;
  }

  size_t byte_count = (size_t)stride * (size_t)height;
  if (byte_count / (size_t)stride != (size_t)height) {
    return NULL;
  }

  // [COMMENT] Action purpose: Graceful-degradation allocation. This helper's
  // contract is already "return NULL on failure" (see the geometry/overflow
  // guards above), and every caller (hikari_bar_refresh, hikari_indicator_bar_
  // update, hikari_output_load_background) already handles a NULL return by
  // skipping that one UI element's repaint rather than crashing -- aborting
  // here would have defeated that contract for the one failure mode that
  // actually matters. See DECISIONS_LOG Finding 4.
  struct hikari_argb8888_buffer *buffer =
      hikari_try_malloc(sizeof(struct hikari_argb8888_buffer));
  if (buffer == NULL) {
    return NULL;
  }

  unsigned char *buffer_data = hikari_try_malloc(byte_count);
  if (buffer_data == NULL) {
    hikari_free(buffer);
    return NULL;
  }

  wlr_buffer_init(&buffer->base, &argb8888_buffer_impl, width, height);
  buffer->format = DRM_FORMAT_ARGB8888;
  buffer->stride = (size_t)stride;
  buffer->data = buffer_data;
  memcpy(buffer->data, data, byte_count);

  return &buffer->base;
}
