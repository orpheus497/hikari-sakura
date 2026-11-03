#include <hikari/position_config.h>
#include <hikari/config_lua.h>

#include <string.h>

void
hikari_position_config_init(struct hikari_position_config *position_config)
{
  position_config->type = HIKARI_POSITION_CONFIG_TYPE_AUTO;
}

void
hikari_position_config_set_absolute(
    struct hikari_position_config *position_config, int x, int y)
{
  position_config->type = HIKARI_POSITION_CONFIG_TYPE_ABSOLUTE;

  position_config->config.absolute.x = x;
  position_config->config.absolute.y = y;
}

void
hikari_position_config_set_relative(
    struct hikari_position_config *position_config,
    enum hikari_position_config_relative relative_position)
{
  position_config->type = HIKARI_POSITION_CONFIG_TYPE_RELATIVE;

  position_config->config.relative = relative_position;
}

bool
hikari_position_config_relative_parse(
    struct hikari_position_config *position_config, const char *str)
{
  bool success = false;
  enum hikari_position_config_relative relative_config;

  if (!strcmp("bottom-left", str)) {
    relative_config = HIKARI_POSITION_CONFIG_RELATIVE_BOTTOM_LEFT;
    success = true;
  } else if (!strcmp("bottom-middle", str)) {
    relative_config = HIKARI_POSITION_CONFIG_RELATIVE_BOTTOM_MIDDLE;
    success = true;
  } else if (!strcmp("bottom-right", str)) {
    relative_config = HIKARI_POSITION_CONFIG_RELATIVE_BOTTOM_RIGHT;
    success = true;
  } else if (!strcmp("center", str)) {
    relative_config = HIKARI_POSITION_CONFIG_RELATIVE_CENTER;
    success = true;
  } else if (!strcmp("center-left", str)) {
    relative_config = HIKARI_POSITION_CONFIG_RELATIVE_CENTER_LEFT;
    success = true;
  } else if (!strcmp("center-right", str)) {
    relative_config = HIKARI_POSITION_CONFIG_RELATIVE_CENTER_RIGHT;
    success = true;
  } else if (!strcmp("top-left", str)) {
    relative_config = HIKARI_POSITION_CONFIG_RELATIVE_TOP_LEFT;
    success = true;
  } else if (!strcmp("top-middle", str)) {
    relative_config = HIKARI_POSITION_CONFIG_RELATIVE_TOP_MIDDLE;
    success = true;
  } else if (!strcmp("top-right", str)) {
    relative_config = HIKARI_POSITION_CONFIG_RELATIVE_TOP_RIGHT;
    success = true;
  } else {
    hikari_config_error(NULL, "failed to parse \"position\" \"%s\"", str);
  }

  if (success) {
    hikari_position_config_set_relative(position_config, relative_config);
  }

  return success;
}

static bool
parse_position(lua_State *L, int idx, int *x, int *y)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  int64_t ret_x = 0;
  int64_t ret_y = 0;
  bool parsed_x = false;
  bool parsed_y = false;

  hikari_config_iter_init(&iter, L, idx);
  while (hikari_config_iter_next(&iter, L, &key, "a position block")) {
    if (!strcmp(key, "x")) {
      if (!hikari_config_int(L, iter.vidx, "a position block", key, &ret_x)) {
        goto done;
      }

      parsed_x = true;
    } else if (!strcmp(key, "y")) {
      if (!hikari_config_int(L, iter.vidx, "a position block", key, &ret_y)) {
        goto done;
      }

      parsed_y = true;
    } else {
      hikari_config_error(
          "a position block", "unknown \"position\" key \"%s\"", key);
      goto done;
    }
  }

  /* Action purpose: the loop also ends when a malformed entry abandoned the
  walk, and only iter.failed tells that apart from reaching the end of the
  block. Without this test a reported error would be followed by a report of
  the two coordinates missing, and then by success. */
  if (iter.failed) {
    goto done;
  }

  if (!parsed_x) {
    hikari_config_error(
        "a position block", "missing \"x\"-coordinate in \"position\"");
    goto done;
  }

  if (!parsed_y) {
    hikari_config_error(
        "a position block", "missing \"y\"-coordinate in \"position\"");
    goto done;
  }

  success = true;

  *x = ret_x;
  *y = ret_y;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

bool
hikari_position_config_absolute_parse(
    struct hikari_position_config *position_config, lua_State *L, int idx)
{
  int x;
  int y;
  bool success = false;

  if (!parse_position(L, idx, &x, &y)) {
    hikari_config_error(NULL, "failed to parse \"position\"");
    goto done;
  }

  hikari_position_config_set_absolute(position_config, x, y);

  success = true;

done:

  return success;
}

bool
hikari_position_config_parse(
    struct hikari_position_config *position_config, lua_State *L, int idx)
{
  int type = lua_type(L, idx);
  const char *relative;
  bool success = false;

  switch (type) {
    case LUA_TTABLE:
      if (!hikari_position_config_absolute_parse(position_config, L, idx)) {
        goto done;
      }
      break;

    case LUA_TSTRING:
      if (!hikari_config_string(L, idx, NULL, "position", &relative) ||
          !hikari_position_config_relative_parse(position_config, relative)) {
        goto done;
      }
      break;

    default:
      hikari_config_error(NULL,
          "expected string or table for \"position\", got %s",
          lua_typename(L, type));
      goto done;
  }

  success = true;

done:

  return success;
}
