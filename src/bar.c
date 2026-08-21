// [COMMENT] Script function and purpose: Native top bar rendering for hikari.
// Consumes swaybar-protocol JSON from the hikari-topbar helper over a
// non-blocking pipe and paints it into the scene graph with cairo/Pango.

#include <hikari/bar.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include <hikari/color.h>
#include <hikari/configuration.h>
#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/server.h>

/* [COMMENT] Action purpose: Horizontal padding applied at each end of the bar
and between the left-aligned and right-aligned block runs. */
#define HIKARI_BAR_PADDING 6

/* [COMMENT] Action purpose: Upper bound on a single helper line. The swaybar
protocol emits one array per tick; anything beyond this is a malformed or
hostile stream and is discarded rather than grown without limit. */
#define HIKARI_BAR_MAX_LINE (64 * 1024)

/* [COMMENT] Action purpose: Upper bound on a single block's requested width.
Any wider request cannot be displayed and would overflow the int accumulator
used for right-aligned layout. */
#define HIKARI_BAR_MAX_BLOCK_WIDTH 8192

/* [COMMENT] Action purpose: The bar uses the shared
hikari_server_create_argb8888_buffer(), which is CPU-backed rather than
allocator-backed. That distinction matters here: allocating through
wlr_allocator fails on the target platform (GBM buffer mapping on FreeBSD/ZFS),
which silently produced a NULL buffer and an invisible bar. See
DECISIONS_LOG Phase 33 and the implementation in src/server.c. */

// [COMMENT] Function purpose: Release the strings owned by the current block
// set and reset the count.
static void
clear_blocks(struct hikari_topbar_source *source)
{
  for (int i = 0; i < source->nr_blocks; i++) {
    hikari_free(source->blocks[i].full_text);
    source->blocks[i].full_text = NULL;
  }
  source->nr_blocks = 0;
}

/* [COMMENT] Function purpose: Parse a "#rrggbb" colour into hikari's normalised
float RGBA. Returns false for anything that is not exactly six hex digits, so a
malformed palette entry falls back to the configured foreground instead of
rendering an undefined colour. */
static bool
parse_hex_color(const char *text, float color[static 4])
{
  if (text == NULL || text[0] != '#') {
    return false;
  }

  if (strlen(text) != 7) {
    return false;
  }

  for (int i = 1; i < 7; i++) {
    if (!isxdigit((unsigned char)text[i])) {
      return false;
    }
  }

  unsigned int r, g, b;
  if (sscanf(text + 1, "%2x%2x%2x", &r, &g, &b) != 3) {
    return false;
  }

  color[0] = (float)r / 255.0f;
  color[1] = (float)g / 255.0f;
  color[2] = (float)b / 255.0f;
  color[3] = 1.0f;

  return true;
}

/* [COMMENT] Function purpose: Extract a double-quoted JSON string value for
`key` from within a single object slice. Returns a freshly allocated,
unescaped copy, or NULL when the key is absent. Deliberately minimal: the
swaybar protocol values hikari consumes are flat strings, so only \" and \\
escapes are honoured. */
static char *
json_string_field(const char *obj, const char *end, const char *key)
{
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);

  const char *p = strstr(obj, pattern);
  if (p == NULL || p >= end) {
    return NULL;
  }

  p = strchr(p + strlen(pattern), ':');
  if (p == NULL || p >= end) {
    return NULL;
  }

  while (p < end && *p != '"') {
    p++;
  }
  if (p >= end) {
    return NULL;
  }
  p++;

  /* [COMMENT] Action purpose: Measure the unescaped length first so the
  destination is sized exactly, then copy. Two passes keeps the allocation
  tight and avoids realloc churn per block per tick. */
  size_t len = 0;
  for (const char *q = p; q < end && *q != '"'; q++) {
    if (*q == '\\' && q + 1 < end) {
      q++;
    }
    len++;
  }

  char *out = hikari_malloc(len + 1);
  size_t i = 0;
  for (const char *q = p; q < end && *q != '"' && i < len; q++) {
    if (*q == '\\' && q + 1 < end) {
      q++;
    }
    out[i++] = *q;
  }
  out[i] = '\0';

  return out;
}

