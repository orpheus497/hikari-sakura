// Script function and purpose: Declarations for hikari's touchscreen device
// wrapper -- tracks a physical WLR_INPUT_DEVICE_TOUCH input device for its
// lifetime and attaches it to the compositor's cursor/output layout.

#if !defined(HIKARI_TOUCH_H)
#define HIKARI_TOUCH_H

#include <wlr/types/wlr_input_device.h>
#include <wayland-util.h>

struct hikari_touch {
  struct wl_list server_touches;
  struct wlr_input_device *device;
  struct wl_listener destroy;
};

void
hikari_touch_init(
    struct hikari_touch *touch, struct wlr_input_device *device);

void
hikari_touch_fini(struct hikari_touch *touch);

#endif
