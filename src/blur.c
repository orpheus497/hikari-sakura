// [COMMENT] Script function and purpose: Separable box blur over ARGB8888
// pixels, used to blur the workspace snapshot behind the lock screen. CPU-only
// and deliberately so -- it runs once per lock, not per frame.

#include <hikari/blur.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <hikari/memory.h>

/* [COMMENT] Function purpose: One horizontal box-blur pass over a single row,
using a running sum so the cost per pixel does not grow with the radius.

The window is clamped at both edges rather than wrapped or zero-padded: wrapping
would bleed the right edge of the screen into the left, and zero-padding would
darken the borders into a vignette. Clamping extends the edge pixel, which is
what makes a blurred screenshot look like a blurred screenshot.

`src` and `dst` must not overlap, and their strides are given separately in
pixels. That separation is what lets one routine serve both passes: the
horizontal pass walks a contiguous row (stride 1 for both), while the vertical
pass walks a column in the image (stride = row stride in pixels) and writes into
a compact scratch line (stride 1). Sharing one stride between source and
destination would make the vertical pass write past the end of that scratch. */
static void
box_blur_line(const unsigned char *src,
    unsigned char *dst,
    int length,
    int src_stride,
    int dst_stride,
    int radius)
{
  const int window = radius * 2 + 1;

  uint32_t sum[4] = { 0, 0, 0, 0 };

  // [COMMENT] Action purpose: Prime the running sum with the window centred on
  // the first pixel, clamping everything left of the start onto pixel 0.
  for (int i = -radius; i <= radius; i++) {
    int index = i < 0 ? 0 : (i >= length ? length - 1 : i);
    const unsigned char *pixel = src + (size_t)index * src_stride * 4;

    for (int c = 0; c < 4; c++) {
      sum[c] += pixel[c];
    }
  }

  for (int i = 0; i < length; i++) {
    unsigned char *out = dst + (size_t)i * dst_stride * 4;

    for (int c = 0; c < 4; c++) {
      out[c] = (unsigned char)(sum[c] / (uint32_t)window);
    }

    // [COMMENT] Action purpose: Slide the window one pixel right -- subtract
    // the sample leaving the trailing edge, add the one entering the leading
    // edge, both clamped to the line.
    int leaving = i - radius;
    int entering = i + radius + 1;

    leaving = leaving < 0 ? 0 : (leaving >= length ? length - 1 : leaving);
    entering = entering < 0 ? 0 : (entering >= length ? length - 1 : entering);

    const unsigned char *out_pixel = src + (size_t)leaving * src_stride * 4;
    const unsigned char *in_pixel = src + (size_t)entering * src_stride * 4;

    for (int c = 0; c < 4; c++) {
      sum[c] -= out_pixel[c];
      sum[c] += in_pixel[c];
    }
  }
}

bool
hikari_blur_argb8888(unsigned char *data,
    int width,
    int height,
    int stride,
    int radius,
    int passes)
{
  if (data == NULL || width <= 0 || height <= 0 || stride < width * 4) {
    return false;
  }

  if (radius <= 0 || passes <= 0) {
    // [COMMENT] Action purpose: A zero radius is a legitimate way to ask for no
    // blur, so this is success with nothing to do rather than an error.
    return true;
  }

  /* [COMMENT] Action purpose: Cap the radius to the image. A radius wider than
  the picture is not wrong -- the clamping above handles it -- but it makes the
  running sum span the whole line for every pixel, so the result is a flat
  average and the work is wasted. */
  int max_radius = (width < height ? width : height) / 2;
  if (max_radius < 1) {
    max_radius = 1;
  }
  if (radius > max_radius) {
    radius = max_radius;
  }

  /* [COMMENT] Action purpose: One scratch line, reused for every row and every
  column, sized to the longer of the two. box_blur_line() cannot work in place
  because the running sum reads source samples the output has already
  overwritten. */
  int longest = width > height ? width : height;
  unsigned char *scratch = hikari_try_malloc((size_t)longest * 4);
  if (scratch == NULL) {
    return false;
  }

  for (int pass = 0; pass < passes; pass++) {
    // [COMMENT] Action purpose: Horizontal pass -- each row is contiguous, so
    // the pixel stride is 1.
    for (int y = 0; y < height; y++) {
      unsigned char *row = data + (size_t)y * stride;

      box_blur_line(row, scratch, width, 1, 1, radius);
      memcpy(row, scratch, (size_t)width * 4);
    }

    /* [COMMENT] Action purpose: Vertical pass -- the same routine walking a
    column, which is what makes the blur separable: two one-dimensional passes
    instead of a two-dimensional convolution. The pixel stride is the row
    stride expressed in pixels, so this requires the stride to be a whole
    number of pixels, which it is for ARGB8888. */
    int column_stride = stride / 4;

    for (int x = 0; x < width; x++) {
      unsigned char *column = data + (size_t)x * 4;

      box_blur_line(column, scratch, height, column_stride, 1, radius);

      for (int y = 0; y < height; y++) {
        memcpy(column + (size_t)y * stride, scratch + (size_t)y * 4, 4);
      }
    }
  }

  hikari_free(scratch);

  return true;
}
