#include <hikari/configuration.h>

#include <ctype.h>
#include <errno.h>

#include <ucl.h>

#include <dev/evdev/input-event-codes.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_switch.h>

#include <hikari/action.h>
#include <hikari/animation.h>
#include <hikari/action_config.h>
#include <hikari/binding.h>
#include <hikari/binding_config.h>
#include <hikari/color.h>
#include <hikari/config_key.h>
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

extern char **environ;

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
    fprintf(stderr,
        "configuration error: unknown container \"layout\" \"%s\"\n",
        layout_func_name);
    return false;
  }

  return true;
}

static bool
parse_container(
    const ucl_object_t *container_obj, struct hikari_split **container)
{
  bool success = false;
  struct hikari_split_container *ret = NULL;
  hikari_layout_func layout_func = NULL;
  int64_t views = 256;
  bool explicit_nr_of_views = false;
  bool override_nr_of_views = false;
  const char *layout_func_name;
  const ucl_object_t *cur;

  ucl_object_iter_t it = ucl_object_iterate_new(container_obj);
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "a layout container");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "layout")) {
      if (!ucl_object_tostring_safe(cur, &layout_func_name)) {
        fprintf(stderr,
            "configuration error: expected string for container \"layout\"\n");
        goto done;
      }

      if (!parse_layout_func(
              layout_func_name, &layout_func, &views, &explicit_nr_of_views)) {
        goto done;
      }
    } else if (!strcmp(key, "views")) {
      override_nr_of_views = true;
      if (explicit_nr_of_views) {
        fprintf(stderr,
            "configuration error: cannot set \"views\" for \"layout\" \"%s\" "
            "container\n",
            layout_func_name);
        goto done;
      }

      if (!ucl_object_toint_safe(cur, &views)) {
        fprintf(stderr,
            "configuration error: expected integer for container \"views\"\n");
        goto done;
      }

      if (views < 2 || views > 256) {
        fprintf(stderr,
            "configuration error: expected integer between 2 and 256 for "
            "container \"views\"\n");
        goto done;
      }
    } else {
      fprintf(
          stderr, "configuration error: unknown container key \"%s\"\n", key);
      goto done;
    }
  }

  if (layout_func == NULL) {
    fprintf(stderr, "configuration error: container expects \"layout\"\n");
    goto done;
  }

  if (override_nr_of_views && explicit_nr_of_views) {
    fprintf(stderr,
        "configuration error: cannot set \"views\" for \"layout\" \"%s\" "
        "container\n",
        layout_func_name);
    goto done;
  }

  ret = hikari_malloc(sizeof(struct hikari_split_container));
  hikari_split_container_init(ret, views, layout_func);

  success = true;

done:
  ucl_object_iterate_free(it);

  *container = (struct hikari_split *)ret;

  return success;
}

static bool
split_is_container(const ucl_object_t *top,
    const ucl_object_t *bottom,
    const ucl_object_t *left,
    const ucl_object_t *right,
    const ucl_object_t *layout)
{
  return layout != NULL && left == NULL && right == NULL && top == NULL &&
         bottom == NULL;
}

static bool
split_is_vertical(const ucl_object_t *top,
    const ucl_object_t *bottom,
    const ucl_object_t *left,
    const ucl_object_t *right,
    const ucl_object_t *layout)
{
  return left != NULL && right != NULL && top == NULL && bottom == NULL &&
         layout == NULL;
}

static bool
split_is_horizontal(const ucl_object_t *top,
    const ucl_object_t *bottom,
    const ucl_object_t *left,
    const ucl_object_t *right,
    const ucl_object_t *layout)
{
  return top != NULL && bottom != NULL && left == NULL && right == NULL &&
         layout == NULL;
}

static bool
parse_vertical(const ucl_object_t *, struct hikari_split **);

static bool
parse_horizontal(const ucl_object_t *, struct hikari_split **);

static bool
parse_split(const ucl_object_t *split_obj, struct hikari_split **split)
{
  bool success = false;
  struct hikari_split *ret = NULL;
  ucl_type_t type = ucl_object_type(split_obj);

  if (type == UCL_STRING) {
    const char *layout_func_name;
    hikari_layout_func layout_func;
    int64_t views = 256;
    bool explicit_nr_of_views = false;
    if (!ucl_object_tostring_safe(split_obj, &layout_func_name)) {
      fprintf(stderr,
          "configuration error: expected string for container layout\n");
      goto done;
    }

    if (!parse_layout_func(
            layout_func_name, &layout_func, &views, &explicit_nr_of_views)) {
      goto done;
    }

    ret = hikari_malloc(sizeof(struct hikari_split_container));
    hikari_split_container_init(
        (struct hikari_split_container *)ret, views, layout_func);

  } else if (type == UCL_OBJECT) {
    const ucl_object_t *left = ucl_object_lookup(split_obj, "left");
    const ucl_object_t *right = ucl_object_lookup(split_obj, "right");
    const ucl_object_t *top = ucl_object_lookup(split_obj, "top");
    const ucl_object_t *bottom = ucl_object_lookup(split_obj, "bottom");
    const ucl_object_t *layout = ucl_object_lookup(split_obj, "layout");

    if (split_is_vertical(top, bottom, left, right, layout)) {
      if (!parse_vertical(split_obj, &ret)) {
        fprintf(
            stderr, "configuration error: failed to parse vertical split\n");
        goto done;
      }
    } else if (split_is_horizontal(top, bottom, left, right, layout)) {
      if (!parse_horizontal(split_obj, &ret)) {
        fprintf(
            stderr, "configuration error: failed to parse horizontal split\n");
        goto done;
      }
    } else if (split_is_container(top, bottom, left, right, layout)) {
      if (!parse_container(split_obj, &ret)) {
        fprintf(stderr, "configuration error: failed to parse container\n");
        goto done;
      }
    } else {
      fprintf(
          stderr, "configuration error: failed to determine layout element\n");
      goto done;
    }
  } else {
    fprintf(stderr,
        "configuration error: expected string or object for layout element\n");
    goto done;
  }

  success = true;

done:
  *split = ret;

  return success;
}

static bool
parse_scale_value(
    const ucl_object_t *scale_value_obj, const char *name, double *scale)
{
  bool success = false;
  double ret;

  if (!ucl_object_todouble_safe(scale_value_obj, &ret)) {
    fprintf(stderr, "configuration error: expected float for \"%s\"\n", name);
    goto done;
  }

  if (ret < hikari_split_scale_min || ret > hikari_split_scale_max) {
    fprintf(stderr,
        "configuration error: \"%s\" of \"%.2f\" is not between \"0.1\" "
        "and "
        "\"0.9\"\n",
        name,
        ret);
    goto done;
  }

  success = true;

done:
  *scale = ret;

  return success;
}

