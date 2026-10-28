#include <hikari/configuration.h>

#include <ctype.h>
#include <errno.h>

#include <hikari/evdev.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_switch.h>

#include <hikari/action.h>
#include <hikari/animation.h>
#include <hikari/action_config.h>
#include <hikari/binding.h>
#include <hikari/binding_config.h>
#include <hikari/color.h>
#include <hikari/config_lua.h>
#include <hikari/command.h>
#include <hikari/exec.h>
#include <hikari/geometry.h>
#include <hikari/gesture_config.h>
#include <hikari/keyboard.h>
#include <hikari/keyboard_config.h>
#include <hikari/layout.h>
#include <hikari/layout_config.h>
#include <hikari/layout_policy.h>
#include <hikari/mark.h>
#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/output_config.h>
#include <hikari/pointer.h>
#include <hikari/pointer_config.h>
#include <hikari/server.h>
#include <hikari/sheet.h>
#include <hikari/split.h>
#include <hikari/switch.h>
#include <hikari/switch_config.h>
#include <hikari/tile.h>
#include <hikari/view.h>
#include <hikari/view_config.h>
#include <hikari/workspace.h>

struct hikari_configuration *hikari_configuration = NULL;

static bool
parse_layout_func(const char *layout_func_name,
    hikari_layout_func *layout_func,
    int64_t *views,
    bool *explicit_nr_of_views)
{
  if (!strcmp(layout_func_name, "queue")) {
    *layout_func = hikari_sheet_queue_layout;
  } else if (!strcmp(layout_func_name, "stack")) {
    *layout_func = hikari_sheet_stack_layout;
  } else if (!strcmp(layout_func_name, "full")) {
    *layout_func = hikari_sheet_full_layout;
  } else if (!strcmp(layout_func_name, "grid")) {
    *layout_func = hikari_sheet_grid_layout;
  } else if (!strcmp(layout_func_name, "single")) {
    *views = 1;
    *explicit_nr_of_views = true;
    *layout_func = hikari_sheet_single_layout;
  } else if (!strcmp(layout_func_name, "empty")) {
    *views = 0;
    *explicit_nr_of_views = true;
    *layout_func = hikari_sheet_empty_layout;
  } else {
    hikari_config_error(
        NULL, "unknown container \"layout\" \"%s\"", layout_func_name);
    return false;
  }

  return true;
}

static bool
parse_container(
    lua_State *L, int container_idx, struct hikari_split **container)
{
  bool success = false;
  struct hikari_split_container *ret = NULL;
  hikari_layout_func layout_func = NULL;
  int64_t views = 256;
  bool explicit_nr_of_views = false;
  bool override_nr_of_views = false;
  const char *layout_func_name;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, container_idx);
  while (hikari_config_iter_next(&iter, L, &key, "a layout container")) {
    if (!strcmp(key, "layout")) {
      if (!hikari_config_string(
              L, iter.vidx, "a layout container", key, &layout_func_name)) {
        goto done;
      }

      if (!parse_layout_func(
              layout_func_name, &layout_func, &views, &explicit_nr_of_views)) {
        goto done;
      }
    } else if (!strcmp(key, "views")) {
      override_nr_of_views = true;
      if (explicit_nr_of_views) {
        hikari_config_error("a layout container",
            "cannot set \"views\" for \"layout\" \"%s\" container",
            layout_func_name);
        goto done;
      }

      if (!hikari_config_int(
              L, iter.vidx, "a layout container", key, &views)) {
        goto done;
      }

      if (views < 2 || views > 256) {
        hikari_config_error("a layout container",
            "expected integer between 2 and 256 for container \"views\"");
        goto done;
      }
    } else {
      hikari_config_error(
          "a layout container", "unknown container key \"%s\"", key);
      goto done;
    }
  }

  /* Action purpose: A false from next() means either "the table is exhausted"
  or "the walk was abandoned because an entry was malformed", and only the
  iterator can tell them apart. Every loop in this file carries this test, for
  the same reason. */
  if (iter.failed) {
    goto done;
  }

  if (layout_func == NULL) {
    hikari_config_error("a layout container", "container expects \"layout\"");
    goto done;
  }

  if (override_nr_of_views && explicit_nr_of_views) {
    hikari_config_error("a layout container",
        "cannot set \"views\" for \"layout\" \"%s\" container",
        layout_func_name);
    goto done;
  }

  ret = hikari_malloc(sizeof(struct hikari_split_container));
  hikari_split_container_init(ret, views, layout_func);

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  *container = (struct hikari_split *)ret;

  return success;
}

static bool
split_is_container(int top, int bottom, int left, int right, int layout)
{
  return layout != LUA_TNIL && left == LUA_TNIL && right == LUA_TNIL &&
         top == LUA_TNIL && bottom == LUA_TNIL;
}

static bool
split_is_vertical(int top, int bottom, int left, int right, int layout)
{
  return left != LUA_TNIL && right != LUA_TNIL && top == LUA_TNIL &&
         bottom == LUA_TNIL && layout == LUA_TNIL;
}

static bool
split_is_horizontal(int top, int bottom, int left, int right, int layout)
{
  return top != LUA_TNIL && bottom != LUA_TNIL && left == LUA_TNIL &&
         right == LUA_TNIL && layout == LUA_TNIL;
}

/* Action purpose: The bound on how deeply a layout may nest, counted in C
stack frames because that is the resource which actually runs out. One level of
the parse_split/parse_vertical recursion costs 272 bytes of C stack, so the
8 MiB default stack is gone at roughly 30,000 levels -- and it goes without a
diagnostic, as a SIGSEGV during startup. The Lua stack reserved per level
(lua_checkstack below) is a different resource entirely and allows roughly
142,000 levels, so it can never be the first to fail. A configuration reaches
these depths easily: a loop nesting tables costs little memory and few VM
instructions, so neither CONFIG_MEMORY_CAP nor CONFIG_INSTRUCTION_BUDGET stops
it either. 64 is past any layout that can be rendered -- the shipped default
nests two deep, and ten levels already divides a 4K axis into sub-pixel panes
-- so no configuration that works today is refused. */
#define HIKARI_SPLIT_DEPTH_MAX 64

static bool
parse_vertical(lua_State *, int, struct hikari_split **, int);

static bool
parse_horizontal(lua_State *, int, struct hikari_split **, int);

static bool
parse_split(
    lua_State *L, int split_idx, struct hikari_split **split, int depth)
{
  bool success = false;
  struct hikari_split *ret = NULL;
  int entry_top = lua_gettop(L);
  int type = lua_type(L, split_idx);

  /* Action purpose: The C stack bound -- see HIKARI_SPLIT_DEPTH_MAX. The depth
  travels as a parameter rather than in a file-scope counter: a parse abandoned
  part way down never unwinds back through the decrements, which would leave a
  static non-zero and refuse the next, shallower load. */
  if (depth > HIKARI_SPLIT_DEPTH_MAX) {
    hikari_config_error(NULL, "layout is nested too deeply");
    goto done;
  }

  /* Action purpose: The Lua stack bound, which is a separate resource from the
  depth above: this level holds the discriminating lookups below plus the
  caller's iterator cursor and value while it descends, and LUA_MINSTACK is
  only 20, so the slots have to be asked for rather than assumed. Reserving
  slots says nothing about recursion depth, and running out of them is a
  different failure, so it reads differently.

  The figure is 16 rather than a counted minimum, and that is deliberate.
  A review raised that the worst path might need 9 -- the iterator's key and
  value, the "scale" field's value, and the "min" and "max" that
  parse_scale_dynamic holds live at the same time, since it pushes both before
  its single lua_settop. Counting by hand I get 5 plus whatever
  parse_scale_value pushes, which is under 8, but I could not pin the exact
  figure by measurement: instrumenting this function means editing the
  PARSE_SPLIT macro that generates half of it, and the system liblua is built
  without LUA_USE_APICHECK, so an overrun would be silent rather than
  asserted.

  So rather than record a count I have not verified, the reservation carries a
  margin. lua_checkstack() only ensures capacity -- it is not an allocation
  per slot, and every nested level re-checks on entry -- so the margin costs
  nothing and the question stops mattering. If the real figure is 8 this is a
  no-op; if it is 9 it closes an overrun that the C API calls undefined. */
  if (!lua_checkstack(L, 16)) {
    hikari_config_error(NULL, "cannot reserve Lua stack for a layout element");
    goto done;
  }

  if (type == LUA_TSTRING) {
    const char *layout_func_name;
    hikari_layout_func layout_func;
    int64_t views = 256;
    bool explicit_nr_of_views = false;
    if (!hikari_config_string(
            L, split_idx, NULL, "layout", &layout_func_name)) {
      goto done;
    }

    if (!parse_layout_func(
            layout_func_name, &layout_func, &views, &explicit_nr_of_views)) {
      goto done;
    }

    ret = hikari_malloc(sizeof(struct hikari_split_container));
    hikari_split_container_init(
        (struct hikari_split_container *)ret, views, layout_func);

  } else if (type == LUA_TTABLE) {
    int left = hikari_config_field(L, split_idx, "left");
    int right = hikari_config_field(L, split_idx, "right");
    int top = hikari_config_field(L, split_idx, "top");
    int bottom = hikari_config_field(L, split_idx, "bottom");
    int layout = hikari_config_field(L, split_idx, "layout");

    if (split_is_vertical(top, bottom, left, right, layout)) {
      if (!parse_vertical(L, split_idx, &ret, depth)) {
        hikari_config_error(NULL, "failed to parse vertical split");
        goto done;
      }
    } else if (split_is_horizontal(top, bottom, left, right, layout)) {
      if (!parse_horizontal(L, split_idx, &ret, depth)) {
        hikari_config_error(NULL, "failed to parse horizontal split");
        goto done;
      }
    } else if (split_is_container(top, bottom, left, right, layout)) {
      if (!parse_container(L, split_idx, &ret)) {
        hikari_config_error(NULL, "failed to parse container");
        goto done;
      }
    } else {
      hikari_config_error(NULL, "failed to determine layout element");
      goto done;
    }
  } else {
    hikari_config_error(
        NULL, "expected string or table for layout element, got %s",
        lua_typename(L, type));
    goto done;
  }

  success = true;

done:
  lua_settop(L, entry_top);

  *split = ret;

  return success;
}

static bool
parse_scale_value(lua_State *L, int idx, const char *name, double *scale)
{
  bool success = false;
  double ret;

  if (!hikari_config_double(L, idx, "a layout split", name, &ret)) {
    goto done;
  }

  if (ret < hikari_split_scale_min || ret > hikari_split_scale_max) {
    hikari_config_error("a layout split",
        "\"%s\" of \"%.2f\" is not between \"0.1\" and \"0.9\"",
        name,
        ret);
    goto done;
  }

  *scale = ret;

  success = true;

done:

  return success;
}

