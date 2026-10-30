#include <hikari/view_config.h>

#include <assert.h>
#include <stdlib.h>

#include <hikari/config_lua.h>
#include <hikari/geometry.h>
#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/sheet.h>
#include <hikari/view.h>

static void
init_properties(struct hikari_view_properties *properties)
{
  properties->group_name = NULL;
  properties->sheet_nr = -1;
  properties->mark = NULL;
  properties->focus = false;
  properties->invisible = false;
  properties->floating = false;
  properties->publicview = false;

  hikari_position_config_init(&properties->position);
}

void
hikari_view_config_init(struct hikari_view_config *view_config)
{
  view_config->app_id = NULL;

  init_properties(&view_config->properties);

  view_config->child_properties = &view_config->properties;
}

static void
fini_properties(struct hikari_view_properties *properties)
{
  hikari_free(properties->group_name);
}

void
hikari_view_config_fini(struct hikari_view_config *view_config)
{
  assert(view_config != NULL);

  struct hikari_view_properties *properties = &view_config->properties;
  struct hikari_view_properties *child_properties =
      view_config->child_properties;

  if (child_properties != properties) {
    fini_properties(child_properties);
    hikari_free(child_properties);
  }

  fini_properties(properties);
  hikari_free(view_config->app_id);
}

struct hikari_sheet *
hikari_view_properties_resolve_sheet(struct hikari_view_properties *properties)
{
  assert(properties != NULL);

  struct hikari_sheet *sheet;
  if (properties->sheet_nr != -1) {
    sheet = hikari_server.workspace->sheets + properties->sheet_nr;
  } else {
    sheet = hikari_server.workspace->sheet;
  }

  return sheet;
}

struct hikari_group *
hikari_view_properties_resolve_group(
    struct hikari_view_properties *properties, const char *app_id)
{
  struct hikari_group *group;

  if (properties->group_name != NULL && strlen(properties->group_name) > 0) {
    group = hikari_server_find_or_create_group(properties->group_name);
  } else {
    group = hikari_server_find_or_create_group(app_id);
  }

  return group;
}

