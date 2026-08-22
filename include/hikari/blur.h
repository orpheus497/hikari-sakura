#if !defined(HIKARI_BLUR_H)
#define HIKARI_BLUR_H

#include <stdbool.h>

/* [COMMENT] Function purpose: Blur ARGB8888 pixels in place.

Three passes of a box blur approximate a Gaussian closely enough that the
difference is invisible at these radii, and a box blur is separable and runs in
time independent of the radius -- a true Gaussian convolution at radius 12 would
be roughly two orders of magnitude slower for no perceptible gain. This runs
once, when the screen is locked, not per frame.

`stride` is in bytes. Alpha is blurred along with the colour channels, which is
correct here because the source is an opaque screen capture. Returns false only
on bad geometry or an allocation failure for the scratch row; the caller should
then show the unblurred capture rather than nothing. */
bool
hikari_blur_argb8888(unsigned char *data,
    int width,
    int height,
    int stride,
    int radius,
    int passes);

#endif