static bool
parse_scale_dynamic(
    lua_State *L, int scale_idx, struct hikari_split_scale_dynamic *scale)
{
  bool success = false;
  int entry_top = lua_gettop(L);

  if (hikari_config_field(L, scale_idx, "min") != LUA_TNIL) {
    if (!parse_scale_value(L, lua_gettop(L), "min", &scale->min)) {
      goto done;
    }
  } else {
    scale->min = hikari_split_scale_min;
  }

  if (hikari_config_field(L, scale_idx, "max") != LUA_TNIL) {
    if (!parse_scale_value(L, lua_gettop(L), "max", &scale->max)) {
      goto done;
    }
  } else {
    scale->max = hikari_split_scale_max;
  }

  success = true;

done:
  lua_settop(L, entry_top);

  return success;
}

static bool
parse_scale(lua_State *L, int scale_idx, struct hikari_split_scale *scale)
{
  bool success = false;
  int type = lua_type(L, scale_idx);

  switch (type) {
    /* Action purpose: Both number subtypes are taken, where libucl's parser
    accepted only UCL_FLOAT here and dropped an integer into the silent
    default below. No integer is a legal scale -- the range is 0.1 to 0.9 --
    so `scale = 1` now reaches parse_scale_value() and is refused by name
    instead of failing with no message at all. */
    case LUA_TNUMBER:
      scale->type = HIKARI_SPLIT_SCALE_TYPE_FIXED;
      if (!parse_scale_value(L, scale_idx, "scale", &scale->scale.fixed)) {
        goto done;
      }
      break;

    case LUA_TTABLE:
      scale->type = HIKARI_SPLIT_SCALE_TYPE_DYNAMIC;

      if (!parse_scale_dynamic(L, scale_idx, &scale->scale.dynamic)) {
        goto done;
      }
      break;

    default:
      hikari_config_error("a layout split",
          "\"scale\" expects a number or a table, got %s",
          lua_typename(L, type));
      goto done;
  }

  success = true;

done:

  return success;
}