void
hikari_view_properties_resolve_position(
    struct hikari_view_properties *properties,
    struct hikari_view *view,
    int *x,
    int *y)
{
  assert(properties != NULL);

  struct hikari_output *output = hikari_server.workspace->output;
  struct wlr_box *geometry = hikari_view_border_geometry(view);

  switch (properties->position.type) {
    case HIKARI_POSITION_CONFIG_TYPE_ABSOLUTE:
      *x = properties->position.config.absolute.x;
      *y = properties->position.config.absolute.y;
      break;

    case HIKARI_POSITION_CONFIG_TYPE_AUTO:
      *x = hikari_server.cursor.wlr_cursor->x - output->geometry.x;
      *y = hikari_server.cursor.wlr_cursor->y - output->geometry.y;
      break;

    case HIKARI_POSITION_CONFIG_TYPE_RELATIVE:
      switch (properties->position.config.relative) {
        case HIKARI_POSITION_CONFIG_RELATIVE_BOTTOM_LEFT:
          hikari_geometry_position_bottom_left(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_BOTTOM_MIDDLE:
          hikari_geometry_position_bottom_middle(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_BOTTOM_RIGHT:
          hikari_geometry_position_bottom_right(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_CENTER:
          hikari_geometry_position_center(geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_CENTER_LEFT:
          hikari_geometry_position_center_left(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_CENTER_RIGHT:
          hikari_geometry_position_center_right(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_TOP_LEFT:
          hikari_geometry_position_top_left(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_TOP_MIDDLE:
          hikari_geometry_position_top_middle(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_TOP_RIGHT:
          hikari_geometry_position_top_right(
              geometry, &output->usable_area, x, y);
      }
      break;
  }
}

static bool
parse_property(struct hikari_view_properties *properties,
    const char *key,
    lua_State *L,
    int idx)
{
  bool success = false;

  if (!strcmp(key, "group")) {
    const char *group_name;

    if (!hikari_config_string(L, idx, "a views entry", key, &group_name)) {
      goto done;
    }

    if (strlen(group_name) == 0) {
      hikari_config_error(
          "a views entry", "expected non-empty string for \"group\"");
      goto done;
    }

    hikari_free(properties->group_name);
    properties->group_name = strdup(group_name);
  } else if (!strcmp(key, "sheet")) {
    int64_t sheet_nr;

    if (!hikari_config_int(L, idx, "a views entry", key, &sheet_nr)) {
      goto done;
    }

    /* Action purpose: hikari_view_properties_resolve_sheet() indexes
    workspace->sheets with this number and -1 is its sentinel for "whichever
    sheet is current", so anything outside that range reads past the end of an
    array of HIKARI_NR_OF_SHEETS sheets. */
    if (sheet_nr < -1 || sheet_nr >= HIKARI_NR_OF_SHEETS) {
      hikari_config_error("a views entry",
          "expected integer between 0 and 9, or -1 for the current sheet, for "
          "\"sheet\"");
      goto done;
    }

    properties->sheet_nr = sheet_nr;
  } else if (!strcmp(key, "mark")) {
    const char *mark_name;

    if (!hikari_config_string(L, idx, "a views entry", key, &mark_name)) {
      goto done;
    }

    if (strlen(mark_name) != 1 || mark_name[0] < 'a' || mark_name[0] > 'z') {
      hikari_config_error(
          "a views entry", "invalid \"mark\" register \"%s\"", mark_name);
      goto done;
    }

    properties->mark = &hikari_marks[mark_name[0] - 'a'];
  } else if (!strcmp(key, "position")) {
    if (!hikari_position_config_parse(&properties->position, L, idx)) {
      goto done;
    }
  } else if (!strcmp(key, "focus")) {
    bool focus;

    if (!hikari_config_bool(L, idx, "a views entry", key, &focus)) {
      goto done;
    }

    properties->focus = focus;
  } else if (!strcmp(key, "invisible")) {
    bool invisible;

    if (!hikari_config_bool(L, idx, "a views entry", key, &invisible)) {
      goto done;
    }

    properties->invisible = invisible;
  } else if (!strcmp(key, "floating")) {
    bool floating;

    if (!hikari_config_bool(L, idx, "a views entry", key, &floating)) {
      goto done;
    }

    properties->floating = floating;
  } else if (!strcmp(key, "public")) {
    bool publicview;

    if (!hikari_config_bool(L, idx, "a views entry", key, &publicview)) {
      goto done;
    }

    properties->publicview = publicview;
  } else {
    hikari_config_error("a views entry", "unknown key \"%s\"", key);
    goto done;
  }

  success = true;

done:

  return success;
}

static bool
parse_properties(struct hikari_view_properties *properties,
    lua_State *L,
    int view_config_idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, view_config_idx);

  while (hikari_config_iter_next(&iter, L, &key, "a views entry")) {
    if (strcmp(key, "inherit")) {
      if (!parse_property(properties, key, L, iter.vidx)) {
        goto done;
      }
    }

    /* Action purpose: lua_next() takes the cursor key from the top of the
    stack, so everything this round pushed -- the value at iter.vidx included
    -- has to be gone before the next one. */
    lua_settop(L, iter.vidx - 1);
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

/* Function purpose: Copy one named property of a views entry from the parent
to the child properties. The name is the user's, so it is matched here rather
than at the call sites; note that the direct key is "public" while the
inheritable name for it is "publicview", an asymmetry hikari(1) documents and
which a rename would quietly break. */
static bool
parse_inherited_property(struct hikari_view_properties *properties,
    struct hikari_view_properties *child_properties,
    const char *attr)
{
  if (!strcmp(attr, "group")) {
    /* Action purpose: release any name already held before overwriting it.
    The inherit list can name "group" more than once -- nothing rejects a
    repeated entry -- and parse_property() frees first for exactly this reason.
    hikari_free() tolerates NULL, so the unset case needs no guard. */
    hikari_free(child_properties->group_name);

    if (properties->group_name != NULL) {
      child_properties->group_name = strdup(properties->group_name);
    } else {
      child_properties->group_name = NULL;
    }
  } else if (!strcmp(attr, "sheet")) {
    child_properties->sheet_nr = properties->sheet_nr;
  } else if (!strcmp(attr, "mark")) {
    child_properties->mark = properties->mark;
  } else if (!strcmp(attr, "position")) {
    memcpy(&child_properties->position,
        &properties->position,
        sizeof(struct hikari_position_config));
  } else if (!strcmp(attr, "focus")) {
    child_properties->focus = properties->focus;
  } else if (!strcmp(attr, "invisible")) {
    child_properties->invisible = properties->invisible;
  } else if (!strcmp(attr, "floating")) {
    child_properties->floating = properties->floating;
  } else if (!strcmp(attr, "publicview")) {
    child_properties->publicview = properties->publicview;
  } else {
    hikari_config_error(
        "a views entry", "unknown \"inherit\" property \"%s\"", attr);
    return false;
  }

  return true;
}

static bool
parse_inherited_properties(struct hikari_view_properties *properties,
    struct hikari_view_properties *child_properties,
    lua_State *L,
    int inherit_idx)
{
  bool success = false;
  int entry_top = lua_gettop(L);
  const char *attr;
  lua_Integer nr_of_elements;
  lua_Integer i;

  inherit_idx = lua_absindex(L, inherit_idx);

  /* Action purpose: a bare string inherits a single property. libucl gave this
  for nothing, because iterating a scalar yields the scalar once; in Lua a
  string is not a table and needs its own branch to keep working. */
  if (lua_type(L, inherit_idx) == LUA_TSTRING) {
    if (!hikari_config_string(
            L, inherit_idx, "a views entry", "inherit", &attr)) {
      goto done;
    }

    success = parse_inherited_property(properties, child_properties, attr);
    goto done;
  }

  if (lua_type(L, inherit_idx) != LUA_TTABLE) {
    hikari_config_error("a views entry",
        "\"inherit\" expects a string or a list of them, got %s",
        lua_typename(L, lua_type(L, inherit_idx)));
    goto done;
  }

  nr_of_elements = (lua_Integer)lua_rawlen(L, inherit_idx);

  /* Action purpose: a table written as { floating = true } has no array part,
  so the loop below would walk none of it and inherit nothing without a word.
  An empty list is not that mistake -- it inherits nothing deliberately, as an
  empty libucl array did -- so the two are told apart by asking whether the
  table holds any entry at all. */
  if (nr_of_elements == 0) {
    lua_pushnil(L);

    if (lua_next(L, inherit_idx) != 0) {
      hikari_config_error("a views entry",
          "\"inherit\" expects a list -- write inherit = { { floating = true "
          "} }, not inherit = { floating = true }");
      goto done;
    }
  }

  for (i = 1; i <= nr_of_elements; i++) {
    int element_idx;

    lua_rawgeti(L, inherit_idx, i);
    element_idx = lua_gettop(L);

    switch (lua_type(L, element_idx)) {
      case LUA_TSTRING:
        if (!hikari_config_string(
                L, element_idx, "a views entry", "inherit", &attr)) {
          goto done;
        }

        if (!parse_inherited_property(properties, child_properties, attr)) {
          goto done;
        }
        break;

      case LUA_TTABLE:
        if (!parse_properties(child_properties, L, element_idx)) {
          goto done;
        }
        break;

      default:
        hikari_config_error("a views entry",
            "\"inherit\" expects a string or a table, got %s",
            lua_typename(L, lua_type(L, element_idx)));
        goto done;
    }

    lua_settop(L, entry_top);
  }

  success = true;

done:
  lua_settop(L, entry_top);

  return success;
}

bool
hikari_view_config_parse(struct hikari_view_config *view_config,
    lua_State *L,
    int view_config_idx)
{
  struct hikari_view_properties *properties = &view_config->properties;
  bool success = false;
  int entry_top = lua_gettop(L);

  view_config_idx = lua_absindex(L, view_config_idx);

  if (!parse_properties(properties, L, view_config_idx)) {
    goto done;
  }

  if (hikari_config_field(L, view_config_idx, "inherit") != LUA_TNIL) {
    int inherit_idx = lua_gettop(L);

    view_config->child_properties =
        hikari_malloc(sizeof(struct hikari_view_properties));

    struct hikari_view_properties *child_properties =
        view_config->child_properties;
    init_properties(child_properties);

    if (!parse_inherited_properties(
            properties, child_properties, L, inherit_idx)) {
      goto done;
    }
  }

  success = true;

done:
  lua_settop(L, entry_top);

  return success;
}
