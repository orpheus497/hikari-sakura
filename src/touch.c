#include <hikari/touch.h>

#include <hikari/memory.h>
#include <hikari/server.h>

static void
destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_touch *touch = wl_container_of(listener, touch, destroy);

  hikari_touch_fini(touch);
  hikari_free(touch);
}

void
hikari_touch_init(
    struct hikari_touch *touch, struct wlr_input_device *device)
{
  touch->device = device;

  wl_list_insert(&hikari_server.touches, &touch->server_touches);

  touch->destroy.notify = destroy_handler;
  wl_signal_add(&device->events.destroy, &touch->destroy);
}

void
hikari_touch_fini(struct hikari_touch *touch)
{
  wl_list_remove(&touch->destroy.link);
  wl_list_remove(&touch->server_touches);
}
