#include <hikari/keyboard_config.h>
#include <hikari/config_lua.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIKARI_KEYBOARD_CONFIG_DEFAULT_REPEAT_RATE 25
#define HIKARI_KEYBOARD_CONFIG_DEFAULT_REPEAT_DELAY 600

static void
init_default_repeat(struct hikari_keyboard_config *keyboard_config)
{
  hikari_keyboard_config_set_repeat_rate(
      keyboard_config, HIKARI_KEYBOARD_CONFIG_DEFAULT_REPEAT_RATE);
  hikari_keyboard_config_set_repeat_delay(
      keyboard_config, HIKARI_KEYBOARD_CONFIG_DEFAULT_REPEAT_DELAY);
}

static bool
parse_xkb_rules(struct hikari_xkb_config *xkb_config, lua_State *L, int idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, idx);
  while (hikari_config_iter_next(
      &iter, L, &key, "inputs.keyboards xkb block")) {
    if (!strcmp("rules", key)) {
      const char *rules;
      if (!hikari_config_string(
              L, iter.vidx, "inputs.keyboards xkb block", key, &rules)) {
        goto done;
      }

      hikari_xkb_config_set_rules(xkb_config, strdup(rules));
    } else if (!strcmp("model", key)) {
      const char *model;
      if (!hikari_config_string(
              L, iter.vidx, "inputs.keyboards xkb block", key, &model)) {
        goto done;
      }

      hikari_xkb_config_set_model(xkb_config, strdup(model));
    } else if (!strcmp("layout", key)) {
      const char *layout;
      if (!hikari_config_string(
              L, iter.vidx, "inputs.keyboards xkb block", key, &layout)) {
        goto done;
      }

      hikari_xkb_config_set_layout(xkb_config, strdup(layout));
    } else if (!strcmp("variant", key)) {
      const char *variant;
      if (!hikari_config_string(
              L, iter.vidx, "inputs.keyboards xkb block", key, &variant)) {
        goto done;
      }

      hikari_xkb_config_set_variant(xkb_config, strdup(variant));
    } else if (!strcmp("options", key)) {
      const char *options;
      if (!hikari_config_string(
              L, iter.vidx, "inputs.keyboards xkb block", key, &options)) {
        goto done;
      }

      hikari_xkb_config_set_options(xkb_config, strdup(options));
    } else {
      hikari_config_error(
          "inputs.keyboards xkb block", "invalid \"xkb\" key \"%s\"", key);
      goto done;
    }
  }

  /* Action purpose: the loop also ends when a malformed entry abandoned the
  walk, and only iter.failed tells that apart from reaching the end of the
  block. Without this test a reported error would still be followed by
  success = true. */
  if (iter.failed) {
    goto done;
  }

  success = true;

done:
  hikari_config_iter_fini(&iter, L);

  return success;
}