static bool
parse_scale_dynamic(
    const ucl_object_t *scale_obj, struct hikari_split_scale_dynamic *scale)
{
  bool success = false;

  const ucl_object_t *min_obj = ucl_object_lookup(scale_obj, "min");
  const ucl_object_t *max_obj = ucl_object_lookup(scale_obj, "max");

  if (min_obj != NULL) {
    if (!parse_scale_value(min_obj, "min", &scale->min)) {
      goto done;
    }
  } else {
    scale->min = hikari_split_scale_min;
  }

  if (max_obj != NULL) {
    if (!parse_scale_value(max_obj, "max", &scale->max)) {
      goto done;
    }
  } else {
    scale->max = hikari_split_scale_max;
  }

  success = true;

done:

  return success;
}

static bool
parse_scale(const ucl_object_t *scale_obj, struct hikari_split_scale *scale)
{
  bool success = false;

  if (scale_obj != NULL) {
    ucl_type_t type = ucl_object_type(scale_obj);

    switch (type) {
      case UCL_FLOAT:
        scale->type = HIKARI_SPLIT_SCALE_TYPE_FIXED;
        if (!parse_scale_value(scale_obj, "scale", &scale->scale.fixed)) {
          goto done;
        }
        break;

      case UCL_OBJECT:
        scale->type = HIKARI_SPLIT_SCALE_TYPE_DYNAMIC;

        if (!parse_scale_dynamic(scale_obj, &scale->scale.dynamic)) {
          goto done;
        }
        break;

      default:
        goto done;
    }
  }

  success = true;

done:

  return success;
}

