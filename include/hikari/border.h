#if !defined(HIKARI_BORDER_H)
#define HIKARI_BORDER_H

#include <wlr/util/box.h>
#include <wlr/types/wlr_scene.h>

#include <hikari/output.h>



enum hikari_border_state {
  HIKARI_BORDER_NONE,
  HIKARI_BORDER_INACTIVE,
  HIKARI_BORDER_ACTIVE
};

struct hikari_border {
  enum hikari_border_state state;

  struct wlr_box geometry;
  struct wlr_scene_rect *top;
  struct wlr_scene_rect *bottom;
  struct wlr_scene_rect *left;
  struct wlr_scene_rect *right;
};

void
hikari_border_init(struct hikari_border *border, struct wlr_scene_tree *parent);

static inline struct wlr_box *
hikari_border_geometry(struct hikari_border *border)
{
  return &border->geometry;
}

void
hikari_border_refresh_geometry(
    struct hikari_border *border, struct wlr_box *geometry);

#endif
