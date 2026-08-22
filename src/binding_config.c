#include <hikari/binding_config.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <dev/evdev/input-event-codes.h>

#include <wlr/types/wlr_keyboard.h>

static bool
parse_modifier_mask(const char *str, uint8_t *result, const char **remaining)
{
  size_t len = strlen(str);
  uint8_t mask = 0;
  int pos;

  for (pos = 0; pos < len; pos++) {
    char c = str[pos];
    if (c == '-' || c == '+') {
      break;
    } else if (c == 'L') {
      mask |= WLR_MODIFIER_LOGO;
    } else if (c == 'S') {
      mask |= WLR_MODIFIER_SHIFT;
    } else if (c == 'A') {
      mask |= WLR_MODIFIER_ALT;
    } else if (c == 'C') {
      mask |= WLR_MODIFIER_CTRL;
    } else if (c == '5') {
      mask |= WLR_MODIFIER_MOD5;
    } else if (c == '0') {
      // do nothing
    } else {
      fprintf(stderr,
          "configuration error: unknown modifier \"%c\" in \"%s\"\n",
          c,
          str);
      return false;
    }
  }

  *result = mask;
  if (remaining != NULL) {
    *remaining = str + pos;
  }

  return true;
}

bool
hikari_binding_config_key_parse(
    struct hikari_binding_config_key *binding_key, const char *str)
{
  bool success = false;
  const char *remaining;

  if (!parse_modifier_mask(str, &binding_key->modifiers, &remaining)) {
    goto done;
  }

  if (*remaining == '-') {
    errno = 0;
    char *end;
    const long value = strtol(remaining + 1, &end, 10);

    /* [COMMENT] Action purpose: Reject an empty parse, trailing characters, and
    anything below the X11 keycode offset. Without the end pointer "12abc" and
    "abc" (which strtol reports as 0 with errno untouched) were both accepted,
    and any value under 8 made the subtraction below wrap: keycode 0 became
    4294967288. */
    if (errno != 0 || end == remaining + 1 || *end != '\0' || value < 8 ||
        value > UINT32_MAX) {
      fprintf(stderr,
          "configuration error: failed to parse keycode \"%s\"\n",
          remaining + 1);
      goto done;
    }

    binding_key->type = HIKARI_ACTION_BINDING_KEY_KEYCODE;
    binding_key->value.keycode = (uint32_t)value - 8;
  } else if (*remaining == '+') {
    xkb_keysym_t value =
        xkb_keysym_from_name(remaining + 1, XKB_KEYSYM_CASE_INSENSITIVE);
    if (value == XKB_KEY_NoSymbol) {
      fprintf(stderr,
          "configuration error: unknown key symbol \"%s\"\n",
          remaining + 1);
      goto done;
    }

    binding_key->type = HIKARI_ACTION_BINDING_KEY_KEYSYM;
    binding_key->value.keysym = value;
  } else {
    // [COMMENT] Action purpose: str is a bare modifier mask with no
    // "-keycode" or "+keysym" suffix (e.g. "L" instead of "L+space").
    // Every other failure path above this one reports a specific cause;
    // without this, the caller's own load-failure message is the only
    // diagnostic ever printed, silently hiding the actual line at fault.
    fprintf(
        stderr, "configuration error: invalid key binding \"%s\"\n", str);
    goto done;
  }

  success = true;

done:

  return success;
}

/* [COMMENT] Function purpose: Parse a mouse binding spec -- modifier mask
plus a named button ("L+left") or a '-'-prefixed numeric evdev button code
("L-272") -- into a binding key for the mouse bindings table. */
bool
hikari_binding_config_button_parse(
    struct hikari_binding_config_key *binding_key, const char *str)
{
  bool success = false;
  const char *remaining;

  if (!parse_modifier_mask(str, &binding_key->modifiers, &remaining)) {
    goto done;
  }

  if (*remaining == '+') {
    binding_key->type = HIKARI_ACTION_BINDING_KEY_KEYCODE;

    remaining++;

    if (!strcmp(remaining, "left")) {
      binding_key->value.keycode = BTN_LEFT;
    } else if (!strcmp(remaining, "right")) {
      binding_key->value.keycode = BTN_RIGHT;
    } else if (!strcmp(remaining, "middle")) {
      binding_key->value.keycode = BTN_MIDDLE;
    } else if (!strcmp(remaining, "side")) {
      binding_key->value.keycode = BTN_SIDE;
    } else if (!strcmp(remaining, "extra")) {
      binding_key->value.keycode = BTN_EXTRA;
    } else if (!strcmp(remaining, "forward")) {
      binding_key->value.keycode = BTN_FORWARD;
    } else if (!strcmp(remaining, "back")) {
      binding_key->value.keycode = BTN_BACK;
    } else if (!strcmp(remaining, "task")) {
      binding_key->value.keycode = BTN_TASK;
    } else {
      fprintf(stderr,
          "configuration error: unknown mouse button \"%s\"\n",
          remaining);
      goto done;
    }
  } else if (*remaining == '-') {
    binding_key->type = HIKARI_ACTION_BINDING_KEY_KEYCODE;

    remaining++;

    errno = 0;
    char *end = NULL;
    const long value = strtol(remaining, &end, 10);
    /* [COMMENT] Action purpose: Reject specs with no digits (end pointer
    unchanged) or trailing characters (end not at the terminating NUL) --
    strtol alone would silently accept "272abc" or a bare "-". The errno and
    UINT32 range validation is retained. */
    if (errno != 0 || end == remaining || *end != '\0' || value < 0 ||
        value > UINT32_MAX) {
      fprintf(stderr,
          "configuration error: failed to parse mouse binding \"%s\"\n",
          remaining);
      goto done;
    }

    /* [COMMENT] Action purpose: Store the parsed numeric button code. The value
    was previously validated and then discarded, leaving keycode zero-initialised
    so numeric mouse bindings (e.g. "L-272") could never fire. Mouse button codes
    are raw evdev codes -- no xkb +8 offset like keyboard keycodes. */
    binding_key->value.keycode = (uint32_t)value;
  } else {
    goto done;
  }

  success = true;

done:

  return success;
}