// [COMMENT] Function purpose: Extract an integer JSON value for `key`, or
// return `fallback` when absent or unparseable.
static int
json_int_field(const char *obj, const char *end, const char *key, int fallback)
{
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);

  const char *p = strstr(obj, pattern);
  if (p == NULL || p >= end) {
    return fallback;
  }

  p = strchr(p + strlen(pattern), ':');
  if (p == NULL || p >= end) {
    return fallback;
  }

  // [COMMENT] Action purpose: strtol reports both malformed input and range
  // errors, unlike atoi. The result is clamped because it is consumed as a
  // pixel advance and an unbounded value would overflow the int accumulator
  // in hikari_bar_refresh.
  errno = 0;
  char *endptr = NULL;
  long value = strtol(p + 1, &endptr, 10);
  if (endptr == p + 1 || errno == ERANGE) {
    return fallback;
  }
  if (value < 0) {
    return 0;
  }
  if (value > HIKARI_BAR_MAX_BLOCK_WIDTH) {
    return HIKARI_BAR_MAX_BLOCK_WIDTH;
  }
  return (int)value;
}

/* [COMMENT] Function purpose: Find the closing '}' matching the '{' at `obj`,
skipping over braces that appear inside JSON string values (and honouring
backslash escapes within those strings) so a block's full_text can itself
contain '{' or '}' without truncating the object early. */
static const char *
find_object_end(const char *obj)
{
  const char *p = obj + 1;
  bool in_string = false;

  for (; *p != '\0'; p++) {
    if (in_string) {
      if (*p == '\\' && *(p + 1) != '\0') {
        p++;
      } else if (*p == '"') {
        in_string = false;
      }
      continue;
    }

    if (*p == '"') {
      in_string = true;
    } else if (*p == '}') {
      return p;
    }
  }

  return NULL;
}

/* [COMMENT] Function purpose: Parse one swaybar-protocol line -- a JSON array
of block objects -- into the source's block set. Blocks beyond
HIKARI_BAR_MAX_BLOCKS are dropped rather than allocated, bounding per-tick work
regardless of what the helper emits. */
static void
parse_line(struct hikari_topbar_source *source, const char *line)
{
  clear_blocks(source);

  const char *p = strchr(line, '[');
  if (p == NULL) {
    return;
  }

  while (source->nr_blocks < HIKARI_BAR_MAX_BLOCKS) {
    const char *obj = strchr(p, '{');
    if (obj == NULL) {
      break;
    }

    const char *end = find_object_end(obj);
    if (end == NULL) {
      break;
    }

    struct hikari_bar_block *block = &source->blocks[source->nr_blocks];
    memset(block, 0, sizeof(*block));

    block->full_text = json_string_field(obj, end, "full_text");
    block->min_width = json_int_field(obj, end, "min_width", 0);

    char *color = json_string_field(obj, end, "color");
    block->has_color = parse_hex_color(color, block->color);
    hikari_free(color);

    char *align = json_string_field(obj, end, "align");
    block->align_right = align != NULL && strcmp(align, "right") == 0;
    hikari_free(align);

    if (block->full_text != NULL) {
      source->nr_blocks++;
    }

    p = end + 1;
  }
}

/* [COMMENT] Function purpose: Serialise the current block set into a flat
string that uniquely identifies what would be painted -- full_text, colour,
min_width, and alignment for every block, in order. Used by hikari_bar_refresh
to detect a no-op repaint request. */
static char *
build_cache_key(struct hikari_topbar_source *source)
{
  size_t cap = 256;
  char *key = hikari_malloc(cap);
  size_t len = 0;
  key[0] = '\0';

  for (int i = 0; i < source->nr_blocks; i++) {
    struct hikari_bar_block *block = &source->blocks[i];
    const char *text = block->full_text != NULL ? block->full_text : "";

    int needed = snprintf(NULL, 0, "\x1f%s\x1f%d\x1f%d\x1f%.6f,%.6f,%.6f,%.6f",
        text, block->min_width, block->align_right,
        block->color[0], block->color[1], block->color[2], block->color[3]);
    if (needed < 0) {
      continue;
    }

    while (len + (size_t)needed + 1 > cap) {
      cap *= 2;
      char *grown = hikari_malloc(cap);
      memcpy(grown, key, len + 1);
      hikari_free(key);
      key = grown;
    }

    len += (size_t)snprintf(key + len, cap - len,
        "\x1f%s\x1f%d\x1f%d\x1f%.6f,%.6f,%.6f,%.6f",
        text, block->min_width, block->align_right,
        block->color[0], block->color[1], block->color[2], block->color[3]);
  }

  return key;
}

