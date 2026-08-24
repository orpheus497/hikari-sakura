#if !defined(HIKARI_OPERATION_H)
#define HIKARI_OPERATION_H

#include <wlr/util/box.h>

struct hikari_tile;

enum hikari_operation_type {
  HIKARI_OPERATION_TYPE_RESIZE,
  HIKARI_OPERATION_TYPE_RESET,
  HIKARI_OPERATION_TYPE_UNMAXIMIZE,
  HIKARI_OPERATION_TYPE_FULL_MAXIMIZE,
  HIKARI_OPERATION_TYPE_VERTICAL_MAXIMIZE,
  HIKARI_OPERATION_TYPE_HORIZONTAL_MAXIMIZE,
  HIKARI_OPERATION_TYPE_TILE,

  /* [COMMENT] Action purpose: Genuine fullscreen -- the whole output, over the
  top bar -- as distinct from FULL_MAXIMIZE, which respects the usable area.
  Only ever reached from a client's own protocol request (xdg-shell
  set_fullscreen, XWayland _NET_WM_STATE_FULLSCREEN); hikari binds no key to it,
  because F11 already lives in the application. Two switches enumerate this enum
  exhaustively and both must handle it: commit_operation() in src/view.c, and
  the tiled-edge switch in src/xdg_view.c -- where fullscreen belongs with
  RESET/UNMAXIMIZE (WLR_EDGE_NONE), NOT with the maximize cases. */
  HIKARI_OPERATION_TYPE_FULLSCREEN
};

struct hikari_operation {
  enum hikari_operation_type type;
  bool dirty;
  bool center;
  uint32_t serial;
  struct wlr_box geometry;
  struct hikari_tile *tile;
};

#endif
