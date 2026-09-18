// Script function and purpose: Lifecycle wrapper for a physical touchscreen
// (WLR_INPUT_DEVICE_TOUCH) input device -- tracks it on the server's touch
// list and tears it down cleanly when the underlying device is unplugged.

#include <hikari/touch.h>

#include <hikari/cursor.h>
#include <hikari/memory.h>
#include <hikari/server.h>

// Function purpose: React to the device disappearing (unplug) by finalising
// and freeing its hikari_touch wrapper.
static void
destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_touch *touch = wl_container_of(listener, touch, destroy);

  /* A touch device can be removed mid-touch (unplug, driver reset) with no
  touch_up/touch_cancel ever delivered for whatever point it was driving.
  Without this, a primary touch latched to this device would stay latched
  forever, permanently disabling touch-driven focus/move/resize compositor
  -wide, since nothing else would ever clear it. Touch IDs aren't
  correlated to a specific device here, so this can't distinguish "this
  device's touch" from "some other device's touch" -- it releases whatever
  is currently latched, trading a rare, harmless early release of an
  unrelated touch for eliminating a permanent stuck state. */
  hikari_cursor_release_primary_touch(&hikari_server.cursor);

  hikari_touch_fini(touch);
  hikari_free(touch);
}

// Function purpose: Track a newly attached touch device on
// server->touches and listen for its destroy signal. Output confinement
// (wlr_cursor_map_input_to_output) is handled separately by the caller
// (add_touch() in server.c), not here.
void
hikari_touch_init(
    struct hikari_touch *touch, struct wlr_input_device *device)
{
  touch->device = device;

  wl_list_insert(&hikari_server.touches, &touch->server_touches);

  touch->destroy.notify = destroy_handler;
  wl_signal_add(&device->events.destroy, &touch->destroy);
}

// Function purpose: Remove the touch device's listener and unlink it from
// server->touches.
void
hikari_touch_fini(struct hikari_touch *touch)
{
  wl_list_remove(&touch->destroy.link);
  wl_list_remove(&touch->server_touches);
}