#define PARSE_SPLIT(name, NAME, first, FIRST, second, SECOND)                  \
  static bool parse_##name(                                                    \
      const ucl_object_t *name##_obj, struct hikari_split **name)              \
  {                                                                            \
    bool success = false;                                                      \
    bool found_orientation = false;                                            \
    const ucl_object_t *cur;                                                   \
    struct hikari_split_##name *ret = NULL;                                    \
    enum hikari_split_##name##_orientation orientation =                       \
        HIKARI_##NAME##_SPLIT_ORIENTATION_##FIRST;                             \
    struct hikari_split *first = NULL;                                         \
    struct hikari_split *second = NULL;                                        \
    struct hikari_split_scale scale;                                           \
    scale.type = HIKARI_SPLIT_SCALE_TYPE_FIXED;                                \
    scale.scale.fixed = hikari_split_scale_default;                            \
                                                                               \
    ucl_object_iter_t it = ucl_object_iterate_new(name##_obj);                 \
                                                                               \
    while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {               \
      hikari_config_warn_duplicate_key(cur, "a layout split");                 \
                                                                               \
      const char *key = ucl_object_key(cur);                                   \
                                                                               \
      if (!strcmp(key, "scale")) {                                             \
        if (!parse_scale(cur, &scale)) {                                       \
          goto done;                                                           \
        }                                                                      \
      } else if (!strcmp(key, #first)) {                                       \
        if (!parse_split(cur, &first)) {                                       \
          fprintf(stderr,                                                      \
              "configuration error: invalid \"" #first "\" for \"" #name       \
              "\" split\n");                                                   \
          goto done;                                                           \
        }                                                                      \
                                                                               \
        if (!found_orientation) {                                              \
          orientation = HIKARI_##NAME##_SPLIT_ORIENTATION_##FIRST;             \
          found_orientation = true;                                            \
        }                                                                      \
      } else if (!strcmp(key, #second)) {                                      \
        if (!parse_split(cur, &second)) {                                      \
          if (first != NULL) {                                                 \
            hikari_split_free(first);                                          \
          }                                                                    \
          fprintf(stderr,                                                      \
              "configuration error: invalid \"" #second "\" for \"" #name      \
              "\" split\n");                                                   \
          goto done;                                                           \
        }                                                                      \
                                                                               \
        if (!found_orientation) {                                              \
          orientation = HIKARI_##NAME##_SPLIT_ORIENTATION_##SECOND;            \
          found_orientation = true;                                            \
        }                                                                      \
      } else {                                                                 \
        fprintf(stderr,                                                        \
            "configuration error: unknown \"" #name "\" key \"%s\"\n",         \
            key);                                                              \
        goto done;                                                             \
      }                                                                        \
    }                                                                          \
                                                                               \
    if (first == NULL) {                                                       \
      fprintf(stderr,                                                          \
          "configuration error: missing \"" #first "\" for \"" #name           \
          "\" split\n");                                                       \
      goto done;                                                               \
    }                                                                          \
                                                                               \
    if (second == NULL) {                                                      \
      hikari_split_free(first);                                                \
      fprintf(stderr,                                                          \
          "configuration error: missing \"" #second "\" for " #name            \
          " split\n");                                                         \
      goto done;                                                               \
    }                                                                          \
                                                                               \
    ret = hikari_malloc(sizeof(struct hikari_split_##name));                   \
    hikari_split_##name##_init(ret, &scale, orientation, first, second);       \
                                                                               \
    success = true;                                                            \
                                                                               \
  done:                                                                        \
    ucl_object_iterate_free(it);                                               \
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
copy_in_config_string(const ucl_object_t *obj)
{
  const char *str;
  char *ret;

  bool success = ucl_object_tostring_safe(obj, &str);

  if (success) {
    size_t len = strlen(str);
    ret = hikari_malloc(len + 1);
    strcpy(ret, str);

    return ret;
  } else {
    fprintf(stderr, "configuration error: expected string\n");
    return NULL;
  }
}

/* [COMMENT] Function purpose: Parse one colourscheme value into normalised
RGBA, accepting either form:

  integer  0xRRGGBB      -- historical form, always fully opaque
  string   "#RRGGBB"     -- same, written explicitly
  string   "#RRGGBBAA"   -- carries alpha in the low byte

Alpha deliberately cannot be expressed as an integer. UCL parses `0x0080FFCC`
and `0x80FFCC` to values that a magnitude test cannot tell apart, so an
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
    const ucl_object_t *obj,
    const char *key,
    float dst[static 4])
{
  if (ucl_object_type(obj) == UCL_STRING) {
    const char *str;

    if (!ucl_object_tostring_safe(obj, &str)) {
      fprintf(stderr,
          "configuration error: expected \"#RRGGBB\", \"#RRGGBBAA\" or a "
          "palette reference for \"%s\"\n",
          key);
      return false;
    }

    /* [COMMENT] Action purpose: Anything not starting with '#' is taken to be a
    palette reference. UCL parses a bare `color3` as a string, so `active =
    color3` and `active = "color3"` are the same thing and both work. */
    if (str[0] != '#') {
      int index;

      if (configuration == NULL || !parse_palette_reference(str, &index)) {
        fprintf(stderr,
            "configuration error: expected \"#RRGGBB\", \"#RRGGBBAA\"%s for "
            "\"%s\", got \"%s\"\n",
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
      fprintf(stderr,
          "configuration error: \"%s\" must have 6 or 8 hex digits, got %zu\n",
          key,
          len);
      return false;
    }

    for (size_t i = 1; i <= len; i++) {
      if (!isxdigit((unsigned char)str[i])) {
        fprintf(stderr,
            "configuration error: invalid hex digit in \"%s\" value \"%s\"\n",
            key,
            str);
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

  int64_t color;
  if (!ucl_object_toint_safe(obj, &color)) {
    fprintf(stderr,
        "configuration error: expected integer or \"#RRGGBB[AA]\" string for "
        "\"%s\"\n",
        key);
    return false;
  }

  /* [COMMENT] Action purpose: Reject out-of-range values instead of silently
  truncating them in the cast below -- -1 would otherwise become white, and
  0x1FF0000 would become 0xFF0000. */
  if (color < 0 || color > 0xFFFFFF) {
    fprintf(stderr,
        "configuration error: \"%s\" must be between 0x000000 and 0xFFFFFF\n",
        key);
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
    struct hikari_configuration *configuration, const ucl_object_t *palette_obj)
{
  bool success = false;
  ucl_object_iter_t it = ucl_object_iterate_new(palette_obj);

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "ui.palette");

    const char *key = ucl_object_key(cur);
    int index;

    if (!parse_palette_reference(key, &index)) {
      fprintf(stderr,
          "configuration error: expected \"color0\"-\"color15\" in "
          "\"palette\", got \"%s\"\n",
          key);
      goto done;
    }

    if (!parse_color(NULL, cur, key, configuration->palette[index])) {
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_colorscheme(struct hikari_configuration *configuration,
    const ucl_object_t *colorscheme_obj)
{
  bool success = false;
  const ucl_object_t *cur;

  ucl_object_iter_t it = ucl_object_iterate_new(colorscheme_obj);

  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "ui.colorscheme");

    const char *key = ucl_object_key(cur);

    if (!strcmp("selected", key)) {
      if (!parse_color(
              configuration, cur, key, configuration->indicator_selected)) {
        goto done;
      }
    } else if (!strcmp("grouped", key)) {
      if (!parse_color(
              configuration, cur, key, configuration->indicator_grouped)) {
        goto done;
      }
    } else if (!strcmp("first", key)) {
      if (!parse_color(
              configuration, cur, key, configuration->indicator_first)) {
        goto done;
      }
    } else if (!strcmp("conflict", key)) {
      if (!parse_color(
              configuration, cur, key, configuration->indicator_conflict)) {
        goto done;
      }
    } else if (!strcmp("insert", key)) {
      if (!parse_color(
              configuration, cur, key, configuration->indicator_insert)) {
        goto done;
      }
    } else if (!strcmp("active", key)) {
      if (!parse_color(
              configuration, cur, key, configuration->border_active)) {
        goto done;
      }
    } else if (!strcmp("inactive", key)) {
      if (!parse_color(
              configuration, cur, key, configuration->border_inactive)) {
        goto done;
      }
    } else if (!strcmp("foreground", key)) {
      if (!parse_color(
              configuration, cur, key, configuration->foreground)) {
        goto done;
      }
    } else if (!strcmp("background", key)) {
      if (!parse_color(
              configuration, cur, key, configuration->clear)) {
        goto done;
      }
    } else if (!strcmp("bar", key)) {
      /* [COMMENT] Action purpose: The top bar's own background. It previously
      borrowed "background" (the output clear colour), which meant it could not
      be tinted or made translucent without also changing the desktop
      background behind every window. See DECISIONS_LOG Phase 60. */
      if (!parse_color(
              configuration, cur, key, configuration->bar)) {
        goto done;
      }
    } else {
      fprintf(stderr, "configuration error: unknown color key \"%s\"\n", key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_execute(
    struct hikari_configuration *configuration, const ucl_object_t *obj)
{
  bool success = false;
  const ucl_object_t *cur;
  const char *key;

  ucl_object_iter_t it = ucl_object_iterate_new(obj);
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "marks");

    key = ucl_object_key(cur);

    struct hikari_exec *execute = NULL;

    if (strlen(key) != 1 || !(key[0] >= 'a' && key[0] <= 'z')) {
      fprintf(stderr,
          "configuration error: invalid \"marks\" register \"%s\"\n",
          key);
      goto done;
    } else {
      int nr = key[0] - 'a';
      execute = &configuration->execs[nr];
    }

    assert(execute != NULL);

    char *command = copy_in_config_string(cur);

    if (command != NULL) {
      execute->command = command;
    } else {
      fprintf(stderr,
          "configuration error: invalid command for \"marks\" "
          "register \"%c\"\n",
          key[0]);
      goto done;
    }
    execute = NULL;
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_view_configs(
    struct hikari_configuration *configuration, const ucl_object_t *obj)
{
  bool success = false;
  ucl_object_iter_t it = ucl_object_iterate_new(obj);

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "views");

    struct hikari_view_config *view_config =
        hikari_malloc(sizeof(struct hikari_view_config));

    hikari_view_config_init(view_config);
    wl_list_insert(&configuration->view_configs, &view_config->link);

    const char *key = ucl_object_key(cur);
    size_t keylen = strlen(key);

    view_config->app_id = hikari_malloc(keylen + 1);
    strcpy(view_config->app_id, key);

    if (!hikari_view_config_parse(view_config, cur)) {
      fprintf(stderr,
          "configuration error: failed to parse \"views\" \"%s\"\n",
          key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

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
    fprintf(stderr, "configuration error: unknown mouse button \"%s\"\n", str);
    return false;
  }

  return true;
}

static bool
parse_action(const char *action_name,
    const ucl_object_t *action_obj,
    const char **command)
{
  if (!ucl_object_tostring_safe(action_obj, command)) {
    fprintf(stderr,
        "configuration error: expected string for \"action\" command\n");
    return false;
  }

  return true;
}

static bool
parse_actions(
    struct hikari_configuration *configuration, const ucl_object_t *actions_obj)
{
  bool success = false;
  struct hikari_action_config *action_config;

  const ucl_object_t *cur;
  ucl_object_iter_t it = ucl_object_iterate_new(actions_obj);
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "actions");

    const char *key = ucl_object_key(cur);
    const char *command;

    if (!parse_action(key, cur, &command)) {
      goto done;
    }

    action_config = hikari_malloc(sizeof(struct hikari_action_config));
    hikari_action_config_init(action_config, key, command);

    wl_list_insert(&configuration->action_configs, &action_config->link);
  }

  success = true;

done:
  return success;
}

static bool
parse_layout(char layout_register,
    const ucl_object_t *layout_obj,
    struct hikari_split **split)
{
  struct hikari_split *ret = NULL;

  if (!parse_split(layout_obj, &ret)) {
    fprintf(stderr,
        "configuration error: failed to parse layout for register \"%c\"\n",
        layout_register);
    return false;
  }

  *split = ret;

  return true;
}

static bool
parse_layouts(
    struct hikari_configuration *configuration, const ucl_object_t *layouts_obj)
{
  bool success = false;
  struct hikari_layout_config *layout_config;
  struct hikari_split *split;

  const ucl_object_t *cur;
  ucl_object_iter_t it = ucl_object_iterate_new(layouts_obj);
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "layouts");

    const char *key = ucl_object_key(cur);

    if (strlen(key) > 1 ||
        !((key[0] >= 'a' && key[0] <= 'z') || isdigit(key[0]))) {
      fprintf(stderr, "configuration error: expected layout register name\n");
      goto done;
    }

    char layout_register = key[0];

    if (!parse_layout(layout_register, cur, &split)) {
      goto done;
    }

    layout_config = hikari_malloc(sizeof(struct hikari_layout_config));
    hikari_layout_config_init(layout_config, layout_register, split);

    wl_list_insert(&configuration->layout_configs, &layout_config->link);
  }

  success = true;

done:
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
    struct hikari_configuration *configuration, const ucl_object_t *layout_obj)
{
  bool success = false;
  struct hikari_layout_policy *policy = &configuration->layout_policy;

  const ucl_object_t *cur;
  ucl_object_iter_t it = ucl_object_iterate_new(layout_obj);

  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "layout");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "auto") || !strcmp(key, "reflow-on-close")) {
      bool value;

      if (!ucl_object_toboolean_safe(cur, &value)) {
        fprintf(stderr,
            "configuration error: expected boolean for \"%s\"\n",
            key);
        goto done;
      }

      if (!strcmp(key, "auto")) {
        policy->automatic = value;
      } else {
        policy->on_close = value;
      }
    } else if (!strcmp(key, "insert")) {
      const char *value;

      if (!ucl_object_tostring_safe(cur, &value)) {
        fprintf(stderr,
            "configuration error: expected string for \"insert\"\n");
        goto done;
      }

      if (!strcmp(value, "append")) {
        policy->insert = HIKARI_LAYOUT_INSERT_APPEND;
      } else if (!strcmp(value, "prepend")) {
        policy->insert = HIKARI_LAYOUT_INSERT_PREPEND;
      } else {
        fprintf(stderr,
            "configuration error: expected \"append\" or \"prepend\" for "
            "\"insert\", got \"%s\"\n",
            value);
        goto done;
      }
    } else if (!strcmp(key, "default-register")) {
      const char *value;

      if (!ucl_object_tostring_safe(cur, &value)) {
        fprintf(stderr,
            "configuration error: expected string for "
            "\"default-register\"\n");
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
        fprintf(stderr,
            "configuration error: \"default-register\" must be a single "
            "layout register (\"a\"-\"z\" or \"0\"-\"9\"), got "
            "\"%s\"\n",
            value);
        goto done;
      }

      policy->default_register = value[0];
    } else {
      fprintf(
          stderr, "configuration error: unknown \"layout\" key \"%s\"\n", key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

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
parse_keyboard_bindings(struct hikari_configuration *configuration,
    const ucl_object_t *bindings_obj)
{
  bool success = false;
  const ucl_object_t *cur;
  struct hikari_binding_config *binding_config;

  ucl_object_iter_t it = ucl_object_iterate_new(bindings_obj);
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "bindings.keyboard");

    const char *key = ucl_object_key(cur);

    binding_config = hikari_malloc(sizeof(struct hikari_binding_config));
    wl_list_insert(
        &configuration->keyboard_binding_configs, &binding_config->link);

    if (!hikari_binding_config_key_parse(&binding_config->key, key)) {
      goto done;
    }

    struct hikari_action *action = &binding_config->action;
    hikari_action_init(action);

    if (!hikari_action_parse(action, &configuration->action_configs, cur)) {
      goto done;
    }
  }

  success = true;

done:

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
parse_mouse_bindings(struct hikari_configuration *configuration,
    const ucl_object_t *bindings_obj)
{
  bool success = false;
  const ucl_object_t *cur;
  struct hikari_binding_config *binding_config;

  ucl_object_iter_t it = ucl_object_iterate_new(bindings_obj);
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "bindings.mouse");

    const char *key = ucl_object_key(cur);

    binding_config = hikari_malloc(sizeof(struct hikari_binding_config));
    wl_list_insert(
        &configuration->mouse_binding_configs, &binding_config->link);

    if (!hikari_binding_config_button_parse(&binding_config->key, key)) {
      goto done;
    }

    struct hikari_action *action = &binding_config->action;
    hikari_action_init(action);

    if (!hikari_action_parse(action, &configuration->action_configs, cur)) {
      goto done;
    }
  }

  success = true;

done:

  return success;
}

static bool
parse_bindings(struct hikari_configuration *configuration,
    const ucl_object_t *bindings_obj)
{
  bool success = false;
  const ucl_object_t *cur;

  ucl_object_iter_t it = ucl_object_iterate_new(bindings_obj);
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "bindings");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "keyboard")) {
      if (!parse_keyboard_bindings(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "mouse")) {
      if (!parse_mouse_bindings(configuration, cur)) {
        goto done;
      }
    } else {
      fprintf(stderr,
          "configuration error: unexpected \"bindings\" section \"%s\"\n",
          key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_pointer_config(struct hikari_pointer_config *pointer_config,
    const ucl_object_t *pointer_config_obj)
{
  bool success = false;
  const char *pointer_name = pointer_config->name;
  const ucl_object_t *cur;

  ucl_object_iter_t it = ucl_object_iterate_new(pointer_config_obj);
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "an inputs.pointers entry");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "accel")) {
      double accel;
      if (!ucl_object_todouble_safe(cur, &accel)) {
        fprintf(stderr,
            "configuration error: expected float for \"%s\" \"accel\"\n",
            pointer_name);
        goto done;
      }

      if (accel < -1.0 || accel > 1.0) {
        fprintf(
            stderr, "configuration error: expected float between -1 and 1\n");
        goto done;
      }

      hikari_pointer_config_set_accel(pointer_config, accel);
    } else if (!strcmp(key, "accel-profile")) {
      const char *accel_profile;
      if (!ucl_object_tostring_safe(cur, &accel_profile)) {
        fprintf(stderr,
            "configuration error: expected string \"%s\" for "
            "\"accel-profile\"\n",
            pointer_name);
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
        fprintf(stderr,
            "configuration error: unkown \"accel-profile\" \"%s\" for \"%s\"\n",
            accel_profile,
            pointer_name);
        goto done;
      }
    } else if (!strcmp(key, "scroll-method")) {
      const char *scroll_method;
      if (!ucl_object_tostring_safe(cur, &scroll_method)) {
        fprintf(stderr,
            "configuration error: expected string \"%s\" for "
            "\"scroll-method\"\n",
            pointer_name);
        goto done;
      }

      if (!strcmp(scroll_method, "on-button-down")) {
        hikari_pointer_config_set_scroll_method(
            pointer_config, LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
      } else if (!strcmp(scroll_method, "no-scroll")) {
        hikari_pointer_config_set_scroll_method(
            pointer_config, LIBINPUT_CONFIG_SCROLL_NO_SCROLL);
      } else {
        fprintf(stderr,
            "configuration error: unkown \"scroll-method\" \"%s\" for \"%s\"\n",
            scroll_method,
            pointer_name);
        goto done;
      }
    } else if (!strcmp(key, "scroll-button")) {
      const char *scroll_button;
      uint32_t scroll_button_keycode;
      if (!ucl_object_tostring_safe(cur, &scroll_button)) {
        fprintf(stderr,
            "configuration error: expected string for \"scroll-button\"\n");
        goto done;
      }

      if (!parse_mouse_button(scroll_button, &scroll_button_keycode)) {
        fprintf(
            stderr, "configuration error: failed to parse \"scroll-button\"\n");
        goto done;
      }

      hikari_pointer_config_set_scroll_button(
          pointer_config, scroll_button_keycode);
    } else if (!strcmp(key, "disable-while-typing")) {
      bool disable_while_typing;
      if (!ucl_object_toboolean_safe(cur, &disable_while_typing)) {
        fprintf(stderr,
            "configuration error: expected boolean for "
            "\"disable-while-typing\"\n");
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
      if (!ucl_object_toboolean_safe(cur, &tap)) {
        fprintf(stderr,
            "configuration error: expected boolean for "
            "\"tap\"\n");
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
      if (!ucl_object_toboolean_safe(cur, &tap_drag)) {
        fprintf(stderr,
            "configuration error: expected boolean for "
            "\"tap-drag\"\n");
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
      if (!ucl_object_toboolean_safe(cur, &tap_drag_lock)) {
        fprintf(stderr,
            "configuration error: expected boolean for "
            "\"tap-drag-lock\"\n");
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
      if (!ucl_object_toboolean_safe(cur, &middle_emulation)) {
        fprintf(stderr,
            "configuration error: expected boolean for "
            "\"middle-emulation\"\n");
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
      if (!ucl_object_toboolean_safe(cur, &natural_scrolling)) {
        fprintf(stderr,
            "configuration error: expected boolean for "
            "\"natural-scrolling\"\n");
        goto done;
      }

      hikari_pointer_config_set_natural_scrolling(
          pointer_config, natural_scrolling);
    } else {
      fprintf(stderr,
          "configuration error: unknown \"pointer\" configuration key \"%s\" "
          "for "
          "\"%s\"\n",
          key,
          pointer_name);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_pointers(struct hikari_configuration *configuration,
    const ucl_object_t *pointers_obj)
{
  bool success = false;

  ucl_object_iter_t it = ucl_object_iterate_new(pointers_obj);
  struct hikari_pointer_config *pointer_config;

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, true)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "inputs.pointers");

    const char *pointer_name = ucl_object_key(cur);

    pointer_config = hikari_malloc(sizeof(struct hikari_pointer_config));
    hikari_pointer_config_init(pointer_config, pointer_name);

    wl_list_insert(&configuration->pointer_configs, &pointer_config->link);

    if (!parse_pointer_config(pointer_config, cur)) {
      goto done;
    }
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

  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_keyboards(struct hikari_configuration *configuration,
    const ucl_object_t *keyboards_obj)
{
  bool success = false;
  const ucl_object_t *cur;
  struct hikari_keyboard_config *keyboard_config;

  ucl_object_iter_t it = ucl_object_iterate_new(keyboards_obj);
  while ((cur = ucl_object_iterate_safe(it, true)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "inputs.keyboards");

    const char *keyboard_name = ucl_object_key(cur);

    keyboard_config = hikari_malloc(sizeof(struct hikari_keyboard_config));
    hikari_keyboard_config_init(keyboard_config, keyboard_name);

    wl_list_insert(&configuration->keyboard_configs, &keyboard_config->link);

    if (!hikari_keyboard_config_parse(keyboard_config, cur)) {
      goto done;
    }
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
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_switches(struct hikari_configuration *configuration,
    const ucl_object_t *switches_obj)
{
  bool success = false;
  const ucl_object_t *cur;
  struct hikari_switch_config *switch_config;

  ucl_object_iter_t it = ucl_object_iterate_new(switches_obj);
  while ((cur = ucl_object_iterate_safe(it, true)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "inputs.switches");

    const char *key = ucl_object_key(cur);

    switch_config = hikari_malloc(sizeof(struct hikari_switch_config));
    wl_list_insert(&configuration->switch_configs, &switch_config->link);

    switch_config->switch_name = strdup(key);

    if (!hikari_action_parse(
            &switch_config->action, &configuration->action_configs, cur)) {
      goto done;
    }
  }

  success = true;

done:
  // [COMMENT] Action purpose: releases the UCL iterator on both the
  // successful and failed parsing paths.
  ucl_object_iterate_free(it);

  return success;
}

/* Function purpose: Parse the `inputs { gestures {} }` block into the
configuration's gesture binding list. */
static bool
parse_gestures(struct hikari_configuration *configuration,
    const ucl_object_t *gestures_obj)
{
  bool success = false;
  const ucl_object_t *cur;
  struct hikari_gesture_binding_config *gesture_binding_config;

  ucl_object_iter_t it = ucl_object_iterate_new(gestures_obj);
  while ((cur = ucl_object_iterate_safe(it, true)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "inputs.gestures");

    const char *key = ucl_object_key(cur);

    gesture_binding_config =
        hikari_malloc(sizeof(struct hikari_gesture_binding_config));
    wl_list_insert(&configuration->gesture_binding_configs,
        &gesture_binding_config->link);

    if (!hikari_gesture_binding_config_key_parse(key,
            &gesture_binding_config->type,
            &gesture_binding_config->direction,
            &gesture_binding_config->fingers)) {
      fprintf(stderr,
          "configuration error: invalid gesture binding \"%s\"\n",
          key);
      goto done;
    }

    struct hikari_action *action = &gesture_binding_config->action;
    hikari_action_init(action);

    if (!hikari_action_parse(action, &configuration->action_configs, cur)) {
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_inputs(
    struct hikari_configuration *configuration, const ucl_object_t *inputs_obj)
{
  bool success = false;
  ucl_object_iter_t it = ucl_object_iterate_new(inputs_obj);

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, true)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "inputs");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "pointers")) {
      if (!parse_pointers(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "keyboards")) {
      if (!parse_keyboards(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "switches")) {
      if (!parse_switches(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "gestures")) {
      if (!parse_gestures(configuration, cur)) {
        goto done;
      }
    } else {
      fprintf(stderr,
          "configuration error: unknown \"inputs\" configuration key \"%s\"",
          key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_background(const ucl_object_t *background_obj,
    char **background,
    enum hikari_background_fit *fit)
{
  bool success = false;

  ucl_object_iter_t it = ucl_object_iterate_new(background_obj);
  bool has_background = false;

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, true)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "an outputs background");

    const char *key = ucl_object_key(cur);
    if (!strcmp(key, "path")) {
      has_background = true;
      *background = copy_in_config_string(cur);
    } else if (!strcmp(key, "fit")) {
      const char *fit_value;
      if (!ucl_object_tostring_safe(cur, &fit_value)) {
        fprintf(stderr,
            "configuration error: expected string for \"background\" "
            "\"fit\"\n");
        goto done;
      }
      if (!strcmp(fit_value, "center")) {
        *fit = HIKARI_BACKGROUND_CENTER;
      } else if (!strcmp(fit_value, "stretch")) {
        *fit = HIKARI_BACKGROUND_STRETCH;
      } else if (!strcmp(fit_value, "tile")) {
        *fit = HIKARI_BACKGROUND_TILE;
      } else {
        fprintf(stderr,
            "configuration error: unexpected \"background\" \"fit\" \"%s\"\n",
            fit_value);
        goto done;
      }
    } else {
      fprintf(stderr,
          "configuration error: unknown \"background\" configuration key "
          "\"%s\"\n",
          key);
      goto done;
    }
  }

  if (!has_background) {
    fprintf(
        stderr, "configuration error: missing \"path\" for \"background\"\n");
    goto done;
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_output_config(struct hikari_output_config *output_config,
    const ucl_object_t *output_config_obj)

{
  bool success = false;
  ucl_object_iter_t it = ucl_object_iterate_new(output_config_obj);

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, true)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "an outputs entry");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "background")) {
      ucl_type_t type = ucl_object_type(cur);

      if (type == UCL_STRING) {
        char *background = copy_in_config_string(cur);

        if (background == NULL) {
          fprintf(
              stderr, "configuration error: invalid \"background\" value\n");
          goto done;
        }

        hikari_output_config_set_background(output_config, background);
      } else if (type == UCL_OBJECT) {
        char *background;
        enum hikari_background_fit background_fit;

        if (!parse_background(cur, &background, &background_fit)) {
          goto done;
        }

        hikari_output_config_set_background(output_config, background);
        hikari_output_config_set_background_fit(output_config, background_fit);
      } else {
        fprintf(stderr,
            "configuration error: expected string or object for "
            "\"background\"\n");
        goto done;
      }
    } else if (!strcmp(key, "position")) {
      struct hikari_position_config position;
      if (!hikari_position_config_absolute_parse(&position, cur)) {
        fprintf(stderr,
            "configuration error: could not parse \"output\" \"position\"");
        goto done;
      }

      hikari_output_config_set_position(output_config, position);
    } else {
      // [COMMENT] Action purpose: Unknown output keys must fail the parse, not
      // just log -- silently accepting typos (e.g. "postion") would leave a
      // running compositor that ignores the intended rule. This matches the
      // strict behaviour of every other unknown-key branch in this parser.
      fprintf(stderr,
          "configuration error: unknown \"outputs\" configuration key \"%s\"\n",
          key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_outputs(
    struct hikari_configuration *configuration, const ucl_object_t *outputs_obj)
{
  bool success = false;

  ucl_object_iter_t it = ucl_object_iterate_new(outputs_obj);
  struct hikari_output_config *output_config;

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, true)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "outputs");

    const char *output_name = ucl_object_key(cur);

    output_config = hikari_malloc(sizeof(struct hikari_output_config));
    hikari_output_config_init(output_config, output_name);

    wl_list_insert(&configuration->output_configs, &output_config->link);

    if (!parse_output_config(output_config, cur)) {
      fprintf(stderr,
          "configuration error: failed to parse \"outputs\" configuration\n");
      goto done;
    }
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
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_border(
    struct hikari_configuration *configuration, const ucl_object_t *border_obj)
{
  int64_t border;

  if (!ucl_object_toint_safe(border_obj, &border)) {
    fprintf(stderr, "configuration error: expected integer for \"border\"\n");
    return false;
  }

  configuration->border = border;

  return true;
}

static bool
parse_gap(
    struct hikari_configuration *configuration, const ucl_object_t *gap_obj)
{
  int64_t gap;

  if (!ucl_object_toint_safe(gap_obj, &gap)) {
    fprintf(stderr, "configuration error: expected integer for \"gap\"\n");
    return false;
  }

  configuration->gap = gap;

  return true;
}

static bool
parse_step(
    struct hikari_configuration *configuration, const ucl_object_t *step_obj)
{
  int64_t step;

  if (!ucl_object_toint_safe(step_obj, &step)) {
    fprintf(stderr, "configuration error: expected integer for \"step\"\n");
    return false;
  }

  configuration->step = step;

  return true;
}

static bool
parse_font(
    struct hikari_configuration *configuration, const ucl_object_t *font_obj)
{
  const char *font;

  if (!ucl_object_tostring_safe(font_obj, &font)) {
    fprintf(stderr, "configuration error: expected string for \"font\"\n");
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
    struct hikari_lock_config *lock_config, const ucl_object_t *blur_obj)
{
  if (ucl_object_type(blur_obj) == UCL_BOOLEAN) {
    bool blur;

    if (!ucl_object_toboolean_safe(blur_obj, &blur)) {
      fprintf(stderr, "configuration error: expected boolean for \"blur\"\n");
      return false;
    }

    lock_config->blur = blur;

    return true;
  }

  if (ucl_object_type(blur_obj) != UCL_OBJECT) {
    fprintf(stderr,
        "configuration error: expected boolean or object for \"blur\"\n");
    return false;
  }

  bool success = false;
  ucl_object_iter_t it = ucl_object_iterate_new(blur_obj);

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "ui.lock.blur");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "radius") || !strcmp(key, "passes")) {
      int64_t value;

      if (!ucl_object_toint_safe(cur, &value) || value < 0 || value > 512) {
        fprintf(stderr,
            "configuration error: expected integer between 0 and 512 for "
            "\"%s\"\n",
            key);
        goto done;
      }

      if (!strcmp(key, "radius")) {
        lock_config->blur_radius = (int)value;
      } else {
        lock_config->blur_passes = (int)value;
      }
    } else {
      fprintf(stderr, "configuration error: unknown \"blur\" key \"%s\"\n", key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

/* [COMMENT] Function purpose: Parse the `ui { bar { ... } }` block -- how long a
top bar block may be before it is capped and scrolled, and how fast. */
static bool
parse_bar(struct hikari_bar_config *bar_config, const ucl_object_t *bar_obj)
{
  bool success = false;
  ucl_object_iter_t it = ucl_object_iterate_new(bar_obj);

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "ui.bar");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "max-block-chars")) {
      int64_t chars;

      /* [COMMENT] Action purpose: 256 is HIKARI_BAR_MAX_CAP_CHARS in
      src/bar.c, which sizes the per-block scroll buffer. The two must not
      drift. 0 disables capping and scrolling entirely. */
      if (!ucl_object_toint_safe(cur, &chars) || chars < 0 || chars > 256) {
        fprintf(stderr,
            "configuration error: expected integer 0-256 for "
            "\"max-block-chars\"\n");
        goto done;
      }

      bar_config->max_block_chars = (int)chars;
    } else if (!strcmp(key, "scroll-interval")) {
      int64_t interval;

      /* [COMMENT] Action purpose: Floor at 50ms. The timer repaints the whole
      bar on every step, so an unbounded value here is a way to make the
      compositor busy-render itself; below this the motion is not readable
      anyway. */
      if (!ucl_object_toint_safe(cur, &interval) || interval < 50 ||
          interval > 10000) {
        fprintf(stderr,
            "configuration error: expected integer 50-10000 for "
            "\"scroll-interval\"\n");
        goto done;
      }

      bar_config->scroll_interval = (int)interval;
    } else if (!strcmp(key, "scroll-separator")) {
      const char *separator;

      if (!ucl_object_tostring_safe(cur, &separator)) {
        fprintf(stderr,
            "configuration error: expected string for \"scroll-separator\"\n");
        goto done;
      }

      hikari_free(bar_config->scroll_separator);
      bar_config->scroll_separator = hikari_malloc(strlen(separator) + 1);
      strcpy(bar_config->scroll_separator, separator);
    } else {
      fprintf(stderr,
          "configuration error: unknown \"bar\" key \"%s\"\n",
          key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

/* [COMMENT] Function purpose: Parse `ui { lock { ... } }` -- the clock, the
blur, and how long the screen stays lit while locked. */
static bool
parse_lock(
    struct hikari_configuration *configuration, const ucl_object_t *lock_obj)
{
  /* [COMMENT] Action purpose: Takes the whole configuration rather than just
  the lock block, because `clock-color` accepts a palette reference and the
  palette lives on the configuration. The lock block's own fields are reached
  through this one extra hop. */
  struct hikari_lock_config *lock_config = &configuration->lock;

  bool success = false;
  ucl_object_iter_t it = ucl_object_iterate_new(lock_obj);

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "ui.lock");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "blur")) {
      if (!parse_lock_blur(lock_config, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "clock")) {
      bool clock;

      if (!ucl_object_toboolean_safe(cur, &clock)) {
        fprintf(stderr, "configuration error: expected boolean for \"clock\"\n");
        goto done;
      }

      lock_config->clock = clock;
    } else if (!strcmp(key, "clock-format") || !strcmp(key, "date-format")) {
      const char *format;

      if (!ucl_object_tostring_safe(cur, &format)) {
        fprintf(stderr,
            "configuration error: expected string for \"%s\"\n",
            key);
        goto done;
      }

      /* [COMMENT] Action purpose: Copy rather than borrow. The ucl_object_t is
      released when the parser is torn down at the end of the load, while these
      strings are read every minute for the life of the session. */
      char **target = !strcmp(key, "clock-format") ? &lock_config->clock_format
                                                   : &lock_config->date_format;

      hikari_free(*target);
      *target = hikari_malloc(strlen(format) + 1);
      strcpy(*target, format);
    } else if (!strcmp(key, "clock-font") || !strcmp(key, "date-font")) {
      const char *font;

      if (!ucl_object_tostring_safe(cur, &font)) {
        fprintf(stderr,
            "configuration error: expected string for \"%s\"\n",
            key);
        goto done;
      }

      struct hikari_font *target = !strcmp(key, "clock-font")
          ? &lock_config->clock_font
          : &lock_config->date_font;

      hikari_font_fini(target);
      hikari_font_init(target, font);
    } else if (!strcmp(key, "clock-color")) {
      if (!parse_color(configuration, cur, key, lock_config->clock_color)) {
        goto done;
      }
    } else if (!strcmp(key, "blank-timeout-ac") ||
        !strcmp(key, "blank-timeout-battery")) {
      int64_t seconds;

      /* [COMMENT] Action purpose: 0 means never blank, which is a legitimate
      choice now that there is something worth looking at on the lock screen.
      The upper bound is a day -- anything longer is indistinguishable from
      never and is more likely a typo. */
      if (!ucl_object_toint_safe(cur, &seconds) || seconds < 0 ||
          seconds > 86400) {
        fprintf(stderr,
            "configuration error: expected integer between 0 and 86400 for "
            "\"%s\"\n",
            key);
        goto done;
      }

      if (!strcmp(key, "blank-timeout-ac")) {
        lock_config->blank_timeout_ac = (int)seconds;
      } else {
        lock_config->blank_timeout_battery = (int)seconds;
      }
    } else {
      fprintf(stderr, "configuration error: unknown \"lock\" key \"%s\"\n", key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

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
parse_animation(
    struct hikari_configuration *configuration, const ucl_object_t *animation_obj)
{
  bool success = false;
  struct hikari_animation_config *config = &configuration->animation;

  const ucl_object_t *cur;
  ucl_object_iter_t it = ucl_object_iterate_new(animation_obj);

  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "ui.animation");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "enabled")) {
      bool enabled;

      if (!ucl_object_toboolean_safe(cur, &enabled)) {
        fprintf(stderr,
            "configuration error: expected boolean for \"enabled\"\n");
        goto done;
      }

      config->enabled = enabled;
    } else if (!strcmp(key, "duration")) {
      int64_t duration;

      if (!ucl_object_toint_safe(cur, &duration) || duration < 0 ||
          duration > 1000) {
        fprintf(stderr,
            "configuration error: expected integer between 0 and 1000 for "
            "\"duration\"\n");
        goto done;
      }

      config->duration_msec = (int)duration;
    } else if (!strcmp(key, "easing")) {
      const char *easing;

      if (!ucl_object_tostring_safe(cur, &easing)) {
        fprintf(stderr,
            "configuration error: expected string for \"easing\"\n");
        goto done;
      }

      if (!strcmp(easing, "linear")) {
        config->easing = HIKARI_EASING_LINEAR;
      } else if (!strcmp(easing, "ease-out")) {
        config->easing = HIKARI_EASING_EASE_OUT;
      } else if (!strcmp(easing, "ease-in-out")) {
        config->easing = HIKARI_EASING_EASE_IN_OUT;
      } else {
        fprintf(stderr,
            "configuration error: expected \"linear\", \"ease-out\" or "
            "\"ease-in-out\" for \"easing\", got \"%s\"\n",
            easing);
        goto done;
      }
    } else {
      fprintf(stderr,
          "configuration error: unknown \"animation\" key \"%s\"\n",
          key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
parse_ui(struct hikari_configuration *configuration, const ucl_object_t *ui_obj)
{
  bool success = false;

  /* [COMMENT] Action purpose: The palette is resolved BEFORE the iteration
  below, not as one of its cases. Every other colour in this block may be
  written as a palette reference, and UCL yields keys in file order -- so
  parsing the palette in sequence would make a configuration's meaning depend on
  whether the user happened to put `palette` above `colorscheme`. Looking it up
  explicitly first is the same idiom hikari_configuration_load() uses for
  `actions` and `layouts`, and for the same reason. */
  const ucl_object_t *palette_obj = ucl_object_lookup(ui_obj, "palette");
  if (palette_obj != NULL && !parse_palette(configuration, palette_obj)) {
    return false;
  }

  ucl_object_iter_t it = ucl_object_iterate_new(ui_obj);

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "ui");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "palette")) {
      // [COMMENT] Action purpose: Already handled above; accepted here so the
      // unknown-key branch does not reject it.
      continue;
    } else if (!strcmp(key, "colorscheme")) {
      if (!parse_colorscheme(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "animation")) {
      if (!parse_animation(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "font")) {
      hikari_font_fini(&configuration->font);
      if (!parse_font(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "border")) {
      if (!parse_border(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "gap")) {
      if (!parse_gap(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "spill")) {
      const char *spill;

      if (!ucl_object_tostring_safe(cur, &spill)) {
        fprintf(stderr, "configuration error: expected string for \"spill\"\n");
        goto done;
      }

      if (!strcmp(spill, "always")) {
        configuration->spill = HIKARI_SPILL_ALWAYS;
      } else if (!strcmp(spill, "drag")) {
        configuration->spill = HIKARI_SPILL_DRAG;
      } else if (!strcmp(spill, "never")) {
        configuration->spill = HIKARI_SPILL_NEVER;
      } else {
        fprintf(stderr,
            "configuration error: expected \"always\", \"drag\" or \"never\" "
            "for \"spill\", got \"%s\"\n",
            spill);
        goto done;
      }
    } else if (!strcmp(key, "step")) {
      if (!parse_step(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "lock")) {
      if (!parse_lock(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "bar")) {
      if (!parse_bar(&configuration->bar_config, cur)) {
        goto done;
      }
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}

static bool
set_env_vars(struct ucl_parser *parser)
{
  for (char **current_var = environ; *current_var != NULL; ++current_var) {
    const char *separator = strchr(*current_var, '=');
    if (separator == NULL) {
      continue;
    }

    const size_t name_length = separator - *current_var;
    char *name = hikari_malloc(name_length + 1);
    if (name == NULL) {
      fprintf(stderr, "Could not allocate enough memory :(\n");
      return false;
    }
    strncpy(name, *current_var, name_length);
    name[name_length] = '\0';
    ucl_parser_register_variable(parser, name, separator + 1);
    hikari_free(name);
  }

  return true;
}

bool
hikari_configuration_load(
    struct hikari_configuration *configuration, char *config_path)
{
  struct ucl_parser *parser = ucl_parser_new(0);
  if (!set_env_vars(parser)) {
    ucl_parser_free(parser);
    return false;
  }
  bool success = false;
  const ucl_object_t *cur;

  ucl_parser_add_file(parser, config_path);
  ucl_object_t *configuration_obj = ucl_parser_get_object(parser);

  if (configuration_obj == NULL) {
    const char *error = ucl_parser_get_error(parser);
    fprintf(stderr, "%s\n", error);
    ucl_parser_free(parser);
    return false;
  }

  ucl_object_iter_t it = ucl_object_iterate_new(configuration_obj);

  const ucl_object_t *actions_obj =
      ucl_object_lookup(configuration_obj, "actions");
  if (actions_obj != NULL && !parse_actions(configuration, actions_obj)) {
    fprintf(stderr, "configuration error: failed to parse \"actions\"\n");
    goto done;
  }

  const ucl_object_t *layouts_obj =
      ucl_object_lookup(configuration_obj, "layouts");
  if (layouts_obj != NULL && !parse_layouts(configuration, layouts_obj)) {
    fprintf(stderr, "configuration error: failed to parse \"layouts\"\n");
    goto done;
  }

  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    hikari_config_warn_duplicate_key(cur, "the configuration");

    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "ui")) {
      if (!parse_ui(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "views")) {
      if (!parse_view_configs(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "marks")) {
      if (!parse_execute(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "bindings")) {
      if (!parse_bindings(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "outputs")) {
      if (!parse_outputs(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "inputs")) {
      if (!parse_inputs(configuration, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "layout")) {
      if (!parse_layout_policy(configuration, cur)) {
        goto done;
      }
    } else if (!!strcmp(key, "actions") && !!strcmp(key, "layouts")) {
      fprintf(stderr,
          "configuration error: unkown configuration section \"%s\"\n",
          key);
      goto done;
    }
  }

  if (!finalize_keyboard_configs(configuration)) {
    goto done;
  }

  success = true;

done:
  ucl_object_iterate_free(it);
  ucl_object_unref(configuration_obj);
  ucl_parser_free(parser);

  return success;
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
      struct hikari_view *view;
      wl_list_for_each (view, &output->views, output_views) {
        hikari_view_refresh_geometry(view, view->current_geometry);
      }

      struct hikari_output_config *output_config =
          hikari_configuration_resolve_output_config(
              hikari_configuration, output->wlr_output->name);

      if (output_config != NULL) {
        if (output_config->position.value.type ==
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
