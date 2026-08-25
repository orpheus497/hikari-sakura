#if !defined(HIKARI_CONFIGURATION_H)
#define HIKARI_CONFIGURATION_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>

#include <hikari/animation.h>
#include <hikari/exec.h>
#include <hikari/bar_config.h>
#include <hikari/font.h>
#include <hikari/layout_policy.h>
#include <hikari/lock_config.h>
#include <hikari/mark.h>

struct hikari_group;
struct hikari_binding;
struct hikari_sheet;
struct hikari_view;
struct hikari_pointer_config;

/* [COMMENT] Class purpose: How many positional colours the palette holds.
Sixteen because that is the size every terminal palette, every pywal scheme and
the hikari-topbar helper already use -- picking a different number would mean
the desktop and the bar could not share one theme. */
static const int HIKARI_NR_OF_PALETTE_COLORS = 16;

struct hikari_configuration {
  /* [COMMENT] Class purpose: The positional colour palette -- `color0` through
  `color15` of the `ui { palette { ... } }` block.

  It holds no meaning of its own. Every colour the compositor actually draws
  with is one of the SEMANTIC slots below, and the palette exists so those slots
  can be expressed as references into one place instead of sixteen literals
  scattered through the file. A configuration that never mentions the palette is
  unaffected: the semantic slots still accept literal hex exactly as before. */
  float palette[16][4];

  float clear[4];
  float foreground[4];
  float indicator_selected[4];
  float indicator_grouped[4];
  float indicator_first[4];
  float indicator_conflict[4];
  float indicator_insert[4];
  float border_active[4];
  float border_inactive[4];

  /* [COMMENT] Class purpose: Top bar background, independent of `clear`. Kept
  separate so the bar can be tinted or made translucent without altering the
  desktop background, which is what sharing `clear` forced. Alpha is honoured
  -- see parse_color() in src/configuration.c. */
  float bar[4];

  /* [COMMENT] Class purpose: Top bar length limits and banner scrolling -- the
  `ui { bar { ... } }` block. Kept compositor-side because the hikari-topbar
  helper is spawned once with no argv and never restarted, so nothing configured
  in it could survive a reload. */
  struct hikari_bar_config bar_config;

  struct hikari_font font;

  /* [COMMENT] Class purpose: Lock screen appearance and blanking
  behaviour -- the `ui { lock { ... } }` block. */
  struct hikari_lock_config lock;

  /* [COMMENT] Class purpose: Window motion -- the `ui { animation { ... } }`
  block. Position only; see include/hikari/animation.h for why size is not the
  compositor's to interpolate. */
  struct hikari_animation_config animation;

  /* [COMMENT] Class purpose: When a sheet re-tiles itself without being asked
  -- the top-level `layout { ... }` block. Top-level and not under `ui` because
  it governs behaviour rather than appearance, and singular so it cannot be
  confused with `layouts`, which owns the layout registers themselves. */
  struct hikari_layout_policy layout_policy;

  int border;
  int gap;
  int step;

  struct hikari_exec execs[HIKARI_NR_OF_EXECS];

  struct wl_list view_configs;
  struct wl_list output_configs;
  struct wl_list pointer_configs;
  struct wl_list keyboard_configs;
  struct wl_list layout_configs;
  struct wl_list action_configs;
  struct wl_list keyboard_binding_configs;
  struct wl_list mouse_binding_configs;
  struct wl_list switch_configs;
  struct wl_list gesture_binding_configs;
};

extern struct hikari_configuration *hikari_configuration;

void
hikari_configuration_init(struct hikari_configuration *configuration);

void
hikari_configuration_fini(struct hikari_configuration *configuration);

bool
hikari_configuration_load(
    struct hikari_configuration *configuration, char *config_path);

bool
hikari_configuration_reload(char *config_path);

struct hikari_view_config *
hikari_configuration_resolve_view_config(
    struct hikari_configuration *configuration, const char *app_id);

struct hikari_output_config *
hikari_configuration_resolve_output_config(
    struct hikari_configuration *configuration, const char *output_name);

struct hikari_pointer_config *
hikari_configuration_resolve_pointer_config(
    struct hikari_configuration *configuration, const char *pointer_name);

struct hikari_keyboard_config *
hikari_configuration_resolve_keyboard_config(
    struct hikari_configuration *configuration, const char *keyboard_name);

struct hikari_switch_config *
hikari_configuration_resolve_switch_config(
    struct hikari_configuration *configuration, const char *switch_name);

struct hikari_split *
hikari_configuration_lookup_layout(
    struct hikari_configuration *configuration, char layout_register);

#endif
