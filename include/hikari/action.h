#if !defined(HIKARI_ACTION_H)
#define HIKARI_ACTION_H

#include <stdbool.h>
#include <wayland-util.h>

/* Declared rather than included: this header reaches most of the tree through
binding_config.h, gesture_config.h and switch_config.h, and <lua.h> belongs
only to the configuration reader. lua_State is a typedef of this struct, so the
two spellings are the same type. */
struct lua_State;

struct hikari_event_action {
  void (*action)(void *);
  void *arg;
};

struct hikari_action {
  struct hikari_event_action begin;
  struct hikari_event_action end;
};

void
hikari_action_init(struct hikari_action *action);

bool
hikari_action_parse(struct hikari_action *action,
    struct wl_list *action_configs,
    struct lua_State *L,
    int idx);

#endif