// [COMMENT] Function purpose: Repaint every output's bar after new telemetry.
static void
refresh_all_bars(void)
{
  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    hikari_bar_refresh(&output->bar);
  }
}

// [COMMENT] Function purpose: Terminate and reap a just-forked topbar helper
// child after a startup failure, keeping *pid set until reaping is confirmed
// so neither failure path loses the only reference to a still-running child.
// Runs during synchronous compositor startup, before the event loop is
// entered -- a short bounded retry loop here cannot stall any live session,
// unlike hikari-unlocker's reap path (src/lock_mode.c), which must stay
// strictly non-blocking because it runs mid-session from inside the event
// loop.
static void
terminate_and_reap_topbar_child(pid_t *pid)
{
  kill(*pid, SIGTERM);

  int status;
  pid_t result;
  int attempts = 0;

  for (;;) {
    result = waitpid(*pid, &status, WNOHANG);

    if (result == *pid || (result == -1 && errno == ECHILD)) {
      break;
    }

    if (result == -1 && errno == EINTR) {
      continue;
    }

    // [COMMENT] Action purpose: Not yet reaped -- back off briefly instead of
    // busy-looping, and give up after a bounded number of attempts (~1s
    // total) rather than risking an unbounded startup hang if the child is
    // somehow stuck. The OS reparents it to init on exit either way, so
    // giving up here does not leak it permanently.
    if (++attempts >= 1000) {
      fprintf(stderr,
          "error: could not reap topbar helper (pid %d) during startup "
          "cleanup\n",
          (int)*pid);
      break;
    }

    usleep(1000);
  }

  *pid = -1;
}

/* [COMMENT] Function purpose: Event-loop callback for the helper pipe. Reads
whatever is available without blocking, splits on newlines, and renders the
most recent complete line. Never blocks the compositor: a slow or wedged helper
simply stops producing updates. */
static int
topbar_readable(int fd, uint32_t mask, void *data)
{
  struct hikari_topbar_source *source = data;

  if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
    /* [COMMENT] Action purpose: The helper died. Drop the event source so the
    loop stops polling a dead fd; the bar keeps its last rendered content. */
    if (source->event_source != NULL) {
      wl_event_source_remove(source->event_source);
      source->event_source = NULL;
    }
    close(source->fd);
    source->fd = -1;
    return 0;
  }

  char chunk[4096];
  ssize_t n = -1;

  for (;;) {
    n = read(fd, chunk, sizeof(chunk));
    if (n == 0) {
      /* [COMMENT] Action purpose: EOF -- the helper closed its end of the
      pipe. Tear the source down the same way as the hangup path so the loop
      stops polling a dead, now readable-forever fd. */
      if (source->event_source != NULL) {
        wl_event_source_remove(source->event_source);
        source->event_source = NULL;
      }
      close(source->fd);
      source->fd = -1;
      return 0;
    }
    if (n <= 0) {
      break;
    }
    if (source->len + (size_t)n + 1 > source->cap) {
      size_t cap = source->cap ? source->cap * 2 : 8192;
      while (cap < source->len + (size_t)n + 1) {
        cap *= 2;
      }
      /* [COMMENT] Action purpose: Refuse to grow without bound. A stream with
      no newline would otherwise consume memory until the fail-fast allocator
      aborts the compositor. */
      if (cap > HIKARI_BAR_MAX_LINE) {
        source->len = 0;
        break;
      }
      char *buf = hikari_malloc(cap);
      if (source->buf != NULL) {
        memcpy(buf, source->buf, source->len);
        hikari_free(source->buf);
      }
      source->buf = buf;
      source->cap = cap;
    }

    memcpy(source->buf + source->len, chunk, (size_t)n);
    source->len += (size_t)n;
  }

  if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
    return 0;
  }

  if (source->buf == NULL || source->len == 0) {
    return 0;
  }

  source->buf[source->len] = '\0';

  /* [COMMENT] Action purpose: Render only the LAST complete line in the buffer.
  The helper ticks faster than a frame; intermediate lines are already stale, so
  parsing them would be wasted work. */
  char *last = NULL;
  char *start = source->buf;
  for (size_t i = 0; i < source->len; i++) {
    if (source->buf[i] == '\n') {
      source->buf[i] = '\0';
      last = start;
      start = source->buf + i + 1;
    }
  }

  if (last != NULL) {
    parse_line(source, last);
    refresh_all_bars();

    /* [COMMENT] Action purpose: Retain the trailing partial line so the next
    read completes it rather than discarding a split payload. */
    size_t remaining = source->len - (size_t)(start - source->buf);
    memmove(source->buf, start, remaining);
    source->len = remaining;
  }

  return 0;
}

