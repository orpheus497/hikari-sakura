// Script function and purpose: Declarations for parsed gesture-binding
// configuration entries -- the typed (type, direction, fingers, action)
// tuple produced from an `inputs { gestures { "<key>" = <action> } }` config
// entry, plus the key-string parser used to build it.

#if !defined(HIKARI_GESTURE_CONFIG_H)
#define HIKARI_GESTURE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include <wayland-util.h>

#include <hikari/action.h>

enum hikari_gesture_type {
  HIKARI_GESTURE_SWIPE,
  HIKARI_GESTURE_PINCH,
  HIKARI_GESTURE_HOLD
};

enum hikari_gesture_direction {
  HIKARI_GESTURE_DIRECTION_NONE,
  HIKARI_GESTURE_DIRECTION_UP,
  HIKARI_GESTURE_DIRECTION_DOWN,
  HIKARI_GESTURE_DIRECTION_LEFT,
  HIKARI_GESTURE_DIRECTION_RIGHT,
  HIKARI_GESTURE_DIRECTION_IN,
  HIKARI_GESTURE_DIRECTION_OUT
};

struct hikari_gesture_binding_config {
  struct wl_list link;

  enum hikari_gesture_type type;
  enum hikari_gesture_direction direction;
  uint32_t fingers;

  struct hikari_action action;
};

bool
hikari_gesture_binding_config_key_parse(const char *str,
    enum hikari_gesture_type *type,
    enum hikari_gesture_direction *direction,
    uint32_t *fingers);

#endif