#define PARSE_SPLIT(name, NAME, first, FIRST, second, SECOND)                  \
  static bool parse_##name(                                                    \
      lua_State *L, int name##_idx, struct hikari_split **name, int depth)     \
  {                                                                            \
    bool success = false;                                                      \
    struct hikari_config_iter iter;                                            \
    const char *key;                                                           \
    struct hikari_split_##name *ret = NULL;                                    \
    enum hikari_split_##name##_orientation orientation =                       \
        HIKARI_##NAME##_SPLIT_ORIENTATION_##FIRST;                             \
    struct hikari_split *first = NULL;                                         \
    struct hikari_split *second = NULL;                                        \
    struct hikari_split_scale scale;                                           \
    scale.type = HIKARI_SPLIT_SCALE_TYPE_FIXED;                                \
    scale.scale.fixed = hikari_split_scale_default;                            \
                                                                               \
    hikari_config_iter_init(&iter, L, name##_idx);                             \
                                                                               \
    while (hikari_config_iter_next(&iter, L, &key, "a layout split")) {        \
      if (!strcmp(key, "scale")) {                                             \
        if (!parse_scale(L, iter.vidx, &scale)) {                              \
          goto done;                                                           \
        }                                                                      \
      } else if (!strcmp(key, "orientation")) {                                \
        /* Action purpose: Which half of a split is laid out first used to be  \
        decided by which of the two keys libucl yielded first, and a Lua       \
        table has no order to read that from. The orientation is therefore     \
        written down, defaulting to the key order's own default so that a      \
        configuration which wrote "#first" before "#second" -- every split in  \
        the shipped default does -- needs no new key and behaves as it did. */ \
        const char *value;                                                     \
                                                                               \
        if (!hikari_config_string(                                             \
                L, iter.vidx, "a layout split", key, &value)) {                \
          goto done;                                                           \
        }                                                                      \
                                                                               \
        if (!strcmp(value, #first)) {                                          \
          orientation = HIKARI_##NAME##_SPLIT_ORIENTATION_##FIRST;             \
        } else if (!strcmp(value, #second)) {                                  \
          orientation = HIKARI_##NAME##_SPLIT_ORIENTATION_##SECOND;            \
        } else {                                                               \
          hikari_config_error("a layout split",                                \
              "expected \"" #first "\" or \"" #second "\" for \"" #name        \
              "\" \"orientation\", got \"%s\"",                                \
              value);                                                          \
          goto done;                                                           \
        }                                                                      \
      } else if (!strcmp(key, #first)) {                                       \
        if (!parse_split(L, iter.vidx, &first, depth + 1)) {                   \
          hikari_config_error("a layout split",                                \
              "invalid \"" #first "\" for \"" #name "\" split");               \
          goto done;                                                           \
        }                                                                      \
      } else if (!strcmp(key, #second)) {                                      \
        if (!parse_split(L, iter.vidx, &second, depth + 1)) {                  \
          hikari_config_error("a layout split",                                \
              "invalid \"" #second "\" for \"" #name "\" split");              \
          goto done;                                                           \
        }                                                                      \
      } else {                                                                 \
        hikari_config_error(                                                   \
            "a layout split", "unknown \"" #name "\" key \"%s\"", key);        \
        goto done;                                                             \
      }                                                                        \
    }                                                                          \
                                                                               \
    if (iter.failed) {                                                         \
      goto done;                                                               \
    }                                                                          \
                                                                               \
    if (first == NULL) {                                                       \
      hikari_config_error("a layout split",                                    \
          "missing \"" #first "\" for \"" #name "\" split");                   \
      goto done;                                                               \
    }                                                                          \
                                                                               \
    if (second == NULL) {                                                      \
      hikari_config_error("a layout split",                                    \
          "missing \"" #second "\" for " #name " split");                      \
      goto done;                                                               \
    }                                                                          \
                                                                               \
    ret = hikari_malloc(sizeof(struct hikari_split_##name));                   \
    hikari_split_##name##_init(ret, &scale, orientation, first, second);       \
                                                                               \
    /* Action purpose: ret has just adopted both subtrees and is the only      \
    owner from here on. Dropping this frame's pointers is the transfer: it     \
    is what tells the cleanup below that there is nothing left to free,        \
    instead of leaving that to be re-derived from success or from ret. */      \
    first = NULL;                                                              \
    second = NULL;                                                             \
                                                                               \
    success = true;                                                            \
                                                                               \
  done:                                                                        \
    hikari_config_iter_fini(&iter, L);                                         \
                                                                               \
    /* Action purpose: Anything still held here is owned by this frame --      \
    the success path above handed both subtrees to ret and NULLed them, so     \
    this cannot double free. Every other path reaching this label was          \
    abandoned mid-walk: a bad scale or orientation, an unknown or malformed    \
    key, a half that failed to parse, a half that was never given. Any of      \
    them can arrive with the sibling already built, because a Lua table has    \
    no key order and the second half may well be walked before the first.      \
    hikari_split_free() recurses over the whole subtree, and it dereferences   \
    its argument, so NULL is checked here rather than passed. */               \
    if (first != NULL) {                                                       \
      hikari_split_free(first);                                                \
    }                                                                          \
                                                                               \
    if (second != NULL) {                                                      \
      hikari_split_free(second);                                               \
    }                                                                          \
                                                                               \
    *name = (struct hikari_split *)ret;                                        \
                                                                               \
    return success;                                                            \
  }

PARSE_SPLIT(vertical, VERTICAL, left, LEFT, right, RIGHT);
PARSE_SPLIT(horizontal, HORIZONTAL, top, TOP, bottom, BOTTOM);
#undef PARSE_SPLIT

struct hikari_view_config *
hikari_configuration_resolve_view_config(
    struct hikari_configuration *configuration, const char *app_id)
{
  assert(app_id != NULL);

  if (app_id != NULL) {
    struct hikari_view_config *view_config;
    wl_list_for_each (view_config, &configuration->view_configs, link) {
      if (!strcmp(view_config->app_id, app_id)) {
        return view_config;
      }
    }
  }

  return NULL;
}

static char *
copy_in_config_string(
    lua_State *L, int idx, const char *where, const char *key)
{
  const char *str;
  char *ret;

  bool success = hikari_config_string(L, idx, where, key, &str);

  if (success) {
    size_t len = strlen(str);
    ret = hikari_malloc(len + 1);
    strcpy(ret, str);

    return ret;
  } else {
    return NULL;
  }
}

/* [COMMENT] Function purpose: Parse one colourscheme value into normalised
RGBA, accepting either form:

  integer  0xRRGGBB      -- historical form, always fully opaque
  string   "#RRGGBB"     -- same, written explicitly
  string   "#RRGGBBAA"   -- carries alpha in the low byte

Alpha deliberately cannot be expressed as an integer. `0x0080FFCC` and
`0x80FFCC` are values that a magnitude test cannot tell apart, so an
8-vs-6-digit integer heuristic would silently misread any colour whose red
channel is zero. The string form makes the digit count explicit, so the two
never collide and every existing integer config keeps its exact meaning. See
DECISIONS_LOG Phase 60. */
/* [COMMENT] Function purpose: Resolve a `colorN` palette reference to its index,
or report that the string is not one.

Strict about the whole token rather than just the prefix: "colorful" and
"color16" are both rejected, so a mistyped reference is an error at load time
instead of a silently wrong colour. */
static bool
parse_palette_reference(const char *str, int *index)
{
  if (strncmp(str, "color", 5) != 0) {
    return false;
  }

  const char *digits = str + 5;

  if (digits[0] == '\0') {
    return false;
  }

  int value = 0;
  for (const char *c = digits; *c != '\0'; c++) {
    if (!isdigit((unsigned char)*c)) {
      return false;
    }

    value = value * 10 + (*c - '0');

    if (value >= HIKARI_NR_OF_PALETTE_COLORS) {
      return false;
    }
  }

  *index = value;

  return true;
}

/* [COMMENT] Function purpose: Parse one colour value.

`configuration` may be NULL, which means "literal forms only" and is what the
palette block itself passes. A palette entry that could name another palette
entry would resolve against whatever happened to be parsed first, making the
meaning of the file depend on key order; forbidding it costs nothing, since a
palette is by definition where the literals live. */
static bool
parse_color(struct hikari_configuration *configuration,
    lua_State *L,
    int idx,
    const char *key,
    float dst[static 4])
{
  if (lua_type(L, idx) == LUA_TSTRING) {
    const char *str;

    if (!hikari_config_string(L, idx, NULL, key, &str)) {
      return false;
    }

    /* [COMMENT] Action purpose: Anything not starting with '#' is taken to be a
    palette reference. The quotes are not optional in Lua -- a bare `color3` is
    a variable read, which the configuration sandbox refuses by name -- so
    `active = "color3"` is the one spelling. */
    if (str[0] != '#') {
      int index;

      if (configuration == NULL || !parse_palette_reference(str, &index)) {
        hikari_config_error(NULL,
            "expected \"#RRGGBB\", \"#RRGGBBAA\"%s for \"%s\", got \"%s\"",
            configuration != NULL ? " or \"color0\"-\"color15\"" : "",
            key,
            str);
        return false;
      }

      memcpy(dst, configuration->palette[index], sizeof(float) * 4);

      return true;
    }

    size_t len = strlen(str + 1);

    if (len != 6 && len != 8) {
      hikari_config_error(
          NULL, "\"%s\" must have 6 or 8 hex digits, got %zu", key, len);
      return false;
    }

    for (size_t i = 1; i <= len; i++) {
      if (!isxdigit((unsigned char)str[i])) {
        hikari_config_error(
            NULL, "invalid hex digit in \"%s\" value \"%s\"", key, str);
        return false;
      }
    }

    /* [COMMENT] Action purpose: strtoul over exactly 6 or 8 validated hex
    digits cannot overflow uint32_t, so the value is used directly. */
    unsigned long value = strtoul(str + 1, NULL, 16);

    if (len == 8) {
      hikari_color_convert_rgba(dst, (uint32_t)value);
    } else {
      hikari_color_convert(dst, (uint32_t)value);
    }

    return true;
  }

  if (lua_type(L, idx) != LUA_TNUMBER) {
    hikari_config_error(NULL,
        "expected integer or \"#RRGGBB[AA]\" string for \"%s\", got %s",
        key,
        lua_typename(L, lua_type(L, idx)));
    return false;
  }

  int64_t color;
  if (!hikari_config_int(L, idx, NULL, key, &color)) {
    return false;
  }

  /* [COMMENT] Action purpose: Reject out-of-range values instead of silently
  truncating them in the cast below -- -1 would otherwise become white, and
  0x1FF0000 would become 0xFF0000. */
  if (color < 0 || color > 0xFFFFFF) {
    hikari_config_error(
        NULL, "\"%s\" must be between 0x000000 and 0xFFFFFF", key);
    return false;
  }

  hikari_color_convert(dst, (uint32_t)color);

  return true;
}

/* [COMMENT] Function purpose: Parse the `ui { palette { ... } }` block --
sixteen positional colours named `color0` through `color15`.

Every key is optional; an unmentioned slot keeps its default, so a configuration
can override two colours without restating fourteen. Entries are parsed with a
NULL configuration, which forbids one palette entry referring to another -- see
parse_color(). */
static bool
parse_palette(
    struct hikari_configuration *configuration, lua_State *L, int palette_idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, palette_idx);
  while (hikari_config_iter_next(&iter, L, &key, "ui.palette")) {
    int index;

    if (!parse_palette_reference(key, &index)) {
      hikari_config_error("ui.palette",
          "expected \"color0\"-\"color15\" in \"palette\", got \"%s\"",
          key);
      goto done;
    }

    if (!parse_color(
            NULL, L, iter.vidx, key, configuration->palette[index])) {
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_colorscheme(struct hikari_configuration *configuration,
    lua_State *L,
    int colorscheme_idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, colorscheme_idx);
  while (hikari_config_iter_next(&iter, L, &key, "ui.colorscheme")) {
    if (!strcmp("selected", key)) {
      if (!parse_color(configuration,
              L,
              iter.vidx,
              key,
              configuration->indicator_selected)) {
        goto done;
      }
    } else if (!strcmp("grouped", key)) {
      if (!parse_color(configuration,
              L,
              iter.vidx,
              key,
              configuration->indicator_grouped)) {
        goto done;
      }
    } else if (!strcmp("first", key)) {
      if (!parse_color(configuration,
              L,
              iter.vidx,
              key,
              configuration->indicator_first)) {
        goto done;
      }
    } else if (!strcmp("conflict", key)) {
      if (!parse_color(configuration,
              L,
              iter.vidx,
              key,
              configuration->indicator_conflict)) {
        goto done;
      }
    } else if (!strcmp("insert", key)) {
      if (!parse_color(configuration,
              L,
              iter.vidx,
              key,
              configuration->indicator_insert)) {
        goto done;
      }
    } else if (!strcmp("active", key)) {
      if (!parse_color(
              configuration, L, iter.vidx, key, configuration->border_active)) {
        goto done;
      }
    } else if (!strcmp("inactive", key)) {
      if (!parse_color(configuration,
              L,
              iter.vidx,
              key,
              configuration->border_inactive)) {
        goto done;
      }
    } else if (!strcmp("foreground", key)) {
      if (!parse_color(
              configuration, L, iter.vidx, key, configuration->foreground)) {
        goto done;
      }
    } else if (!strcmp("background", key)) {
      if (!parse_color(
              configuration, L, iter.vidx, key, configuration->clear)) {
        goto done;
      }
    } else if (!strcmp("bar", key)) {
      /* [COMMENT] Action purpose: The top bar's own background. It previously
      borrowed "background" (the output clear colour), which meant it could not
      be tinted or made translucent without also changing the desktop
      background behind every window. See DECISIONS_LOG Phase 60. */
      if (!parse_color(
              configuration, L, iter.vidx, key, configuration->bar)) {
        goto done;
      }
    } else {
      hikari_config_error(
          "ui.colorscheme", "unknown color key \"%s\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_execute(
    struct hikari_configuration *configuration, lua_State *L, int marks_idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, marks_idx);
  while (hikari_config_iter_next(&iter, L, &key, "marks")) {
    struct hikari_exec *execute = NULL;

    if (strlen(key) != 1 || !(key[0] >= 'a' && key[0] <= 'z')) {
      hikari_config_error("marks", "invalid \"marks\" register \"%s\"", key);
      goto done;
    } else {
      int nr = key[0] - 'a';
      execute = &configuration->execs[nr];
    }

    assert(execute != NULL);

    char *command = copy_in_config_string(L, iter.vidx, "marks", key);

    if (command != NULL) {
      execute->command = command;
    } else {
      hikari_config_error("marks",
          "invalid command for \"marks\" register \"%c\"",
          key[0]);
      goto done;
    }
    execute = NULL;
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_view_configs(
    struct hikari_configuration *configuration, lua_State *L, int views_idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, views_idx);
  while (hikari_config_iter_next(&iter, L, &key, "views")) {
    struct hikari_view_config *view_config =
        hikari_malloc(sizeof(struct hikari_view_config));

    hikari_view_config_init(view_config);
    wl_list_insert(&configuration->view_configs, &view_config->link);

    size_t keylen = strlen(key);

    view_config->app_id = hikari_malloc(keylen + 1);
    strcpy(view_config->app_id, key);

    if (!hikari_view_config_parse(view_config, L, iter.vidx)) {
      hikari_config_error("views", "failed to parse \"views\" \"%s\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_mouse_button(const char *str, uint32_t *keycode)
{
  if (!strcmp(str, "left")) {
    *keycode = BTN_LEFT;
  } else if (!strcmp(str, "right")) {
    *keycode = BTN_RIGHT;
  } else if (!strcmp(str, "middle")) {
    *keycode = BTN_MIDDLE;
  } else if (!strcmp(str, "side")) {
    *keycode = BTN_SIDE;
  } else if (!strcmp(str, "extra")) {
    *keycode = BTN_EXTRA;
  } else if (!strcmp(str, "forward")) {
    *keycode = BTN_FORWARD;
  } else if (!strcmp(str, "back")) {
    *keycode = BTN_BACK;
  } else if (!strcmp(str, "task")) {
    *keycode = BTN_TASK;
  } else {
    hikari_config_error(NULL, "unknown mouse button \"%s\"", str);
    return false;
  }

  return true;
}

static bool
parse_action(const char *action_name,
    lua_State *L,
    int action_idx,
    const char **command)
{
  if (!hikari_config_string(L, action_idx, "actions", action_name, command)) {
    return false;
  }

  return true;
}

static bool
parse_actions(
    struct hikari_configuration *configuration, lua_State *L, int actions_idx)
{
  bool success = false;
  struct hikari_action_config *action_config;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, actions_idx);
  while (hikari_config_iter_next(&iter, L, &key, "actions")) {
    const char *command;

    if (!parse_action(key, L, iter.vidx, &command)) {
      goto done;
    }

    action_config = hikari_malloc(sizeof(struct hikari_action_config));
    hikari_action_config_init(action_config, key, command);

    wl_list_insert(&configuration->action_configs, &action_config->link);
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_layout(char layout_register,
    lua_State *L,
    int layout_idx,
    struct hikari_split **split)
{
  struct hikari_split *ret = NULL;

  if (!parse_split(L, layout_idx, &ret, 0)) {
    hikari_config_error("layouts",
        "failed to parse layout for register \"%c\"",
        layout_register);
    return false;
  }

  *split = ret;

  return true;
}

static bool
parse_layouts(
    struct hikari_configuration *configuration, lua_State *L, int layouts_idx)
{
  bool success = false;
  struct hikari_layout_config *layout_config;
  struct hikari_split *split;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, layouts_idx);
  while (hikari_config_iter_next(&iter, L, &key, "layouts")) {
    if (strlen(key) > 1 ||
        !((key[0] >= 'a' && key[0] <= 'z') || isdigit(key[0]))) {
      hikari_config_error("layouts", "expected layout register name");
      goto done;
    }

    char layout_register = key[0];

    if (!parse_layout(layout_register, L, iter.vidx, &split)) {
      goto done;
    }

    layout_config = hikari_malloc(sizeof(struct hikari_layout_config));
    hikari_layout_config_init(layout_config, layout_register, split);

    wl_list_insert(&configuration->layout_configs, &layout_config->link);
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

/* [COMMENT] Function purpose: Parse the top-level `layout` block -- the policy
that decides when a sheet re-tiles itself.

Kept separate from parse_layouts() above, which parses `layouts` (plural) and
treats every key as a layout register letter. A policy key in that block would
be read as a register name and rejected with a misleading error, which is the
whole reason the two live in different sections. */
static bool
parse_layout_policy(
    struct hikari_configuration *configuration, lua_State *L, int layout_idx)
{
  bool success = false;
  struct hikari_layout_policy *policy = &configuration->layout_policy;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, layout_idx);
  while (hikari_config_iter_next(&iter, L, &key, "layout")) {
    if (!strcmp(key, "auto") || !strcmp(key, "reflow-on-close")) {
      bool value;

      if (!hikari_config_bool(L, iter.vidx, "layout", key, &value)) {
        goto done;
      }

      if (!strcmp(key, "auto")) {
        policy->automatic = value;
      } else {
        policy->on_close = value;
      }
    } else if (!strcmp(key, "insert")) {
      const char *value;

      if (!hikari_config_string(L, iter.vidx, "layout", key, &value)) {
        goto done;
      }

      if (!strcmp(value, "append")) {
        policy->insert = HIKARI_LAYOUT_INSERT_APPEND;
      } else if (!strcmp(value, "prepend")) {
        policy->insert = HIKARI_LAYOUT_INSERT_PREPEND;
      } else {
        hikari_config_error("layout",
            "expected \"append\" or \"prepend\" for \"insert\", got \"%s\"",
            value);
        goto done;
      }
    } else if (!strcmp(key, "default-register")) {
      const char *value;

      if (!hikari_config_string(L, iter.vidx, "layout", key, &value)) {
        goto done;
      }

      /* [COMMENT] Action purpose: Validated against exactly the character set
      parse_layouts() accepts as a register name, so a value that parses here is
      one that could name a real layout. Whether it DOES is not checked -- the
      `layouts` block may legitimately be parsed after this one, and an
      unresolvable register falls back to the per-sheet default at use time
      rather than failing the whole configuration. */
      if (strlen(value) != 1 ||
          !((value[0] >= 'a' && value[0] <= 'z') || isdigit(value[0]))) {
        hikari_config_error("layout",
            "\"default-register\" must be a single layout register "
            "(\"a\"-\"z\" or \"0\"-\"9\"), got \"%s\"",
            value);
        goto done;
      }

      policy->default_register = value[0];
    } else {
      hikari_config_error("layout", "unknown \"layout\" key \"%s\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

struct hikari_split *
hikari_configuration_lookup_layout(
    struct hikari_configuration *configuration, char layout_register)
{
  struct hikari_layout_config *layout_config;
  wl_list_for_each (layout_config, &configuration->layout_configs, link) {
    if (layout_register == layout_config->layout_register) {
      return layout_config->split;
    }
  }

  return NULL;
}

char *
lookup_action(
    struct hikari_configuration *configuration, const char *action_name)
{
  struct hikari_action_config *action_config;
  wl_list_for_each (action_config, &configuration->action_configs, link) {
    if (!strcmp(action_name, action_config->action_name)) {
      return action_config->command;
    }
  }

  return NULL;
}

static bool
parse_keyboard_bindings(
    struct hikari_configuration *configuration, lua_State *L, int bindings_idx)
{
  bool success = false;
  struct hikari_binding_config *binding_config;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, bindings_idx);
  while (hikari_config_iter_next(&iter, L, &key, "bindings.keyboard")) {
    binding_config = hikari_malloc(sizeof(struct hikari_binding_config));
    wl_list_insert(
        &configuration->keyboard_binding_configs, &binding_config->link);

    if (!hikari_binding_config_key_parse(&binding_config->key, key)) {
      goto done;
    }

    struct hikari_action *action = &binding_config->action;
    hikari_action_init(action);

    if (!hikari_action_parse(
            action, &configuration->action_configs, L, iter.vidx)) {
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
finalize_keyboard_configs(struct hikari_configuration *configuration)
{
  struct hikari_keyboard_config *keyboard_config;

  if (wl_list_empty(&configuration->keyboard_configs)) {
    keyboard_config = hikari_malloc(sizeof(struct hikari_keyboard_config));
    hikari_keyboard_config_default(keyboard_config);

    wl_list_insert(&configuration->keyboard_configs, &keyboard_config->link);
  }

  wl_list_for_each (keyboard_config, &configuration->keyboard_configs, link) {
    if (!hikari_keyboard_config_compile_keymap(keyboard_config)) {
      return false;
    }
  }

  return true;
}

static bool
parse_mouse_bindings(
    struct hikari_configuration *configuration, lua_State *L, int bindings_idx)
{
  bool success = false;
  struct hikari_binding_config *binding_config;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, bindings_idx);
  while (hikari_config_iter_next(&iter, L, &key, "bindings.mouse")) {
    binding_config = hikari_malloc(sizeof(struct hikari_binding_config));
    wl_list_insert(
        &configuration->mouse_binding_configs, &binding_config->link);

    if (!hikari_binding_config_button_parse(&binding_config->key, key)) {
      goto done;
    }

    struct hikari_action *action = &binding_config->action;
    hikari_action_init(action);

    if (!hikari_action_parse(
            action, &configuration->action_configs, L, iter.vidx)) {
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_bindings(
    struct hikari_configuration *configuration, lua_State *L, int bindings_idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, bindings_idx);
  while (hikari_config_iter_next(&iter, L, &key, "bindings")) {
    if (!strcmp(key, "keyboard")) {
      if (!parse_keyboard_bindings(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "mouse")) {
      if (!parse_mouse_bindings(configuration, L, iter.vidx)) {
        goto done;
      }
    } else {
      hikari_config_error(
          "bindings", "unexpected \"bindings\" section \"%s\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_pointer_config(struct hikari_pointer_config *pointer_config,
    lua_State *L,
    int pointer_config_idx)
{
  bool success = false;
  const char *pointer_name = pointer_config->name;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, pointer_config_idx);
  while (hikari_config_iter_next(
      &iter, L, &key, "an inputs.pointers entry")) {
    if (!strcmp(key, "accel")) {
      double accel;
      if (!hikari_config_double(
              L, iter.vidx, "an inputs.pointers entry", key, &accel)) {
        goto done;
      }

      if (accel < -1.0 || accel > 1.0) {
        hikari_config_error("an inputs.pointers entry",
            "expected float between -1 and 1 for \"%s\" \"accel\"",
            pointer_name);
        goto done;
      }

      hikari_pointer_config_set_accel(pointer_config, accel);
    } else if (!strcmp(key, "accel-profile")) {
      const char *accel_profile;
      if (!hikari_config_string(
              L, iter.vidx, "an inputs.pointers entry", key, &accel_profile)) {
        goto done;
      }

      if (!strcmp(accel_profile, "none")) {
        hikari_pointer_config_set_accel_profile(
            pointer_config, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);
      } else if (!strcmp(accel_profile, "flat")) {
        hikari_pointer_config_set_accel_profile(
            pointer_config, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
      } else if (!strcmp(accel_profile, "adaptive")) {
        hikari_pointer_config_set_accel_profile(
            pointer_config, LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
      } else {
        hikari_config_error("an inputs.pointers entry",
            "unknown \"accel-profile\" \"%s\" for \"%s\"",
            accel_profile,
            pointer_name);
        goto done;
      }
    } else if (!strcmp(key, "scroll-method")) {
      const char *scroll_method;
      if (!hikari_config_string(
              L, iter.vidx, "an inputs.pointers entry", key, &scroll_method)) {
        goto done;
      }

      if (!strcmp(scroll_method, "on-button-down")) {
        hikari_pointer_config_set_scroll_method(
            pointer_config, LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
      } else if (!strcmp(scroll_method, "no-scroll")) {
        hikari_pointer_config_set_scroll_method(
            pointer_config, LIBINPUT_CONFIG_SCROLL_NO_SCROLL);
      } else {
        hikari_config_error("an inputs.pointers entry",
            "unknown \"scroll-method\" \"%s\" for \"%s\"",
            scroll_method,
            pointer_name);
        goto done;
      }
    } else if (!strcmp(key, "scroll-button")) {
      const char *scroll_button;
      uint32_t scroll_button_keycode;
      if (!hikari_config_string(
              L, iter.vidx, "an inputs.pointers entry", key, &scroll_button)) {
        goto done;
      }

      if (!parse_mouse_button(scroll_button, &scroll_button_keycode)) {
        hikari_config_error(
            "an inputs.pointers entry", "failed to parse \"scroll-button\"");
        goto done;
      }

      hikari_pointer_config_set_scroll_button(
          pointer_config, scroll_button_keycode);
    } else if (!strcmp(key, "disable-while-typing")) {
      bool disable_while_typing;
      if (!hikari_config_bool(L,
              iter.vidx,
              "an inputs.pointers entry",
              key,
              &disable_while_typing)) {
        goto done;
      }

      if (disable_while_typing) {
        hikari_pointer_config_set_disable_while_typing(
            pointer_config, LIBINPUT_CONFIG_DWT_ENABLED);
      } else {
        hikari_pointer_config_set_disable_while_typing(
            pointer_config, LIBINPUT_CONFIG_DWT_DISABLED);
      }
    } else if (!strcmp(key, "tap")) {
      bool tap;
      if (!hikari_config_bool(
              L, iter.vidx, "an inputs.pointers entry", key, &tap)) {
        goto done;
      }

      if (tap) {
        hikari_pointer_config_set_tap(
            pointer_config, LIBINPUT_CONFIG_TAP_ENABLED);
      } else {
        hikari_pointer_config_set_tap(
            pointer_config, LIBINPUT_CONFIG_TAP_DISABLED);
      }
    } else if (!strcmp(key, "tap-drag")) {
      bool tap_drag;
      if (!hikari_config_bool(
              L, iter.vidx, "an inputs.pointers entry", key, &tap_drag)) {
        goto done;
      }

      if (tap_drag) {
        hikari_pointer_config_set_tap_drag(
            pointer_config, LIBINPUT_CONFIG_DRAG_ENABLED);
      } else {
        hikari_pointer_config_set_tap_drag(
            pointer_config, LIBINPUT_CONFIG_DRAG_DISABLED);
      }
    } else if (!strcmp(key, "tap-drag-lock")) {
      bool tap_drag_lock;
      if (!hikari_config_bool(
              L, iter.vidx, "an inputs.pointers entry", key, &tap_drag_lock)) {
        goto done;
      }

      if (tap_drag_lock) {
        hikari_pointer_config_set_tap_drag_lock(
            pointer_config, LIBINPUT_CONFIG_DRAG_LOCK_ENABLED);
      } else {
        hikari_pointer_config_set_tap_drag_lock(
            pointer_config, LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
      }
    } else if (!strcmp(key, "middle-emulation")) {
      bool middle_emulation;
      if (!hikari_config_bool(L,
              iter.vidx,
              "an inputs.pointers entry",
              key,
              &middle_emulation)) {
        goto done;
      }

      if (middle_emulation) {
        hikari_pointer_config_set_middle_emulation(
            pointer_config, LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
      } else {
        hikari_pointer_config_set_middle_emulation(
            pointer_config, LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
      }
    } else if (!strcmp(key, "natural-scrolling")) {
      bool natural_scrolling;
      if (!hikari_config_bool(L,
              iter.vidx,
              "an inputs.pointers entry",
              key,
              &natural_scrolling)) {
        goto done;
      }

      hikari_pointer_config_set_natural_scrolling(
          pointer_config, natural_scrolling);
    } else {
      hikari_config_error("an inputs.pointers entry",
          "unknown \"pointer\" configuration key \"%s\" for \"%s\"",
          key,
          pointer_name);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_pointers(
    struct hikari_configuration *configuration, lua_State *L, int pointers_idx)
{
  bool success = false;
  struct hikari_pointer_config *pointer_config;
  struct hikari_config_iter iter;
  const char *pointer_name;

  hikari_config_iter_init(&iter, L, pointers_idx);
  while (hikari_config_iter_next(&iter, L, &pointer_name, "inputs.pointers")) {
    pointer_config = hikari_malloc(sizeof(struct hikari_pointer_config));
    hikari_pointer_config_init(pointer_config, pointer_name);

    wl_list_insert(&configuration->pointer_configs, &pointer_config->link);

    if (!parse_pointer_config(pointer_config, L, iter.vidx)) {
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  struct hikari_pointer_config *default_config =
      hikari_configuration_resolve_pointer_config(configuration, "*");

  if (default_config != NULL) {
    wl_list_for_each (pointer_config, &configuration->pointer_configs, link) {
      if (!!strcmp(pointer_config->name, "*")) {
        hikari_pointer_config_merge(pointer_config, default_config);
      }
    }
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_keyboards(
    struct hikari_configuration *configuration, lua_State *L, int keyboards_idx)
{
  bool success = false;
  struct hikari_keyboard_config *keyboard_config;
  struct hikari_config_iter iter;
  const char *keyboard_name;

  hikari_config_iter_init(&iter, L, keyboards_idx);
  while (
      hikari_config_iter_next(&iter, L, &keyboard_name, "inputs.keyboards")) {
    keyboard_config = hikari_malloc(sizeof(struct hikari_keyboard_config));
    hikari_keyboard_config_init(keyboard_config, keyboard_name);

    wl_list_insert(&configuration->keyboard_configs, &keyboard_config->link);

    if (!hikari_keyboard_config_parse(keyboard_config, L, iter.vidx)) {
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  struct hikari_keyboard_config *default_config =
      hikari_configuration_resolve_keyboard_config(configuration, "*");
  if (default_config == NULL) {
    default_config = hikari_malloc(sizeof(struct hikari_keyboard_config));
    hikari_keyboard_config_default(default_config);

    wl_list_insert(&configuration->keyboard_configs, &default_config->link);
  }

  wl_list_for_each (keyboard_config, &configuration->keyboard_configs, link) {
    if (!!strcmp(keyboard_config->keyboard_name, "*")) {
      hikari_keyboard_config_merge(keyboard_config, default_config);
    }
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_switches(
    struct hikari_configuration *configuration, lua_State *L, int switches_idx)
{
  bool success = false;
  struct hikari_switch_config *switch_config;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, switches_idx);
  while (hikari_config_iter_next(&iter, L, &key, "inputs.switches")) {
    switch_config = hikari_malloc(sizeof(struct hikari_switch_config));
    wl_list_insert(&configuration->switch_configs, &switch_config->link);

    switch_config->switch_name = strdup(key);

    /* Action purpose: Every other hikari_action_parse() call site initialises
    its action first, and this one did not -- so a switch whose entry set only
    `begin` left `end` holding whatever the allocation came with, and toggling
    the switch back called it. */
    hikari_action_init(&switch_config->action);

    if (!hikari_action_parse(&switch_config->action,
            &configuration->action_configs,
            L,
            iter.vidx)) {
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  // [COMMENT] Action purpose: releases the iterator on both the successful and
  // failed parsing paths.
  hikari_config_iter_fini(&iter, L);

  return success;
}

/* Function purpose: Parse the `inputs { gestures {} }` block into the
configuration's gesture binding list. */
static bool
parse_gestures(
    struct hikari_configuration *configuration, lua_State *L, int gestures_idx)
{
  bool success = false;
  struct hikari_gesture_binding_config *gesture_binding_config;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, gestures_idx);
  while (hikari_config_iter_next(&iter, L, &key, "inputs.gestures")) {
    gesture_binding_config =
        hikari_malloc(sizeof(struct hikari_gesture_binding_config));
    wl_list_insert(&configuration->gesture_binding_configs,
        &gesture_binding_config->link);

    if (!hikari_gesture_binding_config_key_parse(key,
            &gesture_binding_config->type,
            &gesture_binding_config->direction,
            &gesture_binding_config->fingers)) {
      hikari_config_error(
          "inputs.gestures", "invalid gesture binding \"%s\"", key);
      goto done;
    }

    struct hikari_action *action = &gesture_binding_config->action;
    hikari_action_init(action);

    if (!hikari_action_parse(
            action, &configuration->action_configs, L, iter.vidx)) {
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_inputs(
    struct hikari_configuration *configuration, lua_State *L, int inputs_idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, inputs_idx);
  while (hikari_config_iter_next(&iter, L, &key, "inputs")) {
    if (!strcmp(key, "pointers")) {
      if (!parse_pointers(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "keyboards")) {
      if (!parse_keyboards(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "switches")) {
      if (!parse_switches(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "gestures")) {
      if (!parse_gestures(configuration, L, iter.vidx)) {
        goto done;
      }
    } else {
      hikari_config_error(
          "inputs", "unknown \"inputs\" configuration key \"%s\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_background(lua_State *L,
    int background_idx,
    char **background,
    enum hikari_background_fit *fit)
{
  bool success = false;
  bool has_background = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, background_idx);
  while (hikari_config_iter_next(&iter, L, &key, "an outputs background")) {
    if (!strcmp(key, "path")) {
      /* Action purpose: copy_in_config_string() returns NULL when the value is
      not a string, having already printed the diagnostic. Without this test
      the walk continued, iter.failed stayed false, and the function returned
      true after reporting an error -- the one "error printed, success
      returned" path left in this rewrite. has_background is set only once the
      copy succeeded, so a NULL can never be handed to
      hikari_output_config_set_background(). */
      *background =
          copy_in_config_string(L, iter.vidx, "an outputs background", key);
      if (*background == NULL) {
        goto done;
      }
      has_background = true;
    } else if (!strcmp(key, "fit")) {
      const char *fit_value;
      if (!hikari_config_string(
              L, iter.vidx, "an outputs background", key, &fit_value)) {
        goto done;
      }
      if (!strcmp(fit_value, "center")) {
        *fit = HIKARI_BACKGROUND_CENTER;
      } else if (!strcmp(fit_value, "stretch")) {
        *fit = HIKARI_BACKGROUND_STRETCH;
      } else if (!strcmp(fit_value, "tile")) {
        *fit = HIKARI_BACKGROUND_TILE;
      } else {
        hikari_config_error("an outputs background",
            "unexpected \"background\" \"fit\" \"%s\"",
            fit_value);
        goto done;
      }
    } else {
      hikari_config_error("an outputs background",
          "unknown \"background\" configuration key \"%s\"",
          key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  if (!has_background) {
    hikari_config_error(
        "an outputs background", "missing \"path\" for \"background\"");
    goto done;
  }

  success = true;

done:
  /* [COMMENT] Action purpose: Free the path on every failing path, because the
  caller cannot. It passes an UNINITIALISED char * and, on a false return, goes
  straight to its own `done:` without touching it -- so an allocation made here
  and then abandoned by a later key is unreachable from that moment on. The
  abandoning keys are real: a "fit" that is not one of the three accepted
  spellings, an unknown key inside the background table, or an iterator that
  failed, each of which can follow a "path" that already allocated.

  has_background is exactly the right guard, not an approximation of one: it is
  set only after the copy succeeds, and the NULL check above returns before it
  on a failed copy, so has_background is true if and only if *background holds
  a pointer this function owns. NULLing it after the free keeps the caller from
  reading a dangling value should that contract ever change.

  This is the same ownership discipline PARSE_SPLIT uses in this file -- own it
  until it is handed over, free it at one place if it never is. The leak it
  closes is small but not one-shot: a reload leaves the compositor running, so
  a configuration that is edited and reloaded repeatedly leaks once each time. */
  if (!success && has_background) {
    hikari_free(*background);
    *background = NULL;
  }

  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_output_config(struct hikari_output_config *output_config,
    lua_State *L,
    int output_config_idx)

{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, output_config_idx);
  while (hikari_config_iter_next(&iter, L, &key, "an outputs entry")) {
    if (!strcmp(key, "background")) {
      int type = lua_type(L, iter.vidx);

      if (type == LUA_TSTRING) {
        char *background =
            copy_in_config_string(L, iter.vidx, "an outputs entry", key);

        if (background == NULL) {
          hikari_config_error(
              "an outputs entry", "invalid \"background\" value");
          goto done;
        }

        hikari_output_config_set_background(output_config, background);
      } else if (type == LUA_TTABLE) {
        char *background;
        /* Action purpose: parse_background() writes through this pointer only
        inside its "fit" branch, so a background table that sets just a path
        leaves it indeterminate -- and it is stored unconditionally below.
        Initialised to the same default the scalar-string form uses, so the two
        spellings of a background agree. */
        enum hikari_background_fit background_fit = HIKARI_BACKGROUND_STRETCH;

        if (!parse_background(L, iter.vidx, &background, &background_fit)) {
          goto done;
        }

        hikari_output_config_set_background(output_config, background);
        hikari_output_config_set_background_fit(output_config, background_fit);
      } else {
        hikari_config_error("an outputs entry",
            "expected string or table for \"background\", got %s",
            lua_typename(L, type));
        goto done;
      }
    } else if (!strcmp(key, "position")) {
      struct hikari_position_config position;
      if (!hikari_position_config_absolute_parse(&position, L, iter.vidx)) {
        hikari_config_error(
            "an outputs entry", "could not parse \"output\" \"position\"");
        goto done;
      }

      hikari_output_config_set_position(output_config, position);
    } else {
      // [COMMENT] Action purpose: Unknown output keys must fail the parse, not
      // just log -- silently accepting typos (e.g. "postion") would leave a
      // running compositor that ignores the intended rule. This matches the
      // strict behaviour of every other unknown-key branch in this parser.
      hikari_config_error("an outputs entry",
          "unknown \"outputs\" configuration key \"%s\"",
          key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_outputs(
    struct hikari_configuration *configuration, lua_State *L, int outputs_idx)
{
  bool success = false;
  struct hikari_output_config *output_config;
  struct hikari_config_iter iter;
  const char *output_name;

  hikari_config_iter_init(&iter, L, outputs_idx);
  while (hikari_config_iter_next(&iter, L, &output_name, "outputs")) {
    output_config = hikari_malloc(sizeof(struct hikari_output_config));
    hikari_output_config_init(output_config, output_name);

    wl_list_insert(&configuration->output_configs, &output_config->link);

    if (!parse_output_config(output_config, L, iter.vidx)) {
      hikari_config_error(
          "outputs", "failed to parse \"outputs\" configuration");
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

  struct hikari_output_config *default_config =
      hikari_configuration_resolve_output_config(configuration, "*");

  if (default_config != NULL) {
    wl_list_for_each (output_config, &configuration->output_configs, link) {
      if (!!strcmp(output_config->output_name, "*")) {
        hikari_output_config_merge(output_config, default_config);
      }
    }
  }

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_border(
    struct hikari_configuration *configuration, lua_State *L, int border_idx)
{
  int64_t border;

  if (!hikari_config_int(L, border_idx, "ui", "border", &border)) {
    return false;
  }

  /* The upper bound here is not a realistic display size -- it exists so
  that border.c's and sheet.c's own arithmetic on this value (doubling it
  for a border's total inflation, multiplying it against a row/column
  count for total gap space) stays well inside int range no matter how
  many rows, columns, or views a layout ends up with. Bounding the input
  once here is far simpler than auditing and widening every downstream
  site that multiplies it. */
  if (border < 0 || border > 100000) {
    hikari_config_error("ui", "\"border\" must be between 0 and 100000");
    return false;
  }

  configuration->border = border;

  return true;
}

static bool
parse_gap(struct hikari_configuration *configuration, lua_State *L, int gap_idx)
{
  int64_t gap;

  if (!hikari_config_int(L, gap_idx, "ui", "gap", &gap)) {
    return false;
  }

  /* Same reasoning as parse_border's bound -- protects sheet.c's
  gap * row_gaps / gap * col_gaps arithmetic from overflowing, not a
  realistic gap size. */
  if (gap < 0 || gap > 100000) {
    hikari_config_error("ui", "\"gap\" must be between 0 and 100000");
    return false;
  }

  configuration->gap = gap;

  return true;
}

static bool
parse_step(
    struct hikari_configuration *configuration, lua_State *L, int step_idx)
{
  int64_t step;

  if (!hikari_config_int(L, step_idx, "ui", "step", &step)) {
    return false;
  }

  /* The same bound, and for the same reason, as "border" and "gap" above:
  hikari_config_int() hands back an int64_t and configuration->step is an int,
  so without a check the assignment is a silent narrowing conversion. It is
  silent in the worst way -- the load SUCCEEDS and the compositor runs with a
  value the user never wrote. Measured before this check existed:

    step = 4294967296   loaded, and became 0            -- movement does nothing
    step = 2147483648   loaded, and became -2147483648  -- movement inverts

  server.c reads this straight into `const int step` at three call sites and
  adds it to view geometry, so a negative value moves views the wrong way at
  maximum magnitude rather than failing. 100000 is not a realistic step; it is
  chosen to match the neighbouring keys so that one rule covers the three
  integer dimensions this section parses. */
  if (step < 0 || step > 100000) {
    hikari_config_error("ui", "\"step\" must be between 0 and 100000");
    return false;
  }

  configuration->step = step;

  return true;
}

static bool
parse_font(
    struct hikari_configuration *configuration, lua_State *L, int font_idx)
{
  const char *font;

  if (!hikari_config_string(L, font_idx, "ui", "font", &font)) {
    return false;
  }

  hikari_font_init(&configuration->font, font);

  return true;
}

/* [COMMENT] Function purpose: Parse `ui { lock { blur { ... } } }`.

Blur is expressed as an object rather than a bare number so that turning it off
and tuning it use the same key: `blur = false` disables it outright, while
`blur = { radius = 20 }` keeps it on and adjusts it. A bare `radius` with no way
to say "off" would have forced a second key that could contradict it. */
static bool
parse_lock_blur(
    struct hikari_lock_config *lock_config, lua_State *L, int blur_idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  if (lua_type(L, blur_idx) == LUA_TBOOLEAN) {
    bool blur;

    if (!hikari_config_bool(L, blur_idx, "ui.lock", "blur", &blur)) {
      return false;
    }

    lock_config->blur = blur;

    return true;
  }

  if (lua_type(L, blur_idx) != LUA_TTABLE) {
    hikari_config_error("ui.lock",
        "expected boolean or table for \"blur\", got %s",
        lua_typename(L, lua_type(L, blur_idx)));
    return false;
  }

  hikari_config_iter_init(&iter, L, blur_idx);
  while (hikari_config_iter_next(&iter, L, &key, "ui.lock.blur")) {
    if (!strcmp(key, "radius") || !strcmp(key, "passes")) {
      int64_t value;

      if (!hikari_config_int(L, iter.vidx, "ui.lock.blur", key, &value)) {
        goto done;
      }

      if (value < 0 || value > 512) {
        hikari_config_error("ui.lock.blur",
            "expected integer between 0 and 512 for \"%s\"",
            key);
        goto done;
      }

      if (!strcmp(key, "radius")) {
        lock_config->blur_radius = (int)value;
      } else {
        lock_config->blur_passes = (int)value;
      }
    } else {
      hikari_config_error("ui.lock.blur", "unknown \"blur\" key \"%s\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

/* [COMMENT] Function purpose: Parse the `ui { bar { ... } }` block -- how long a
top bar block may be before it is capped and scrolled, and how fast. */
static bool
parse_bar(struct hikari_bar_config *bar_config, lua_State *L, int bar_idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, bar_idx);
  while (hikari_config_iter_next(&iter, L, &key, "ui.bar")) {
    if (!strcmp(key, "max-block-chars")) {
      int64_t chars;

      /* [COMMENT] Action purpose: 256 is HIKARI_BAR_MAX_CAP_CHARS in
      src/bar.c, which sizes the per-block scroll buffer. The two must not
      drift. 0 disables capping and scrolling entirely. */
      if (!hikari_config_int(L, iter.vidx, "ui.bar", key, &chars)) {
        goto done;
      }

      if (chars < 0 || chars > 256) {
        hikari_config_error(
            "ui.bar", "expected integer 0-256 for \"max-block-chars\"");
        goto done;
      }

      bar_config->max_block_chars = (int)chars;
    } else if (!strcmp(key, "scroll-interval")) {
      int64_t interval;

      /* [COMMENT] Action purpose: Floor at 50ms. The timer repaints the whole
      bar on every step, so an unbounded value here is a way to make the
      compositor busy-render itself; below this the motion is not readable
      anyway. */
      if (!hikari_config_int(L, iter.vidx, "ui.bar", key, &interval)) {
        goto done;
      }

      if (interval < 50 || interval > 10000) {
        hikari_config_error(
            "ui.bar", "expected integer 50-10000 for \"scroll-interval\"");
        goto done;
      }

      bar_config->scroll_interval = (int)interval;
    } else if (!strcmp(key, "scroll-separator")) {
      const char *separator;

      if (!hikari_config_string(L, iter.vidx, "ui.bar", key, &separator)) {
        goto done;
      }

      hikari_free(bar_config->scroll_separator);
      bar_config->scroll_separator = hikari_malloc(strlen(separator) + 1);
      strcpy(bar_config->scroll_separator, separator);
    } else {
      hikari_config_error("ui.bar", "unknown \"bar\" key \"%s\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

/* [COMMENT] Function purpose: Parse `ui { lock { ... } }` -- the clock, the
blur, and how long the screen stays lit while locked. */
static bool
parse_lock(
    struct hikari_configuration *configuration, lua_State *L, int lock_idx)
{
  /* [COMMENT] Action purpose: Takes the whole configuration rather than just
  the lock block, because `clock-color` accepts a palette reference and the
  palette lives on the configuration. The lock block's own fields are reached
  through this one extra hop. */
  struct hikari_lock_config *lock_config = &configuration->lock;

  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, lock_idx);
  while (hikari_config_iter_next(&iter, L, &key, "ui.lock")) {
    if (!strcmp(key, "blur")) {
      if (!parse_lock_blur(lock_config, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "clock")) {
      bool clock;

      if (!hikari_config_bool(L, iter.vidx, "ui.lock", key, &clock)) {
        goto done;
      }

      lock_config->clock = clock;
    } else if (!strcmp(key, "clock-format") || !strcmp(key, "date-format")) {
      const char *format;

      if (!hikari_config_string(L, iter.vidx, "ui.lock", key, &format)) {
        goto done;
      }

      /* [COMMENT] Action purpose: Copy rather than borrow. The Lua state that
      owns this string is destroyed at the end of the load, while these strings
      are read every minute for the life of the session. */
      char **target = !strcmp(key, "clock-format") ? &lock_config->clock_format
                                                   : &lock_config->date_format;

      hikari_free(*target);
      *target = hikari_malloc(strlen(format) + 1);
      strcpy(*target, format);
    } else if (!strcmp(key, "clock-font") || !strcmp(key, "date-font")) {
      const char *font;

      if (!hikari_config_string(L, iter.vidx, "ui.lock", key, &font)) {
        goto done;
      }

      struct hikari_font *target = !strcmp(key, "clock-font")
          ? &lock_config->clock_font
          : &lock_config->date_font;

      hikari_font_fini(target);
      hikari_font_init(target, font);
    } else if (!strcmp(key, "clock-color")) {
      if (!parse_color(
              configuration, L, iter.vidx, key, lock_config->clock_color)) {
        goto done;
      }
    } else if (!strcmp(key, "blank-timeout-ac") ||
        !strcmp(key, "blank-timeout-battery")) {
      int64_t seconds;

      /* [COMMENT] Action purpose: 0 means never blank, which is a legitimate
      choice now that there is something worth looking at on the lock screen.
      The upper bound is a day -- anything longer is indistinguishable from
      never and is more likely a typo. */
      if (!hikari_config_int(L, iter.vidx, "ui.lock", key, &seconds)) {
        goto done;
      }

      if (seconds < 0 || seconds > 86400) {
        hikari_config_error("ui.lock",
            "expected integer between 0 and 86400 for \"%s\"",
            key);
        goto done;
      }

      if (!strcmp(key, "blank-timeout-ac")) {
        lock_config->blank_timeout_ac = (int)seconds;
      } else {
        lock_config->blank_timeout_battery = (int)seconds;
      }
    } else {
      hikari_config_error("ui.lock", "unknown \"lock\" key \"%s\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

/* [COMMENT] Function purpose: Parse the `ui { animation { ... } }` block.

`duration` is bounded rather than merely non-negative. Below about 20 ms an
animation is indistinguishable from an instant move but still costs a frame per
step, and above a second a window takes long enough to arrive that the compositor
feels broken -- both are far more likely to be a typo than an intention. 0 is
accepted and means instant, which is the same as `enabled = false` but reachable
without editing two keys. */
static bool
parse_animation(struct hikari_configuration *configuration,
    lua_State *L,
    int animation_idx)
{
  bool success = false;
  struct hikari_animation_config *config = &configuration->animation;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, animation_idx);
  while (hikari_config_iter_next(&iter, L, &key, "ui.animation")) {
    if (!strcmp(key, "enabled")) {
      bool enabled;

      if (!hikari_config_bool(L, iter.vidx, "ui.animation", key, &enabled)) {
        goto done;
      }

      config->enabled = enabled;
    } else if (!strcmp(key, "duration")) {
      int64_t duration;

      if (!hikari_config_int(L, iter.vidx, "ui.animation", key, &duration)) {
        goto done;
      }

      if (duration < 0 || duration > 1000) {
        hikari_config_error("ui.animation",
            "expected integer between 0 and 1000 for \"duration\"");
        goto done;
      }

      config->duration_msec = (int)duration;
    } else if (!strcmp(key, "easing")) {
      const char *easing;

      if (!hikari_config_string(L, iter.vidx, "ui.animation", key, &easing)) {
        goto done;
      }

      if (!strcmp(easing, "linear")) {
        config->easing = HIKARI_EASING_LINEAR;
      } else if (!strcmp(easing, "ease-out")) {
        config->easing = HIKARI_EASING_EASE_OUT;
      } else if (!strcmp(easing, "ease-in-out")) {
        config->easing = HIKARI_EASING_EASE_IN_OUT;
      } else {
        hikari_config_error("ui.animation",
            "expected \"linear\", \"ease-out\" or \"ease-in-out\" for "
            "\"easing\", got \"%s\"",
            easing);
        goto done;
      }
    } else {
      hikari_config_error(
          "ui.animation", "unknown \"animation\" key \"%s\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

static bool
parse_ui(struct hikari_configuration *configuration, lua_State *L, int ui_idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;
  int entry_top = lua_gettop(L);

  /* [COMMENT] Action purpose: The palette is resolved BEFORE the iteration
  below, not as one of its cases. Every other colour in this block may be
  written as a palette reference, and a Lua table has no order at all -- so
  parsing the palette in sequence would make a configuration's meaning depend on
  which key the traversal happened to reach first. Looking it up explicitly
  first is the same idiom read_configuration() uses for `actions` and
  `layouts`, and for the same reason. */
  if (hikari_config_field(L, ui_idx, "palette") != LUA_TNIL &&
      !parse_palette(configuration, L, lua_gettop(L))) {
    lua_settop(L, entry_top);
    return false;
  }

  lua_settop(L, entry_top);

  hikari_config_iter_init(&iter, L, ui_idx);
  while (hikari_config_iter_next(&iter, L, &key, "ui")) {
    if (!strcmp(key, "palette")) {
      // [COMMENT] Action purpose: Already handled above; accepted here so the
      // unknown-key branch does not reject it.
      continue;
    } else if (!strcmp(key, "colorscheme")) {
      if (!parse_colorscheme(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "animation")) {
      if (!parse_animation(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "font")) {
      hikari_font_fini(&configuration->font);
      if (!parse_font(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "border")) {
      if (!parse_border(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "gap")) {
      if (!parse_gap(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "spill")) {
      const char *spill;

      if (!hikari_config_string(L, iter.vidx, "ui", key, &spill)) {
        goto done;
      }

      if (!strcmp(spill, "always")) {
        configuration->spill = HIKARI_SPILL_ALWAYS;
      } else if (!strcmp(spill, "drag")) {
        configuration->spill = HIKARI_SPILL_DRAG;
      } else if (!strcmp(spill, "never")) {
        configuration->spill = HIKARI_SPILL_NEVER;
      } else {
        hikari_config_error("ui",
            "expected \"always\", \"drag\" or \"never\" for \"spill\", got "
            "\"%s\"",
            spill);
        goto done;
      }
    } else if (!strcmp(key, "step")) {
      if (!parse_step(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "lock")) {
      if (!parse_lock(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "bar")) {
      if (!parse_bar(&configuration->bar_config, L, iter.vidx)) {
        goto done;
      }
    } else {
      hikari_config_error("ui", "unknown key \"%s\" for \"ui\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

/* Function purpose: Read the table the configuration file returned into the
configuration being built. Called by hikari_config_lua_read() with that table
already on the stack, from inside a protected call, so that a raise anywhere
below is a diagnostic rather than a panic. */
static bool
read_configuration(lua_State *L, int root, void *ud)
{
  struct hikari_configuration *configuration = ud;
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;
  int entry_top = lua_gettop(L);

  /* Action purpose: `actions` and `layouts` are resolved ahead of the walk
  below, because a binding may name an action and a sheet may name a layout
  register, and a table traversal gives no guarantee about which section it
  reaches first. The two explicit lookups make the order a property of this
  function rather than of the file. */
  if (hikari_config_field(L, root, "actions") != LUA_TNIL &&
      !parse_actions(configuration, L, lua_gettop(L))) {
    hikari_config_error("the configuration", "failed to parse \"actions\"");
    lua_settop(L, entry_top);
    return false;
  }

  lua_settop(L, entry_top);

  if (hikari_config_field(L, root, "layouts") != LUA_TNIL &&
      !parse_layouts(configuration, L, lua_gettop(L))) {
    hikari_config_error("the configuration", "failed to parse \"layouts\"");
    lua_settop(L, entry_top);
    return false;
  }

  lua_settop(L, entry_top);

  hikari_config_iter_init(&iter, L, root);
  while (hikari_config_iter_next(&iter, L, &key, "the configuration")) {
    if (!strcmp(key, "ui")) {
      if (!parse_ui(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "views")) {
      if (!parse_view_configs(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "marks")) {
      if (!parse_execute(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "bindings")) {
      if (!parse_bindings(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "outputs")) {
      if (!parse_outputs(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "inputs")) {
      if (!parse_inputs(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "layout")) {
      if (!parse_layout_policy(configuration, L, iter.vidx)) {
        goto done;
      }
    } else if (!strcmp(key, "output_management_overrides_config")) {
      /* Action purpose: The only top-level key that is a value rather than a
      section, which is why it is handled inline instead of through a parse_*
      helper. It sits at the top level because it governs neither the interface
      nor any one output -- putting it in `ui { }` beside `spill` would file a
      display-authority decision under appearance, and putting it in
      `outputs { }` is not possible at all, since every key in that block is
      taken as an output name. */
      bool overrides;

      if (!hikari_config_bool(
              L, iter.vidx, "the configuration", key, &overrides)) {
        goto done;
      }

      configuration->output_management_overrides_config = overrides;
    } else if (!!strcmp(key, "actions") && !!strcmp(key, "layouts")) {
      hikari_config_error(
          "the configuration", "unknown configuration section \"%s\"", key);
      goto done;
    }
  }

  if (iter.failed) {
    goto done;
  }

  if (!finalize_keyboard_configs(configuration)) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

bool
hikari_configuration_load(
    struct hikari_configuration *configuration, char *config_path)
{
  return hikari_config_lua_read(config_path, read_configuration, configuration);
}

bool
hikari_configuration_reload(char *config_path)
{
  struct hikari_configuration *configuration =
      hikari_malloc(sizeof(struct hikari_configuration));

  hikari_configuration_init(configuration);

  bool success = hikari_configuration_load(configuration, config_path);

  if (success) {
    if (hikari_server.workspace->focus_view != NULL) {
      hikari_indicator_damage(
          &hikari_server.indicator, hikari_server.workspace->focus_view);
    }

    hikari_configuration_fini(hikari_configuration);
    hikari_free(hikari_configuration);
    hikari_configuration = configuration;

    struct hikari_pointer *pointer;
    wl_list_for_each (pointer, &hikari_server.pointers, server_pointers) {
      struct hikari_pointer_config *pointer_config =
          hikari_configuration_resolve_pointer_config(
              hikari_configuration, pointer->device->name);

      if (pointer_config != NULL) {
        hikari_pointer_configure(pointer, pointer_config);
      }
    }

    hikari_cursor_configure_bindings(
        &hikari_server.cursor, &configuration->mouse_binding_configs);

    struct hikari_keyboard *keyboard;
    wl_list_for_each (keyboard, &hikari_server.keyboards, server_keyboards) {
      struct hikari_keyboard_config *keyboard_config =
          hikari_configuration_resolve_keyboard_config(
              hikari_configuration, keyboard->wlr_keyboard->base.name);

      assert(keyboard_config != NULL);
      hikari_keyboard_configure(keyboard, keyboard_config);

      hikari_keyboard_configure_bindings(
          keyboard, &configuration->keyboard_binding_configs);
    }

    struct hikari_output *output;
    wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
      if (!output->wants_enabled) {
        continue;
      }

      struct hikari_view *view;
      wl_list_for_each (view, &output->views, output_views) {
        hikari_view_refresh_geometry(view, view->current_geometry);
      }

      struct hikari_output_config *output_config =
          hikari_configuration_resolve_output_config(
              hikari_configuration, output->wlr_output->name);

      if (output_config != NULL) {
        /* Action purpose: Re-assert the configured position only while this
        file leads. With output management in the lead, a reload that dragged
        every output back to where `outputs { }` says it should be would
        silently undo whatever wlr-randr or kanshi had arranged -- and a reload
        is bound to a key, so it would happen by accident. Reading
        hikari_configuration here is correct: the incoming configuration was
        swapped in above, so a reload is governed by its own setting rather than
        by the one it replaces. Only the position is affected; the background is
        this compositor's business either way. */
        if (!hikari_configuration->output_management_overrides_config &&
            output_config->position.value.type ==
                HIKARI_POSITION_CONFIG_TYPE_ABSOLUTE) {
          int x = output_config->position.value.config.absolute.x;
          int y = output_config->position.value.config.absolute.y;

          if (output->geometry.x != x || output->geometry.y != y) {
            hikari_output_move(output, x, y);
          }
        }

        if (output_config->background.value != NULL) {
          hikari_output_load_background(output,
              output_config->background.value,
              output_config->background_fit.value);
        }
      }
    }

    struct hikari_switch *swtch;
    wl_list_for_each (swtch, &hikari_server.switches, server_switches) {
      struct hikari_switch_config *switch_config =
          hikari_configuration_resolve_switch_config(
              hikari_configuration, swtch->wlr_switch->base.name);

      if (switch_config != NULL) {
        hikari_switch_configure(swtch, switch_config);
      } else {
        hikari_switch_reset(swtch);
      }
    }

    if (hikari_server.workspace->focus_view != NULL) {
      hikari_indicator_update(
          &hikari_server.indicator, hikari_server.workspace->focus_view);
    }
  } else {
    hikari_configuration_fini(configuration);
    hikari_free(configuration);
  }

  return success;
}

void
hikari_configuration_init(struct hikari_configuration *configuration)
{
  wl_list_init(&configuration->view_configs);
  wl_list_init(&configuration->output_configs);
  wl_list_init(&configuration->pointer_configs);
  wl_list_init(&configuration->keyboard_configs);
  wl_list_init(&configuration->layout_configs);
  wl_list_init(&configuration->action_configs);
  wl_list_init(&configuration->keyboard_binding_configs);
  wl_list_init(&configuration->mouse_binding_configs);
  wl_list_init(&configuration->switch_configs);
  wl_list_init(&configuration->gesture_binding_configs);

  /* [COMMENT] Action purpose: The default palette -- Hikari Sakura's own
  scheme. Laid out in the conventional 16-colour order (0-7 normal, 8-15
  bright), which is what makes it interchangeable with a terminal theme and
  what lets hikari-topbar consume the same sixteen values. */
  static const uint32_t default_palette[] = {
    0x2b1e3a, /* color0  -- base */
    0xc96464, /* color1  -- red */
    0xdf9f87, /* color2  -- orange */
    0xe4b382, /* color3  -- yellow */
    0x8e7cc3, /* color4  -- violet */
    0xb18fc7, /* color5  -- mauve */
    0x9fa0a6, /* color6  -- grey */
    0xd4d4d9, /* color7  -- text */
    0x5e5966, /* color8  -- bright base */
    0xdf8787, /* color9  -- bright red */
    0xf2bda8, /* color10 -- bright orange */
    0xf5cf9e, /* color11 -- bright yellow */
    0xaba0d9, /* color12 -- bright violet */
    0xcfaedc, /* color13 -- bright mauve */
    0xb8b9be, /* color14 -- bright grey */
    0xf0edf2  /* color15 -- bright text */
  };

  for (int i = 0; i < HIKARI_NR_OF_PALETTE_COLORS; i++) {
    hikari_color_convert(configuration->palette[i], default_palette[i]);
  }

  /* [COMMENT] Action purpose: Every semantic slot is DERIVED from the palette
  rather than carrying a literal of its own, so the two can never disagree about
  what the default theme is. A user who overrides only the palette gets a
  coherent scheme for free; one who overrides a slot directly still wins, since
  this runs before any parsing.

  The assignments are not arbitrary. Borders take the extremes -- brightest for
  the focused window, muted base for the rest -- because that contrast is the
  only cue to focus. The three group indicators take neighbouring violets so
  they read as one family (first is the anchor, grouped its relatives, selected
  the one you are on), conflict takes the red that means "this is already
  taken", and the indicator text takes the dark base because every indicator
  background above is a light tone. */
#define PALETTE(n) configuration->palette[n]
  memcpy(configuration->clear, PALETTE(0), sizeof(float) * 4);
  memcpy(configuration->foreground, PALETTE(0), sizeof(float) * 4);
  memcpy(configuration->indicator_first, PALETTE(4), sizeof(float) * 4);
  memcpy(configuration->indicator_grouped, PALETTE(5), sizeof(float) * 4);
  memcpy(configuration->indicator_selected, PALETTE(12), sizeof(float) * 4);
  memcpy(configuration->indicator_insert, PALETTE(13), sizeof(float) * 4);
  memcpy(configuration->indicator_conflict, PALETTE(9), sizeof(float) * 4);
  memcpy(configuration->border_active, PALETTE(15), sizeof(float) * 4);
  memcpy(configuration->border_inactive, PALETTE(8), sizeof(float) * 4);

  /* [COMMENT] Action purpose: The bar is the base colour at ~90% alpha, so it
  reads as an overlay rather than a solid block while staying legible. Built by
  copying the palette entry and overwriting alpha, because the palette stores
  opaque colours and alpha is the one thing a positional palette cannot carry. */
  memcpy(configuration->bar, PALETTE(0), sizeof(float) * 4);
  configuration->bar[3] = 0xE6 / 255.0f;
#undef PALETTE

  hikari_font_init(&configuration->font, "monospace 10");

  hikari_lock_config_init(&configuration->lock);
  hikari_bar_config_init(&configuration->bar_config);
  hikari_layout_policy_init(&configuration->layout_policy);
  hikari_animation_config_init(&configuration->animation);

  /* [COMMENT] Action purpose: DRAG rather than ALWAYS, even though ALWAYS is
  the behaviour every previous release had. A window resting half-painted over a
  screen showing a different sheet is a defect rather than a preference, and the
  overhang that IS useful -- the one that shows a drag is about to cross -- is
  exactly what this setting keeps. ALWAYS remains available for anyone who wants
  the old behaviour back verbatim. */
  configuration->spill = HIKARI_SPILL_DRAG;

  /* Action purpose: true, and it regresses nobody. The only output setting this
  key can suppress on reload is an absolute `position`, and a position defaults
  to HIKARI_POSITION_CONFIG_TYPE_AUTO (src/position_config.c) while the shipped
  configuration sets none at all -- so no existing configuration currently has a
  position re-applied on reload for this to stop. What it buys is that wlr-randr
  works out of the box, which is the behaviour anyone installing it expects. */
  configuration->output_management_overrides_config = true;

  configuration->border = 1;
  configuration->gap = 5;
  configuration->step = 100;

  for (int i = 0; i < HIKARI_NR_OF_EXECS; i++) {
    hikari_exec_init(&configuration->execs[i]);
  }
}

void
hikari_configuration_fini(struct hikari_configuration *configuration)
{
  hikari_font_fini(&configuration->font);
  hikari_lock_config_fini(&configuration->lock);
  hikari_bar_config_fini(&configuration->bar_config);

  struct hikari_view_config *view_config, *view_config_temp;
  wl_list_for_each_safe (
      view_config, view_config_temp, &configuration->view_configs, link) {
    wl_list_remove(&view_config->link);

    hikari_view_config_fini(view_config);
    hikari_free(view_config);
  }

  struct hikari_output_config *output_config, *output_config_temp;
  wl_list_for_each_safe (
      output_config, output_config_temp, &configuration->output_configs, link) {
    wl_list_remove(&output_config->link);

    hikari_output_config_fini(output_config);
    hikari_free(output_config);
  }

  struct hikari_pointer_config *pointer_config, *pointer_config_temp;
  wl_list_for_each_safe (pointer_config,
      pointer_config_temp,
      &configuration->pointer_configs,
      link) {
    wl_list_remove(&pointer_config->link);

    hikari_pointer_config_fini(pointer_config);
    hikari_free(pointer_config);
  }

  struct hikari_keyboard_config *keyboard_config, *keyboard_config_temp;
  wl_list_for_each_safe (keyboard_config,
      keyboard_config_temp,
      &configuration->keyboard_configs,
      link) {
    wl_list_remove(&keyboard_config->link);

    hikari_keyboard_config_fini(keyboard_config);
    hikari_free(keyboard_config);
  }

  struct hikari_switch_config *switch_config, *switch_config_temp;
  wl_list_for_each_safe (
      switch_config, switch_config_temp, &configuration->switch_configs, link) {
    wl_list_remove(&switch_config->link);

    hikari_switch_config_fini(switch_config);
    hikari_free(switch_config);
  }

  struct hikari_gesture_binding_config *gesture_binding_config,
      *gesture_binding_config_temp;
  wl_list_for_each_safe (gesture_binding_config,
      gesture_binding_config_temp,
      &configuration->gesture_binding_configs,
      link) {
    wl_list_remove(&gesture_binding_config->link);
    hikari_free(gesture_binding_config);
  }

  struct hikari_binding_config *binding_config, *binding_config_temp;
  wl_list_for_each_safe (binding_config,
      binding_config_temp,
      &configuration->keyboard_binding_configs,
      link) {
    wl_list_remove(&binding_config->link);
    hikari_free(binding_config);
  }
  wl_list_for_each_safe (binding_config,
      binding_config_temp,
      &configuration->mouse_binding_configs,
      link) {
    wl_list_remove(&binding_config->link);
    hikari_free(binding_config);
  }

  struct hikari_layout_config *layout_config, *layout_config_temp;
  wl_list_for_each_safe (
      layout_config, layout_config_temp, &configuration->layout_configs, link) {
    wl_list_remove(&layout_config->link);

    hikari_layout_config_fini(layout_config);
    hikari_free(layout_config);
  }

  struct hikari_action_config *action_config, *action_config_temp;
  wl_list_for_each_safe (
      action_config, action_config_temp, &configuration->action_configs, link) {
    wl_list_remove(&action_config->link);

    hikari_action_config_fini(action_config);
    hikari_free(action_config);
  }

  for (int i = 0; i < HIKARI_NR_OF_EXECS; i++) {
    hikari_exec_fini(&configuration->execs[i]);
  }
}

struct hikari_output_config *
hikari_configuration_resolve_output_config(
    struct hikari_configuration *configuration, const char *output_name)
{
  struct hikari_output_config *output_config;
  wl_list_for_each (output_config, &configuration->output_configs, link) {
    if (!strcmp(output_config->output_name, output_name)) {
      return output_config;
    }
  }

  wl_list_for_each (output_config, &configuration->output_configs, link) {
    if (!strcmp(output_config->output_name, "*")) {
      return output_config;
    }
  }

  return NULL;
}

struct hikari_pointer_config *
hikari_configuration_resolve_pointer_config(
    struct hikari_configuration *configuration, const char *pointer_name)
{
  struct hikari_pointer_config *pointer_config;
  wl_list_for_each (pointer_config, &configuration->pointer_configs, link) {
    if (!strcmp(pointer_config->name, pointer_name)) {
      return pointer_config;
    }
  }

  wl_list_for_each (pointer_config, &configuration->pointer_configs, link) {
    if (!strcmp(pointer_config->name, "*")) {
      return pointer_config;
    }
  }

  return NULL;
}

struct hikari_switch_config *
hikari_configuration_resolve_switch_config(
    struct hikari_configuration *configuration, const char *switch_name)
{
  struct hikari_switch_config *switch_config;
  wl_list_for_each (switch_config, &configuration->switch_configs, link) {
    if (!strcmp(switch_config->switch_name, switch_name)) {
      return switch_config;
    }
  }

  return NULL;
}

struct hikari_keyboard_config *
hikari_configuration_resolve_keyboard_config(
    struct hikari_configuration *configuration, const char *keyboard_name)
{
  struct hikari_keyboard_config *keyboard_config;
  wl_list_for_each (keyboard_config, &configuration->keyboard_configs, link) {
    if (!strcmp(keyboard_config->keyboard_name, keyboard_name)) {
      return keyboard_config;
    }
  }

  wl_list_for_each (keyboard_config, &configuration->keyboard_configs, link) {
    if (!strcmp(keyboard_config->keyboard_name, "*")) {
      return keyboard_config;
    }
  }

  return NULL;
}