void
hikari_topbar_source_init(struct hikari_topbar_source *source)
{
  memset(source, 0, sizeof(*source));
  source->fd = -1;
  source->pid = -1;

  int fds[2];
  if (pipe(fds) == -1) {
    fprintf(stderr, "error: could not create topbar pipe: %s\n", strerror(errno));
    return;
  }

  pid_t child = fork();
  if (child == -1) {
    fprintf(stderr, "error: could not fork topbar helper: %s\n", strerror(errno));
    close(fds[0]);
    close(fds[1]);
    return;
  }

  if (child == 0) {
    close(fds[0]);
    if (fds[1] != STDOUT_FILENO) {
      // [COMMENT] Action purpose: An unchecked dup2 would leave the helper
      // writing to the wrong descriptor, producing a permanently empty bar
      // with no diagnostic.
      if (dup2(fds[1], STDOUT_FILENO) == -1) {
        static const char err[] = "error: could not redirect topbar stdout\n";
        write(STDERR_FILENO, err, sizeof(err) - 1);
        _exit(EXIT_FAILURE);
      }
      close(fds[1]);
    }
    setsid();
    /* [COMMENT] Action purpose: Close every inherited descriptor above stderr
    -- listening sockets, DRM/GBM fds, and anything else the compositor had
    open -- before exec. Only stdin/stdout/stderr are meant to cross into the
    helper. */
    closefrom(STDERR_FILENO + 1);
    /* [COMMENT] Action purpose: Resolve the helper through the same
    compile-time absolute prefix used for hikari-unlocker, so a modified PATH
    cannot substitute a different binary into the compositor's pipeline. */
    execl(HIKARI_TOPBAR_PATH, "hikari-topbar", NULL);
    /* [COMMENT] Action purpose: fprintf is not async-signal-safe and must not
    be used this deep post-fork/pre-exec (its internal buffering can deadlock
    on a lock left held by the parent at fork time); write() to a raw fd is. */
    static const char msg[] = "error: could not execute hikari-topbar\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(EXIT_FAILURE);
  }

  close(fds[1]);
  source->pid = child;
  source->fd = fds[0];

  /* [COMMENT] Action purpose: Non-blocking is mandatory. This fd is serviced
  from the Wayland event loop; a blocking read here would stall every client
  whenever the helper is mid-tick. */
  int flags = fcntl(source->fd, F_GETFL, 0);
  if (flags == -1 || fcntl(source->fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    /* [COMMENT] Action purpose: Registering a blocking fd would wedge the event
    loop: topbar_readable reads until EAGAIN, which never arrives. Tear the
    helper down instead of running the bar at that cost. */
    fprintf(stderr, "error: could not set topbar pipe non-blocking\n");

    terminate_and_reap_topbar_child(&source->pid);

    close(source->fd);
    source->fd = -1;

    return;
  }

  source->event_source = wl_event_loop_add_fd(hikari_server.event_loop,
      source->fd,
      WL_EVENT_READABLE,
      topbar_readable,
      source);

  if (source->event_source == NULL) {
    fprintf(stderr, "error: could not register topbar event source\n");

    terminate_and_reap_topbar_child(&source->pid);

    close(source->fd);
    source->fd = -1;
  }
}

void
hikari_topbar_source_fini(struct hikari_topbar_source *source)
{
  if (source->event_source != NULL) {
    wl_event_source_remove(source->event_source);
    source->event_source = NULL;
  }

  if (source->fd >= 0) {
    close(source->fd);
    source->fd = -1;
  }

  /* [COMMENT] Action purpose: Signal then reap non-blockingly. The helper exits
  on SIGTERM; if it has not yet done so we leave it to init rather than blocking
  compositor shutdown on a child. */
  if (source->pid > 0) {
    kill(source->pid, SIGTERM);
    int status;
    pid_t result;
    do {
      result = waitpid(source->pid, &status, WNOHANG);
    } while (result == -1 && errno == EINTR);
    source->pid = -1;
  }

  clear_blocks(source);

  hikari_free(source->buf);
  source->buf = NULL;
  source->len = 0;
  source->cap = 0;
}

void
hikari_bar_init(struct hikari_bar *bar, struct hikari_output *output)
{
  memset(bar, 0, sizeof(*bar));

  bar->output = output;
  bar->scene_buffer = NULL;

  /* [COMMENT] Action purpose: Derive height from the configured font so the bar
  scales with the user's font choice instead of hardcoding pixels. */
  bar->height = hikari_configuration->font.height + HIKARI_BAR_PADDING;
  bar->enabled = true;
}

void
hikari_bar_fini(struct hikari_bar *bar)
{
  if (bar->scene_buffer != NULL) {
    wlr_scene_node_destroy(&bar->scene_buffer->node);
    bar->scene_buffer = NULL;
  }

  hikari_free(bar->cache_key);
  bar->cache_key = NULL;

  bar->enabled = false;
}

void
hikari_bar_reserve(struct hikari_bar *bar, struct wlr_box *usable_area)
{
  if (!bar->enabled || bar->height <= 0) {
    return;
  }

  usable_area->y += bar->height;
  usable_area->height -= bar->height;

  if (usable_area->height < 0) {
    usable_area->height = 0;
  }
}

void
hikari_bar_refresh(struct hikari_bar *bar)
{
  struct hikari_output *output = bar->output;

  if (!bar->enabled || output == NULL || !output->enabled) {
    return;
  }

  int width = output->geometry.width;
  int height = bar->height;

  if (width <= 0 || height <= 0) {
    return;
  }

  /* [COMMENT] Action purpose: output->geometry is in output-logical pixels,
  but the scene buffer's backing cairo surface must be allocated in physical
  pixels on scaled (HiDPI) outputs, or the bar renders blurry/undersized.
  cairo_scale() below keeps every drawing call in logical coordinates; only
  the surface allocation and the eventual wlr_scene_buffer_set_dest_size()
  call need to know about the scale factor. */
  float scale = output->wlr_output->scale;
  if (scale <= 0.0f) {
    scale = 1.0f;
  }
  int pixel_width = (int)(width * scale + 0.5f);
  int pixel_height = (int)(height * scale + 0.5f);
  if (pixel_width <= 0 || pixel_height <= 0) {
    return;
  }

  struct hikari_topbar_source *source = &hikari_server.topbar;

  /* [COMMENT] Action purpose: Skip the repaint entirely when neither the
  rendered content nor the geometry changed since the last frame -- the
  common case, since the helper ticks far faster than most blocks change. */
  char *key = build_cache_key(source);
  if (bar->scene_buffer != NULL && bar->cache_key != NULL &&
      bar->cache_width == width && bar->cache_height == height &&
      bar->cache_scale == scale && strcmp(bar->cache_key, key) == 0) {
    hikari_free(key);
    return;
  }

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pixel_width, pixel_height);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    hikari_free(key);
    return;
  }

  cairo_t *cairo = cairo_create(surface);
  cairo_scale(cairo, scale, scale);
  PangoLayout *layout = pango_cairo_create_layout(cairo);
  pango_layout_set_font_description(layout, hikari_configuration->font.desc);

  /* [COMMENT] Action purpose: Paint the bar background from the configured
  clear colour so it matches the compositor's own palette. */
  float *bg = hikari_configuration->clear;
  cairo_set_source_rgba(cairo, bg[0], bg[1], bg[2], 1.0);
  cairo_paint(cairo);

  /* [COMMENT] Action purpose: Two-pass layout. Right-aligned blocks are
  measured first so they can be laid out from the right edge inward, while
  left-aligned blocks flow from the left. This reproduces swaybar's behaviour
  without needing the helper's spacer block to be pixel-accurate. */
  int right_width = 0;
  for (int i = 0; i < source->nr_blocks; i++) {
    struct hikari_bar_block *block = &source->blocks[i];
    if (!block->align_right) {
      continue;
    }
    pango_layout_set_text(layout, block->full_text, -1);
    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    right_width += w > block->min_width ? w : block->min_width;
  }

  int left_x = HIKARI_BAR_PADDING;
  int right_x = width - HIKARI_BAR_PADDING - right_width;

  for (int i = 0; i < source->nr_blocks; i++) {
    struct hikari_bar_block *block = &source->blocks[i];

    pango_layout_set_text(layout, block->full_text, -1);
    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);

    int advance = w > block->min_width ? w : block->min_width;

    if (block->has_color) {
      cairo_set_source_rgba(cairo,
          block->color[0], block->color[1], block->color[2], block->color[3]);
    } else {
      float *fg = hikari_configuration->border_active;
      cairo_set_source_rgba(cairo, fg[0], fg[1], fg[2], 1.0);
    }

    int x;
    if (block->align_right) {
      x = right_x;
      right_x += advance;
    } else {
      x = left_x;
      left_x += advance;
    }

    /* [COMMENT] Action purpose: Skip a block whose run starts outside the
    output width, without stopping the loop. Left- and right-aligned blocks
    are laid out from independent origins in the same pass, so one
    overflowing left-aligned block (e.g. behind a wide spacer) must not
    suppress the right-aligned blocks that still fit. */
    if (x > width) {
      continue;
    }

    /* [COMMENT] Action purpose: Centre the text vertically. The division is
    done in floating point because cairo_move_to takes doubles -- integer
    division here would quantise the baseline and shift text off-centre by up
    to a pixel on odd height differences. */
    cairo_move_to(cairo, (double)x, (double)(height - h) / 2.0);
    pango_cairo_update_layout(cairo, layout);
    pango_cairo_show_layout(cairo, layout);
  }

  cairo_surface_flush(surface);

  unsigned char *data = cairo_image_surface_get_data(surface);
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pixel_width);

  struct wlr_buffer *buffer =
      hikari_server_create_argb8888_buffer(pixel_width, pixel_height, data, stride);

  if (buffer == NULL) {
    /* [COMMENT] Action purpose: Report rather than fail silently. An invisible
    bar with no diagnostic is exactly the failure mode that made this hard to
    track down the first time. */
    fprintf(stderr,
        "error: could not create bar buffer for output \"%s\" (%dx%d)\n",
        output->wlr_output->name,
        pixel_width,
        pixel_height);
  } else {
    bool newly_created = bar->scene_buffer == NULL;
    if (newly_created) {
      bar->scene_buffer =
          wlr_scene_buffer_create(&hikari_server.scene->tree, buffer);
    } else {
      wlr_scene_buffer_set_buffer(bar->scene_buffer, buffer);
    }

    if (bar->scene_buffer != NULL) {
      /* [COMMENT] Action purpose: Position in layout-absolute coordinates --
      the bar is parented to the scene root, not to an output-local tree, so
      the output origin must be added explicitly. */
      wlr_scene_node_set_position(&bar->scene_buffer->node,
          output->geometry.x, output->geometry.y);
      /* [COMMENT] Action purpose: The backing buffer is in physical pixels
      (pixel_width x pixel_height) but the scene node must occupy logical
      output space; dest_size tells wlr_scene to scale it down accordingly. */
      wlr_scene_buffer_set_dest_size(bar->scene_buffer, width, height);
      /* [COMMENT] Action purpose: Raise only once, at creation. Raising on
      every refresh (which happens multiple times a second) would repeatedly
      pull the bar above the lock indicator and other overlays that are
      raised less often, fighting their own raise_to_top calls. */
      if (newly_created) {
        wlr_scene_node_raise_to_top(&bar->scene_buffer->node);
      }
      wlr_scene_node_set_enabled(&bar->scene_buffer->node, true);
    }

    wlr_buffer_drop(buffer);

    /* [COMMENT] Action purpose: Only adopt the new identity once the buffer
    actually made it to the scene -- a failed render should not poison the
    cache into skipping the next (potentially successful) refresh. */
    hikari_free(bar->cache_key);
    bar->cache_key = key;
    key = NULL;
    bar->cache_width = width;
    bar->cache_height = height;
    bar->cache_scale = scale;
  }

  hikari_free(key);

  g_object_unref(layout);
  cairo_destroy(cairo);
  cairo_surface_destroy(surface);
}
