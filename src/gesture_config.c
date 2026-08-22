// Script function and purpose: Parses gesture binding keys (e.g.
// "swipe-left-3", "pinch-in-4", "hold-3") from the config's
// `inputs { gestures {} } ` section into their typed components.

#include <hikari/gesture_config.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

// Function purpose: Parse a gesture binding key of the form
// "swipe-<direction>-<fingers>", "pinch-<direction>-<fingers>", or
// "hold-<fingers>" into its typed components.
bool
hikari_gesture_binding_config_key_parse(const char *str,
    enum hikari_gesture_type *type,
    enum hikari_gesture_direction *direction,
    uint32_t *fingers)
{
  char buf[64];

  size_t len = strlen(str);
  if (len >= sizeof(buf)) {
    return false;
  }

  // Action purpose: strtok() silently collapses consecutive delimiter
  // characters and ignores a delimiter at either end of the string, which
  // would let malformed keys like "swipe--left-3" or "swipe-left-3-" parse
  // as if they were well-formed. Reject those shapes up front, before
  // tokenization ever begins.
  if (len == 0 || str[0] == '-' || str[len - 1] == '-' ||
      strstr(str, "--") != NULL) {
    return false;
  }

  strcpy(buf, str);

  char *type_str = strtok(buf, "-");
  char *dir_str = strtok(NULL, "-");
  char *fingers_str = strtok(NULL, "-");
  // Action purpose: Capture a 4th token so trailing garbage after the
  // expected fields (e.g. "swipe-left-3-9") is rejected instead of silently
  // discarded.
  char *extra_str = strtok(NULL, "-");

  if (type_str == NULL) {
    return false;
  }

  if (!strcmp(type_str, "swipe")) {
    *type = HIKARI_GESTURE_SWIPE;

    if (dir_str == NULL || fingers_str == NULL || extra_str != NULL) {
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

    if (dir_str == NULL || fingers_str == NULL || extra_str != NULL) {
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

    // Action purpose: "hold" has no direction token, so its finger-count is
    // the second token rather than the third; a present third token (e.g.
    // "hold-3-4") is trailing garbage and must be rejected.
    if (dir_str == NULL || fingers_str != NULL) {
      return false;
    }
    fingers_str = dir_str;
  } else {
    return false;
  }

  // Action purpose: strtoul() skips leading whitespace and accepts a
  // leading '+'/'-' sign, which would let malformed keys like "swipe-left-
  // +3" or "swipe-left- 3" parse as finger count 3. Require fingers_str to
  // be non-empty and consist solely of ASCII digits before ever calling
  // strtoul(), so only a bare decimal count is accepted.
  for (const char *c = fingers_str; *c != '\0'; c++) {
    if (*c < '0' || *c > '9') {
      return false;
    }
  }

  if (*fingers_str == '\0') {
    return false;
  }

  errno = 0;
  char *end;
  unsigned long value = strtoul(fingers_str, &end, 10);

  // Action purpose: Reject 0, unparseable/overflowing values, and anything
  // above 5 -- no supported touchpad/touchscreen gesture requires more
  // simultaneous touchpoints than that.
  if (*end != '\0' || errno != 0 || value == 0 || value > 5) {
    return false;
  }

  *fingers = (uint32_t)value;

  return true;
}
