#if !defined(HIKARI_SCREEN_CAPTURE_H)
#define HIKARI_SCREEN_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>

struct hikari_output;

/* [COMMENT] Class purpose: A snapshot of one output's composited contents, in
straight-through ARGB8888 CPU memory.

`data` is owned by the snapshot and released by hikari_screen_capture_fini().
Dimensions are in physical pixels, matching the output's current mode rather
than its logical size, so a snapshot of a scaled output is captured at full
resolution and scaled down by the scene node that displays it. */
struct hikari_screen_capture {
  unsigned char *data;
  int width;
  int height;
  int stride;
};

/* [COMMENT] Function purpose: Render the scene as the given output currently
sees it into CPU memory, without committing anything to the display.

This is what makes the lock screen show the workspace the user was actually
looking at. It must be called while the output is still enabled and before lock
mode hides anything -- afterwards there is nothing left to photograph.

Returns false and leaves `capture` untouched when the renderer cannot satisfy
the request; the caller is expected to fall back to a plain backdrop rather
than treat that as fatal. */
bool
hikari_screen_capture_init(
    struct hikari_screen_capture *capture, struct hikari_output *output);

void
hikari_screen_capture_fini(struct hikari_screen_capture *capture);

#endif
