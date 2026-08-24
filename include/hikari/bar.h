// Script function and purpose: Native top bar for hikari. Renders a
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

/* [COMMENT] Class purpose: Which of the bar's three independent runs a block
belongs to. This was previously a single `bool align_right`, which made a
centred run impossible to express -- the helper faked one with a fixed-width
spacer in the left run, so it drifted with the output width and with whichever
blocks happened to precede it. See DECISIONS_LOG Phase 60. */
enum hikari_bar_align {
  HIKARI_BAR_ALIGN_LEFT,
  HIKARI_BAR_ALIGN_CENTER,
  HIKARI_BAR_ALIGN_RIGHT
};

/* [COMMENT] Class purpose: One rendered status block from the swaybar
protocol. `full_text` is the display string; `color` is an optional
#rrggbb/#rrggbbaa override; `min_width` pads the block out to a fixed pixel
width; `align` selects which of the three runs it is laid out in. */
struct hikari_bar_block {
  char *full_text;
  float color[4];
  bool has_color;
  int min_width;
  enum hikari_bar_align align;

  /* [COMMENT] Class purpose: How far this block's banner has scrolled, in
  CODEPOINTS from the start of `full_text`. Only meaningful when the block is
  longer than the configured cap; otherwise pinned at 0.

  Carried across parses rather than reset with the block set: the helper re-emits
  every block several times a second, so clearing this on each line would restart
  the scroll before a single step was visible. parse_line() copies it forward
  when the block at the same index still holds the same text, and drops it the
  moment the text changes -- a new track starts reading from its beginning. */
  int scroll_offset;
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

  /* [COMMENT] Class purpose: Set while a genuinely fullscreen view is visible
  on this output, which is the one case where the bar gets out of a window's
  way.

  DELIBERATELY SEPARATE FROM `enabled`, and the two must never be merged.
  `enabled` is what hikari_bar_reserve() tests, so it decides whether the bar's
  rows are subtracted from output->usable_area -- and usable_area is what the
  tiling engine lays every window out against. Clearing `enabled` to hide the
  bar would therefore hand those rows back to the layout and reflow every tiled
  window on the output the instant a video went fullscreen, then reflow them
  again on the way out. This field suppresses the scene node and nothing else:
  the reservation stands, the layout never moves, and only the pixels go away. */
  bool obscured;

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

  /* [COMMENT] Class purpose: Drives the banner scroll, and is ARMED ONLY WHILE
  SOME BLOCK IS OVER THE CAP. That condition is the point: every step repaints
  the whole bar, so a permanently-running timer would have the compositor
  re-rendering several times a second forever. With nothing playing there is no
  timer and no wakeups at all.

  Deliberately not driven off the helper's own 200ms tick, which would couple
  scrolling to telemetry arriving and would freeze mid-title if the helper
  wedged. NULL when disarmed. */
  struct wl_event_source *scroll_timer;
  bool scroll_armed;
};

// Function purpose: Spawn the hikari-topbar helper and register its
// pipe with the compositor event loop. Safe to call when the helper is absent;
// the bar simply stays empty.
void
hikari_topbar_source_init(struct hikari_topbar_source *source);

// Function purpose: Tear down the helper process, event source, and
// buffers. Reaps the child non-blockingly.
void
hikari_topbar_source_fini(struct hikari_topbar_source *source);

// Function purpose: Attach a bar to an output and create its scene
// buffer node. Called once per output during output initialisation.
void
hikari_bar_init(struct hikari_bar *bar, struct hikari_output *output);

// Function purpose: Destroy the bar's scene node and clear state.
void
hikari_bar_fini(struct hikari_bar *bar);

// Function purpose: Re-render this output's bar from the current
// block set and reposition it at the top of the output.
void
hikari_bar_refresh(struct hikari_bar *bar);

// Function purpose: Subtract the bar's height from an output-local
// usable area. Called from output geometry setup and from the layer-shell
// arrangement pass so both agree on the space the bar occupies.
void
hikari_bar_reserve(struct hikari_bar *bar, struct wlr_box *usable_area);

/* [COMMENT] Function purpose: Recompute whether this output's bar should be on
screen, from whether a fullscreen view is currently visible on it, and apply the
result to the scene node.

Takes the output rather than the bar because the answer is a property of the
output's visible view set, not of the bar. Deliberately derived by walking that
set on each call instead of being maintained as a counter: a counter has to be
decremented on every path a view can stop being visible or stop being fullscreen
-- which is the same nine-way audit that makes the flag itself delicate -- and a
single missed decrement leaves the bar hidden for the rest of the session with
no way for the user to get it back. Walking is O(visible views on one output),
which is a handful, and it cannot drift. */
void
hikari_bar_update_visibility(struct hikari_output *output);

#endif
