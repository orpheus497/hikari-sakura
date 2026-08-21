// [COMMENT] Script function and purpose: Native top bar for hikari. Renders a
// swaybar-style status bar directly into the compositor's scene graph, fed by
// the separate hikari-topbar helper process over a non-blocking pipe.

#if !defined(HIKARI_BAR_H)
#define HIKARI_BAR_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

struct hikari_output;

/* [COMMENT] Class purpose: One rendered status block from the swaybar
protocol. `full_text` is the display string; `color` is an optional #rrggbb
override; `min_width` pads the block out to a fixed pixel width (the helper
uses this to push right-aligned items toward the edge). */
struct hikari_bar_block {
  char *full_text;
  float color[4];
  bool has_color;
  int min_width;
  bool align_right;
};

#define HIKARI_BAR_MAX_BLOCKS 32

/* [COMMENT] Class purpose: Per-output bar state. The bar owns exactly one
wlr_scene_buffer, replaced whenever new telemetry arrives. `height` is derived
from the configured font and is the amount subtracted from the output's usable
area so tiled views never sit underneath the bar. */
struct hikari_bar {
  struct hikari_output *output;
  struct wlr_scene_buffer *scene_buffer;
  int height;
  bool enabled;

  /* [COMMENT] Class purpose: Identity of the last repaint -- a serialised
  snapshot of the rendered block set plus the geometry it was rendered at.
  hikari_bar_refresh() skips the cairo/Pango work entirely when a new refresh
  request would produce an identical result (the common case: the helper
  ticks every 200ms but most blocks only change once a second). */
  char *cache_key;
  int cache_width;
  int cache_height;
  float cache_scale;
};

/* [COMMENT] Class purpose: Global state for the hikari-topbar helper process
and its pipe. A single helper feeds every output's bar -- the telemetry is
machine-wide, so sampling it once and fanning it out avoids N redundant
processes on multi-monitor setups. */
struct hikari_topbar_source {
  pid_t pid;
  int fd;
  struct wl_event_source *event_source;

  /* [COMMENT] Class purpose: Line-accumulation buffer. The helper emits one
  JSON array per line, but a pipe read can split a line arbitrarily, so
  partial data is retained here until a newline completes it. */
  char *buf;
  size_t len;
  size_t cap;

  struct hikari_bar_block blocks[HIKARI_BAR_MAX_BLOCKS];
  int nr_blocks;
};

// [COMMENT] Function purpose: Spawn the hikari-topbar helper and register its
// pipe with the compositor event loop. Safe to call when the helper is absent;
// the bar simply stays empty.
void
hikari_topbar_source_init(struct hikari_topbar_source *source);

// [COMMENT] Function purpose: Tear down the helper process, event source, and
// buffers. Reaps the child non-blockingly.
void
hikari_topbar_source_fini(struct hikari_topbar_source *source);

// [COMMENT] Function purpose: Attach a bar to an output and create its scene
// buffer node. Called once per output during output initialisation.
void
hikari_bar_init(struct hikari_bar *bar, struct hikari_output *output);

// [COMMENT] Function purpose: Destroy the bar's scene node and clear state.
void
hikari_bar_fini(struct hikari_bar *bar);

// [COMMENT] Function purpose: Re-render this output's bar from the current
// block set and reposition it at the top of the output.
void
hikari_bar_refresh(struct hikari_bar *bar);

// [COMMENT] Function purpose: Subtract the bar's height from an output-local
// usable area. Called from output geometry setup and from the layer-shell
// arrangement pass so both agree on the space the bar occupies.
void
hikari_bar_reserve(struct hikari_bar *bar, struct wlr_box *usable_area);

#endif
