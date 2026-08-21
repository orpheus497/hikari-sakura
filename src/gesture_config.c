#include <hikari/gesture_config.h>

#include <errno.h>
#include <string.h>

// [COMMENT] Function purpose: Parse a gesture binding key of the form
// "swipe-<direction>-<fingers>", "pinch-<direction>-<fingers>", or
// "hold-<fingers>" into its typed components.
bool
hikari_gesture_binding_config_key_parse(const char *str,
    enum hikari_gesture_type *type,
    enum hikari_gesture_direction *direction,
    uint32_t *fingers)
{
  char buf[64];

  if (strlen(str) >= sizeof(buf)) {
    return false;
  }

  strcpy(buf, str);

  char *type_str = strtok(buf, "-");
  char *dir_str = strtok(NULL, "-");
  char *fingers_str = strtok(NULL, "-");

  if (type_str == NULL) {
    return false;
  }

  if (!strcmp(type_str, "swipe")) {
    *type = HIKARI_GESTURE_SWIPE;

    if (dir_str == NULL || fingers_str == NULL) {
      return false;
    } else if (!strcmp(dir_str, "up")) {
      *direction = HIKARI_GESTURE_DIRECTION_UP;
    } else if (!strcmp(dir_str, "down")) {
      *direction = HIKARI_GESTURE_DIRECTION_DOWN;
    } else if (!strcmp(dir_str, "left")) {
      *direction = HIKARI_GESTURE_DIRECTION_LEFT;
    } else if (!strcmp(dir_str, "right")) {
      *direction = HIKARI_GESTURE_DIRECTION_RIGHT;
    } else {
      return false;
    }
  } else if (!strcmp(type_str, "pinch")) {
    *type = HIKARI_GESTURE_PINCH;

    if (dir_str == NULL || fingers_str == NULL) {
      return false;
    } else if (!strcmp(dir_str, "in")) {
      *direction = HIKARI_GESTURE_DIRECTION_IN;
    } else if (!strcmp(dir_str, "out")) {
      *direction = HIKARI_GESTURE_DIRECTION_OUT;
    } else {
      return false;
    }
  } else if (!strcmp(type_str, "hold")) {
    *type = HIKARI_GESTURE_HOLD;
    *direction = HIKARI_GESTURE_DIRECTION_NONE;

    // [COMMENT] Action purpose: "hold" has no direction token, so its
    // finger-count is the second token rather than the third.
    fingers_str = dir_str;

    if (fingers_str == NULL) {
      return false;
    }
  } else {
    return false;
  }

  errno = 0;
  char *end;
  unsigned long value = strtoul(fingers_str, &end, 10);

  if (*end != '\0' || errno != 0 || value == 0) {
    return false;
  }

  *fingers = (uint32_t)value;

  return true;
}