/* [COMMENT] Function purpose: Load a keymap from a user-supplied xkb file
(the keyboard "xkb file" config option) and store it tagged as
HIKARI_XKB_TYPE_KEYMAP so the union is interpreted correctly downstream. */
static bool
load_xkb_file(struct hikari_xkb *xkb, const char *xkb_file)
{
  bool success = false;

  FILE *keymap_file = fopen(xkb_file, "r");

  if (!keymap_file) {
    hikari_config_error("inputs.keyboards entry",
        "failed to open \"xkb\" file \"%s\"",
        xkb_file);
    goto done;
  }

  struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  struct xkb_keymap *keymap = xkb_keymap_new_from_file(context,
      keymap_file,
      XKB_KEYMAP_FORMAT_TEXT_V1,
      XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref(context);

  /* [COMMENT] Action purpose: Close the stream as soon as xkbcommon is done
  reading it, which is the moment the call above returns. xkb_keymap_new_from_
  file() takes a FILE * and reads the keymap from it; xkbcommon.h documents no
  transfer of ownership, so closing is the caller's job and nothing downstream
  holds the handle. Placed here rather than at `done:` because it covers both
  remaining paths -- a NULL keymap and a successful one -- with one call.
  remaining paths -- a NULL keymap and a successful one -- with one call. */
  fclose(keymap_file);

  if (keymap == NULL) {
    goto done;
  }

  /* [COMMENT] Action purpose: Tag the union member that was actually stored.
  A file-loaded keymap lives in value.keymap, therefore the type tag must be
  HIKARI_XKB_TYPE_KEYMAP. Tagging it RULES makes compile_keymap()/xkb_fini()
  reinterpret the keymap pointer as a rules struct (garbage fields, invalid
  free() calls) -- a crash/corruption path. */
  xkb->type = HIKARI_XKB_TYPE_KEYMAP;
  xkb->value.keymap = keymap;

  success = true;

done:

  return success;
}

bool
hikari_keyboard_config_parse(struct hikari_keyboard_config *keyboard_config,
    lua_State *L,
    int idx)
{
  bool success = false;
  struct hikari_config_iter iter;
  const char *key;

  hikari_config_iter_init(&iter, L, idx);
  while (hikari_config_iter_next(&iter, L, &key, "inputs.keyboards entry")) {
    if (!strcmp("xkb", key)) {
      int type = lua_type(L, iter.vidx);

      if (type == LUA_TTABLE) {
        keyboard_config->xkb.type = HIKARI_XKB_TYPE_RULES;

        if (!parse_xkb_rules(&keyboard_config->xkb.value.rules, L, iter.vidx)) {
          hikari_config_error(
              "inputs.keyboards entry", "failed to parse xkb rules");
          goto done;
        }
      } else if (type == LUA_TSTRING) {
        const char *xkb_file;
        if (!hikari_config_string(
                L, iter.vidx, "inputs.keyboards entry", key, &xkb_file) ||
            !load_xkb_file(&keyboard_config->xkb, xkb_file)) {
          goto done;
        }
      } else {
        hikari_config_error("inputs.keyboards entry",
            "expected string or table for \"xkb\"");
        goto done;
      }
    } else if (!strcmp("repeat-rate", key)) {
      int64_t repeat_rate;

      if (!hikari_config_int(
              L, iter.vidx, "inputs.keyboards entry", key, &repeat_rate)) {
        goto done;
      }

      if (repeat_rate < 0) {
        hikari_config_error("inputs.keyboards entry",
            "expected positive integer \"repeat-rate\"");
        goto done;
      }

      hikari_keyboard_config_set_repeat_rate(keyboard_config, repeat_rate);
    } else if (!strcmp("repeat-delay", key)) {
      int64_t repeat_delay;

      if (!hikari_config_int(
              L, iter.vidx, "inputs.keyboards entry", key, &repeat_delay)) {
        goto done;
      }

      if (repeat_delay < 0) {
        hikari_config_error("inputs.keyboards entry",
            "expected positive integer \"repeat-delay\"");
        goto done;
      }

      hikari_keyboard_config_set_repeat_delay(keyboard_config, repeat_delay);
    } else {
      hikari_config_error(
          "inputs.keyboards entry", "invalid \"keyboard\" key \"%s\"", key);
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

static void
xkb_init(struct hikari_xkb_config *xkb_config)
{
  hikari_xkb_config_init_rules(xkb_config, NULL);
  hikari_xkb_config_init_model(xkb_config, NULL);
  hikari_xkb_config_init_layout(xkb_config, NULL);
  hikari_xkb_config_init_variant(xkb_config, NULL);
  hikari_xkb_config_init_options(xkb_config, NULL);
}

void
hikari_keyboard_config_init(
    struct hikari_keyboard_config *keyboard_config, const char *keyboard_name)
{
  keyboard_config->xkb.type = HIKARI_XKB_TYPE_RULES;

  xkb_init(&keyboard_config->xkb.value.rules);

  keyboard_config->keyboard_name = strdup(keyboard_name);

  if (!strcmp(keyboard_name, "*")) {
    init_default_repeat(keyboard_config);
  } else {
    hikari_keyboard_config_init_repeat_rate(
        keyboard_config, HIKARI_KEYBOARD_CONFIG_DEFAULT_REPEAT_RATE);
    hikari_keyboard_config_init_repeat_delay(
        keyboard_config, HIKARI_KEYBOARD_CONFIG_DEFAULT_REPEAT_DELAY);
  }
}

static void
xkb_fini(struct hikari_xkb_config *xkb_config)
{
  free(xkb_config->rules.value);
  free(xkb_config->model.value);
  free(xkb_config->layout.value);
  free(xkb_config->variant.value);
  free(xkb_config->options.value);
}

void
hikari_keyboard_config_fini(struct hikari_keyboard_config *keyboard_config)
{
  switch (keyboard_config->xkb.type) {
    case HIKARI_XKB_TYPE_KEYMAP:
      xkb_keymap_unref(keyboard_config->xkb.value.keymap);
      break;

    case HIKARI_XKB_TYPE_RULES:
      xkb_fini(&keyboard_config->xkb.value.rules);
      break;
  }

  free(keyboard_config->keyboard_name);
}

static void
default_init(struct hikari_xkb_config *xkb_config)
{
  char *rules = getenv("XKB_DEFAULT_RULES");
  char *model = getenv("XKB_DEFAULT_MODEL");
  char *layout = getenv("XKB_DEFAULT_LAYOUT");
  char *variant = getenv("XKB_DEFAULT_VARIANT");
  char *options = getenv("XKB_DEFAULT_OPTIONS");

  if (rules == NULL) {
    hikari_xkb_config_set_rules(xkb_config, strdup("evdev"));
  } else {
    hikari_xkb_config_set_rules(xkb_config, strdup(rules));
  }

  if (model != NULL) {
    hikari_xkb_config_set_model(xkb_config, strdup(model));
  }

  if (layout != NULL) {
    hikari_xkb_config_set_layout(xkb_config, strdup(layout));
  }

  if (variant != NULL) {
    hikari_xkb_config_set_variant(xkb_config, strdup(variant));
  }

  if (options != NULL) {
    hikari_xkb_config_set_options(xkb_config, strdup(options));
  }
}

void
hikari_keyboard_config_default(struct hikari_keyboard_config *keyboard_config)
{
  keyboard_config->xkb.type = HIKARI_XKB_TYPE_RULES;

  xkb_init(&keyboard_config->xkb.value.rules);
  default_init(&keyboard_config->xkb.value.rules);

  keyboard_config->keyboard_name = malloc(2 * sizeof(char));
  strcpy(keyboard_config->keyboard_name, "*");

  init_default_repeat(keyboard_config);
}

static bool
has_set_options(struct hikari_xkb_config *xkb_config)
{
  return hikari_xkb_config_has_rules(xkb_config) ||
         hikari_xkb_config_has_model(xkb_config) ||
         hikari_xkb_config_has_layout(xkb_config) ||
         hikari_xkb_config_has_variant(xkb_config) ||
         hikari_xkb_config_has_options(xkb_config);
}

#define MERGE(option)                                                          \
  if (hikari_xkb_config_merge_##option(config_xkb, default_xkb) &&             \
      default_xkb->option.value != NULL) {                                     \
    config_xkb->option.value = strdup(default_xkb->option.value);              \
  }

void
hikari_keyboard_config_merge(struct hikari_keyboard_config *keyboard_config,
    struct hikari_keyboard_config *default_config)
{
  hikari_keyboard_config_merge_repeat_rate(keyboard_config, default_config);
  hikari_keyboard_config_merge_repeat_delay(keyboard_config, default_config);

  if (default_config->xkb.type == HIKARI_XKB_TYPE_KEYMAP) {
    if (keyboard_config->xkb.type == HIKARI_XKB_TYPE_RULES) {
      if (!has_set_options(&keyboard_config->xkb.value.rules)) {
        keyboard_config->xkb.type = HIKARI_XKB_TYPE_KEYMAP;
        keyboard_config->xkb.value.keymap =
            xkb_keymap_ref(default_config->xkb.value.keymap);
      }
    }
  } else {
    assert(default_config->xkb.type == HIKARI_XKB_TYPE_RULES);

    if (keyboard_config->xkb.type == HIKARI_XKB_TYPE_RULES) {
      struct hikari_xkb_config *config_xkb = &keyboard_config->xkb.value.rules;
      struct hikari_xkb_config *default_xkb = &default_config->xkb.value.rules;

      MERGE(rules);
      MERGE(model);
      MERGE(layout);
      MERGE(variant);
      MERGE(options);
    }
  }
}
#undef MERGE

/* [COMMENT] Function purpose: Compile a keymap from the configured xkb
rules/model/layout/variant/options via xkbcommon; used when no keymap file
was configured. */
static struct xkb_keymap *
compile_keymap(struct hikari_xkb_config *xkb_config)
{
  struct xkb_rule_names rules = { 0 };

  rules.rules = xkb_config->rules.value;
  rules.model = xkb_config->model.value;
  rules.layout = xkb_config->layout.value;
  rules.variant = xkb_config->variant.value;
  rules.options = xkb_config->options.value;

  struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  /* [COMMENT] Action purpose: Compile rules into a keymap via the real
  xkbcommon entry point xkb_keymap_new_from_names(). The previously used
  "xkb_map_new_from_names" does not exist in libxkbcommon >= 1.0 -- it fails
  to link on any clean build. */
  struct xkb_keymap *keymap =
      xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref(context);

  return keymap;
}

bool
hikari_keyboard_config_compile_keymap(
    struct hikari_keyboard_config *keyboard_config)
{
  if (keyboard_config->xkb.type == HIKARI_XKB_TYPE_KEYMAP) {
    return true;
  }

  assert(keyboard_config->xkb.type == HIKARI_XKB_TYPE_RULES);

  struct xkb_keymap *keymap = compile_keymap(&keyboard_config->xkb.value.rules);

  if (keymap == NULL) {
    return false;
  }

  xkb_fini(&keyboard_config->xkb.value.rules);

  keyboard_config->xkb.type = HIKARI_XKB_TYPE_KEYMAP;
  keyboard_config->xkb.value.keymap = keymap;

  return true;
}
