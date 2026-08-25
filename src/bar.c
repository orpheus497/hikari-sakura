// [COMMENT] Script function and purpose: Native top bar rendering for hikari.
// Consumes swaybar-protocol JSON from the hikari-topbar helper over a
// non-blocking pipe and paints it into the scene graph with cairo/Pango.

#include <hikari/bar.h>

#include <hikari/buffer.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include <hikari/color.h>
#include <hikari/configuration.h>
#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/server.h>
/* [COMMENT] Action purpose: hikari_bar_update_visibility() asks the view layer
whether anything on this output is fullscreen, which needs the full definition
of struct hikari_view -- output.h forward-declares it only. */
#include <hikari/view.h>
#include <hikari/workspace.h>

/* [COMMENT] Action purpose: Horizontal padding applied at each end of the bar,
and as the gap each run leaves before the run that follows it.

The second half of that was a lie until Phase 90: this comment claimed padding
was applied "between the left-aligned and right-aligned block runs", and it was
not -- the value was only ever used at the two outer edges, and the runs had no
awareness of each other at all. It is now genuinely used as the inter-run gap
when computing each run's right-hand limit. */
#define HIKARI_BAR_PADDING 6

/* [COMMENT] Action purpose: Upper bound on a single helper line. The swaybar
protocol emits one array per tick; anything beyond this is a malformed or
hostile stream and is discarded rather than grown without limit. */
#define HIKARI_BAR_MAX_LINE (64 * 1024)

/* [COMMENT] Action purpose: Upper bound on a block's requested MIN_WIDTH -- the
swaybar `min_width` field, which pads a block out to a fixed pixel width.

This previously claimed to be an "upper bound on a single block's requested
width" such that "any wider request cannot be displayed". It never was: it is
applied in json_int_field()'s clamp and bounds nothing about how wide a block's
TEXT renders, which was unbounded and is what let one long block paint across
the whole bar. Rendered width is now bounded structurally by the per-run clip in
hikari_bar_refresh(). Corrected Phase 90. */
#define HIKARI_BAR_MAX_BLOCK_WIDTH 8192

/* [COMMENT] Action purpose: Longest block, in codepoints, that may be requested
through `ui { bar { max-block-chars } }`. Bounds the per-block scroll buffer
below; a status-bar block anywhere near this is already unreadable. */
#define HIKARI_BAR_MAX_CAP_CHARS 256

/* [COMMENT] Action purpose: Bytes needed for one scrolled block's window --
HIKARI_BAR_MAX_CAP_CHARS codepoints at UTF-8's 4-byte maximum, plus the
terminator. Only blocks actually over the cap use one of these; a block that
fits is pointed at directly with a length and never copied, which is why
disabling the cap entirely (max-block-chars = 0) needs no buffer at all. */
#define HIKARI_BAR_SCROLL_BYTES (HIKARI_BAR_MAX_CAP_CHARS * 4 + 1)

/* [COMMENT] Action purpose: The bar draws with cairo and hands the pixels to
hikari_buffer_create_argb8888(), which is CPU-backed rather than
allocator-backed. That is not a platform workaround: wlroots exposes no
allocator a compositor can write pixels into, on any platform, so every
compositor that draws its own UI supplies a wlr_buffer_impl of its own. See
BLUEPRINT.md section 13, FB-2, which corrects the earlier Phase 33 framing, and
the implementation in src/buffer.c. */

void
hikari_bar_config_init(struct hikari_bar_config *bar_config)
{
  bar_config->max_block_chars = 26;
  bar_config->scroll_interval = 300;

  static const char default_separator[] = "   •   ";
  bar_config->scroll_separator = hikari_malloc(sizeof(default_separator));
  memcpy(bar_config->scroll_separator, default_separator,
      sizeof(default_separator));
}

void
hikari_bar_config_fini(struct hikari_bar_config *bar_config)
{
  hikari_free(bar_config->scroll_separator);
  bar_config->scroll_separator = NULL;
}

/* [COMMENT] Function purpose: Byte length of one UTF-8 sequence starting at s,
or 0 if what is there is not a well-formed sequence.

Written out rather than reached for from glib because the result is needed for
two different jobs -- refusing malformed input, and stepping the scroll by
codepoints -- and because it lets the rejection be exact. Rejects overlong
encodings, surrogates and anything above U+10FFFF, all of which are ill-formed
UTF-8 that a naive length-table decoder would wave through. */
static int
utf8_sequence_len(const char *s)
{
  const unsigned char *u = (const unsigned char *)s;
  unsigned char c = u[0];

  if (c < 0x80) {
    return 1;
  }

  int len;
  unsigned int cp;

  if ((c & 0xe0) == 0xc0) {
    len = 2;
    cp = c & 0x1fu;
  } else if ((c & 0xf0) == 0xe0) {
    len = 3;
    cp = c & 0x0fu;
  } else if ((c & 0xf8) == 0xf0) {
    len = 4;
    cp = c & 0x07u;
  } else {
    return 0; /* continuation byte or 5/6-byte form: not valid UTF-8 */
  }

  for (int i = 1; i < len; i++) {
    if ((u[i] & 0xc0) != 0x80) {
      return 0;
    }
    cp = (cp << 6) | (u[i] & 0x3fu);
  }

  /* [COMMENT] Action purpose: Overlong forms encode a codepoint in more bytes
  than needed and are a classic way to smuggle characters past a filter; UTF-16
  surrogates have no business in UTF-8; and nothing above U+10FFFF exists.
  Pango's own validation rejects all three, so accepting them here would only
  move the failure. */
  static const unsigned int minimum[] = { 0, 0, 0x80, 0x800, 0x10000 };
  if (cp < minimum[len] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
    return 0;
  }

  return len;
}

/* [COMMENT] Function purpose: Length in bytes of the longest leading run of s
that is valid UTF-8.

Every string reaching the layout has to pass through this. pango_layout_set_text
() requires valid UTF-8 and hikari cannot promise it: the helper reads MPRIS
metadata with fgets() into a fixed 128-byte buffer and escapes it into another
fixed buffer, and BOTH truncate on a byte boundary -- so a track title with an
accent or a CJK glyph landing near the limit already arrives here cut through
the middle of a sequence. json_string_field() then copies those bytes without
inspecting them. This is a live defect independent of the character cap; the cap
merely makes it routine rather than occasional. */
static size_t
utf8_valid_prefix_len(const char *s)
{
  size_t off = 0;

  while (s[off] != '\0') {
    int len = utf8_sequence_len(s + off);

    if (len == 0) {
      break;
    }
    off += (size_t)len;
  }

  return off;
}

/* [COMMENT] Function purpose: Count codepoints in the valid prefix of s, and
report the prefix's byte length through *valid_bytes. */
static int
utf8_count(const char *s, size_t *valid_bytes)
{
  size_t off = 0;
  int count = 0;

  while (s[off] != '\0') {
    int len = utf8_sequence_len(s + off);

    if (len == 0) {
      break;
    }
    off += (size_t)len;
    count++;
  }

  if (valid_bytes != NULL) {
    *valid_bytes = off;
  }

  return count;
}

/* [COMMENT] Function purpose: Byte offset of the nth codepoint of s, clamped to
the end of the valid prefix. */
static size_t
utf8_offset_of(const char *s, int n)
{
  size_t off = 0;

  for (int i = 0; i < n && s[off] != '\0'; i++) {
    int len = utf8_sequence_len(s + off);

    if (len == 0) {
      break;
    }
    off += (size_t)len;
  }

  return off;
}

/* [COMMENT] Function purpose: Decide whether a block needs the banner treatment
and, if so, how many codepoints its scroll cycle spans.

The cycle is the text plus the separator, so the offset wraps through the
separator and back into the title rather than snapping from the end to the
start. Returns 0 when the block fits and should be drawn verbatim. */
static int
block_scroll_period(const struct hikari_bar_block *block, int cap)
{
  if (cap <= 0 || block->full_text == NULL) {
    return 0;
  }

  int chars = utf8_count(block->full_text, NULL);

  if (chars <= cap) {
    return 0;
  }

  const char *separator = hikari_configuration->bar_config.scroll_separator;
  int sep_chars = separator != NULL ? utf8_count(separator, NULL) : 0;

  return chars + sep_chars;
}

/* [COMMENT] Function purpose: Produce the text actually drawn for a block --
either the block verbatim, or a `cap`-codepoint window onto text+separator
starting at the block's current scroll offset.

Always writes a NUL-terminated, VALID UTF-8 string into out, so this is the one
place the layout's input contract is established. Returns out. */
static const char *
block_display_text(const struct hikari_bar_block *block,
    int cap,
    char *out,
    size_t out_size)
{
  const char *text = block->full_text != NULL ? block->full_text : "";

  int period = block_scroll_period(block, cap);

  if (period == 0) {
    /* [COMMENT] Action purpose: Fits the cap, so it is drawn as-is -- but only
    its VALID prefix, because an invalid tail handed to Pango is undefined
    behaviour. Copied a whole sequence at a time so a buffer that runs out
    cannot itself sever one, which would reintroduce the bug being fixed. */
    size_t written = 0;
    size_t off = 0;

    while (text[off] != '\0') {
      int len = utf8_sequence_len(text + off);

      if (len == 0 || written + (size_t)len + 1 > out_size) {
        break;
      }

      memcpy(out + written, text + off, (size_t)len);
      written += (size_t)len;
      off += (size_t)len;
    }

    out[written] = '\0';

    return out;
  }

  const char *separator = hikari_configuration->bar_config.scroll_separator;
  if (separator == NULL) {
    separator = " ";
  }

  int text_chars = utf8_count(text, NULL);
  int sep_chars = utf8_count(separator, NULL);

  int start = block->scroll_offset;
  if (period > 0) {
    start %= period;
    if (start < 0) {
      start += period;
    }
  }

  size_t written = 0;

  /* [COMMENT] Action purpose: Walk `cap` codepoints from `start`, wrapping
  through the separator and back into the title. Indices are taken modulo the
  period on every step rather than by splicing three substrings, so the wrap
  point needs no special case. */
  for (int i = 0; i < cap; i++) {
    int idx = (start + i) % period;

    const char *src;
    int src_idx;

    if (idx < text_chars) {
      src = text;
      src_idx = idx;
    } else {
      src = separator;
      src_idx = idx - text_chars;

      if (src_idx >= sep_chars) {
        continue;
      }
    }

    size_t begin = utf8_offset_of(src, src_idx);
    int len = utf8_sequence_len(src + begin);

    if (len == 0 || written + (size_t)len + 1 > out_size) {
      break;
    }

    memcpy(out + written, src + begin, (size_t)len);
    written += (size_t)len;
  }

  out[written] = '\0';

  return out;
}

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

  /* [COMMENT] Action purpose: Accept both "#rrggbb" and "#rrggbbaa", matching
  the alpha-capable colour form the compositor's own colourscheme now takes. A
  block without an alpha component stays fully opaque. */
  size_t len = strlen(text);
  if (len != 7 && len != 9) {
    return false;
  }

  for (size_t i = 1; i < len; i++) {
    if (!isxdigit((unsigned char)text[i])) {
      return false;
    }
  }

  unsigned int r, g, b, a = 0xff;
  if (len == 9) {
    if (sscanf(text + 1, "%2x%2x%2x%2x", &r, &g, &b, &a) != 4) {
      return false;
    }
  } else if (sscanf(text + 1, "%2x%2x%2x", &r, &g, &b) != 3) {
    return false;
  }

  color[0] = (float)r / 255.0f;
  color[1] = (float)g / 255.0f;
  color[2] = (float)b / 255.0f;
  color[3] = (float)a / 255.0f;

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
  const char *p = strchr(line, '[');
  if (p == NULL) {
    /* [COMMENT] Action purpose: A malformed line leaves the previous blocks in
    place rather than blanking the bar for a tick. */
    return;
  }

  /* [COMMENT] Action purpose: MOVE the previous block set aside, taking
  ownership of its strings, so a still-scrolling block can keep its position.

  The helper re-emits every block several times a second; resetting
  scroll_offset with the block set would restart the banner on every tick and
  nothing would ever appear to move. The move must be an ownership transfer
  rather than a copy plus clear_blocks() -- clear_blocks() frees the strings,
  and the comparison below reads them. They are released at the end of this
  function instead, once nothing refers to them. */
  struct hikari_bar_block previous[HIKARI_BAR_MAX_BLOCKS];
  int nr_previous = source->nr_blocks;

  memcpy(previous, source->blocks,
      sizeof(struct hikari_bar_block) * (size_t)nr_previous);

  source->nr_blocks = 0;

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

    /* [COMMENT] Action purpose: Map the alignment string onto the three-way
    lane. Anything unrecognised (including absent) falls back to left, matching
    the swaybar default. This previously tested only for "right", so "center"
    silently became left -- which is why the clock could be right-aligned but
    nothing could ever be centred. */
    char *align = json_string_field(obj, end, "align");
    if (align != NULL && strcmp(align, "right") == 0) {
      block->align = HIKARI_BAR_ALIGN_RIGHT;
    } else if (align != NULL && strcmp(align, "center") == 0) {
      block->align = HIKARI_BAR_ALIGN_CENTER;
    } else {
      block->align = HIKARI_BAR_ALIGN_LEFT;
    }
    hikari_free(align);

    /* [COMMENT] Action purpose: Carry the scroll position forward when this
    slot still holds the same text. Matching by index as well as by text keeps
    it cheap and is exactly right for this producer: the helper emits a fixed
    sequence of blocks, so a given slot is the same conceptual block from tick
    to tick. A changed string -- a new track -- drops the offset, so the banner
    restarts from the beginning of the new title rather than resuming halfway
    through it. */
    if (block->full_text != NULL && source->nr_blocks < nr_previous) {
      struct hikari_bar_block *old = &previous[source->nr_blocks];

      if (old->full_text != NULL &&
          strcmp(old->full_text, block->full_text) == 0) {
        block->scroll_offset = old->scroll_offset;
      }
    }

    if (block->full_text != NULL) {
      source->nr_blocks++;
    }

    p = end + 1;
  }

  /* [COMMENT] Action purpose: Release the strings the move above took ownership
  of. Last, once the comparison loop can no longer reach them. */
  for (int i = 0; i < nr_previous; i++) {
    hikari_free(previous[i].full_text);
  }
}

/* [COMMENT] Function purpose: Append one formatted fragment to the growing cache
key, resizing when it does not fit.

The sizing pass and the writing pass are driven from one call site with one
argument list, so they cannot drift: previously each fragment was two duplicated
literal format strings, and adding a field meant editing both -- editing only one
would size the buffer for a shorter key than gets written. */
static void
key_append(char **key, size_t *len, size_t *cap, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  int needed = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  if (needed < 0) {
    return;
  }

  while (*len + (size_t)needed + 1 > *cap) {
    *cap *= 2;
    char *grown = hikari_malloc(*cap);
    memcpy(grown, *key, *len + 1);
    hikari_free(*key);
    *key = grown;
  }

  va_start(args, fmt);
  *len += (size_t)vsnprintf(*key + *len, *cap - *len, fmt, args);
  va_end(args);
}

/* [COMMENT] Function purpose: Serialise everything that determines what would be
painted into a flat string, so hikari_bar_refresh can detect a no-op repaint.

Two kinds of input go in, and both are needed for the claim in that sentence to
be true:

  * Per block -- full_text, min_width, alignment, colour, and scroll_offset.
    scroll_offset has to be here because it is the ONLY thing that differs
    between two frames of a banner scroll; omitting it would make every step look
    identical to the cache and the text would never move.

  * The display parameters those blocks are rendered THROUGH -- max_block_chars
    and scroll_separator. Both change the pixels for identical telemetry:
    max_block_chars decides whether a block is capped and how wide its window is,
    and scroll_separator is spliced into the scroll cycle and changes its period.
    Leaving them out meant a config reload did not invalidate anything, and since
    hikari_configuration_reload() does not repaint bars either, the bar kept
    rendering at the OLD cap until some block's text happened to change on its
    own -- usually the next CPU-percentage tick, but indefinitely on an idle
    machine with steady telemetry. That contradicted the documented behaviour
    that these keys take effect on reload rather than needing a restart.

scroll_interval is deliberately NOT included: it changes only how often a step is
taken, never what a given frame looks like, and update_scroll_timer() and
scroll_timer_handler() both re-read it live. */
static char *
build_cache_key(struct hikari_topbar_source *source)
{
  size_t cap = 256;
  char *key = hikari_malloc(cap);
  size_t len = 0;
  key[0] = '\0';

  const struct hikari_bar_config *bar_config =
      &hikari_configuration->bar_config;
  const char *separator = bar_config->scroll_separator != NULL
                              ? bar_config->scroll_separator
                              : "";

  key_append(&key, &len, &cap, "%d\x1f%s", bar_config->max_block_chars,
      separator);

  for (int i = 0; i < source->nr_blocks; i++) {
    struct hikari_bar_block *block = &source->blocks[i];
    const char *text = block->full_text != NULL ? block->full_text : "";

    key_append(&key, &len, &cap,
        "\x1f%s\x1f%d\x1f%d\x1f%d\x1f%.6f,%.6f,%.6f,%.6f", text,
        block->min_width, (int)block->align, block->scroll_offset,
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

static int
scroll_timer_handler(void *data);

/* [COMMENT] Function purpose: Arm the scroll timer if any block is currently
over the cap, and disarm it if none is.

Called after every parse, so the timer's existence tracks whether there is
anything to animate. THIS IS THE POINT: a step repaints the whole bar, so a
permanently-armed timer would have the compositor re-rendering several times a
second for the entire session. With nothing playing, or with a title that fits,
there is no timer and no wakeups at all. */
static void
update_scroll_timer(struct hikari_topbar_source *source)
{
  int cap = hikari_configuration->bar_config.max_block_chars;

  bool wanted = false;
  for (int i = 0; i < source->nr_blocks; i++) {
    if (block_scroll_period(&source->blocks[i], cap) > 0) {
      wanted = true;
      break;
    }
  }

  if (!wanted) {
    if (source->scroll_armed && source->scroll_timer != NULL) {
      /* [COMMENT] Action purpose: Disarm by updating to 0, which cancels a
      wl_event_source_timer without destroying it -- the source is reusable, so
      it is created once and kept for the process lifetime rather than churned
      every time a track ends. */
      wl_event_source_timer_update(source->scroll_timer, 0);
      source->scroll_armed = false;
    }
    return;
  }

  if (source->scroll_timer == NULL) {
    source->scroll_timer = wl_event_loop_add_timer(
        hikari_server.event_loop, scroll_timer_handler, source);

    if (source->scroll_timer == NULL) {
      /* [COMMENT] Action purpose: Degrade rather than fail. Without a timer the
      banner does not move, but the text is still capped and still clipped, so
      the bar remains correct -- just static. */
      wlr_log(WLR_ERROR,
          "could not create the top bar scroll timer; long blocks will be "
          "truncated but will not scroll");
      return;
    }
  }

  if (!source->scroll_armed) {
    wl_event_source_timer_update(
        source->scroll_timer, hikari_configuration->bar_config.scroll_interval);
    source->scroll_armed = true;
  }
}

/* [COMMENT] Function purpose: Advance every over-long block by one codepoint and
repaint.

wl_event_loop timers are one-shot, so the re-arm at the end is what keeps the
banner moving; dropping it would scroll exactly one step. Re-reads the interval
from configuration each time so a config reload takes effect on the next step
rather than needing a restart. */
static int
scroll_timer_handler(void *data)
{
  struct hikari_topbar_source *source = data;

  int cap = hikari_configuration->bar_config.max_block_chars;
  bool moved = false;

  for (int i = 0; i < source->nr_blocks; i++) {
    struct hikari_bar_block *block = &source->blocks[i];
    int period = block_scroll_period(block, cap);

    if (period <= 0) {
      /* [COMMENT] Action purpose: Reset rather than leave stale. A block that
      has stopped overflowing must not keep a non-zero offset, or it would
      resume mid-word if it ever grows again. */
      block->scroll_offset = 0;
      continue;
    }

    block->scroll_offset = (block->scroll_offset + 1) % period;
    moved = true;
  }

  if (moved) {
    refresh_all_bars();
    wl_event_source_timer_update(
        source->scroll_timer, hikari_configuration->bar_config.scroll_interval);
  } else {
    source->scroll_armed = false;
  }

  return 0;
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
  bool killed = false;

  for (;;) {
    result = waitpid(*pid, &status, WNOHANG);

    if (result == *pid || (result == -1 && errno == ECHILD)) {
      *pid = -1;
      return;
    }

    if (result == -1 && errno == EINTR) {
      continue;
    }

    // [COMMENT] Action purpose: Not yet reaped -- back off briefly instead of
    // busy-looping. After the same ~1s bounded wait SIGTERM was given,
    // escalate to SIGKILL once rather than giving up while the child (and
    // its reference) is still alive: an ignored/blocked SIGTERM would
    // otherwise leave *pid cleared while the process keeps running, and
    // SIGKILL cannot be caught or blocked, so this reap attempt still
    // completes within a bounded time even for a wedged child.
    if (!killed && ++attempts >= 1000) {
      kill(*pid, SIGKILL);
      killed = true;
      attempts = 0;
      continue;
    }

    if (killed && ++attempts >= 1000) {
      fprintf(stderr,
          "error: could not reap topbar helper (pid %d) during startup "
          "cleanup, even after SIGKILL\n",
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

    /* [COMMENT] Action purpose: Re-evaluate whether anything still needs to
    scroll before repainting -- a track that just ended should stop the timer on
    the same tick its text disappears, not on the next one. */
    update_scroll_timer(source);

    refresh_all_bars();

    /* [COMMENT] Action purpose: Retain the trailing partial line so the next
    read completes it rather than discarding a split payload. */
    size_t remaining = source->len - (size_t)(start - source->buf);
    memmove(source->buf, start, remaining);
    source->len = remaining;
  }

  return 0;
}

/* [COMMENT] Function purpose: Render the configured palette as the single
argument the helper is spawned with -- sixteen "#rrggbb" values, comma
separated.

Built in the PARENT, deliberately. The only alternative that reaches a child is
setenv(), and calling it between fork() and exec() in a process wlroots has
given threads to is not async-signal-safe -- the same hazard the write() below
the exec already documents. An argument is composed while it is still safe to
compose anything.

This is how the compositor's theme reaches the bar. hikari-topbar previously had
no source of colour but ~/.cache/wal/colors, so a machine without pywal drew
every block in white; that path is retained as the fallback, so nothing that
worked before stops working. */
static void
format_palette(char *buffer, size_t size)
{
  size_t offset = 0;

  for (int i = 0; i < HIKARI_NR_OF_PALETTE_COLORS; i++) {
    float *color = hikari_configuration->palette[i];

    int written = snprintf(buffer + offset,
        size - offset,
        "%s#%02x%02x%02x",
        i == 0 ? "" : ",",
        (unsigned)(color[0] * 255.0f + 0.5f),
        (unsigned)(color[1] * 255.0f + 0.5f),
        (unsigned)(color[2] * 255.0f + 0.5f));

    /* [COMMENT] Action purpose: Truncation cannot happen with the buffer the
    caller declares (16 * 7 + 15 + 1 = 128 bytes), but a silently half-written
    palette would be a very confusing bar -- so a short buffer terminates what
    has been written and stops rather than emitting a malformed tail. */
    if (written < 0 || (size_t)written >= size - offset) {
      buffer[offset] = '\0';
      return;
    }

    offset += (size_t)written;
  }
}

void
hikari_topbar_source_init(struct hikari_topbar_source *source)
{
  memset(source, 0, sizeof(*source));
  source->fd = -1;
  source->pid = -1;

  char palette[160];
  format_palette(palette, sizeof(palette));

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
    /* [COMMENT] Action purpose: The palette is passed as argv[1]. The helper is
    spawned once and never restarted, so -- like everything else it is told --
    a palette change reaches it only on the next compositor start, not on a
    configuration reload. */
    execl(HIKARI_TOPBAR_PATH, "hikari-topbar", palette, NULL);
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
  /* [COMMENT] Action purpose: Drop the scroll timer before the event loop it is
  registered with is destroyed, on the same terms as the pipe's event source
  below. */
  if (source->scroll_timer != NULL) {
    wl_event_source_remove(source->scroll_timer);
    source->scroll_timer = NULL;
  }
  source->scroll_armed = false;

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
hikari_bar_update_visibility(struct hikari_output *output)
{
  assert(output != NULL);

  struct hikari_bar *bar = &output->bar;

  if (!bar->enabled) {
    return;
  }

  /* [COMMENT] Action purpose: workspace->views, not output->views. The former
  is the VISIBLE set for the displayed sheet; the latter is every view on the
  output including those parked on sheets that are not showing. Using the wrong
  one would hide the bar on account of a fullscreen video sitting on a sheet the
  user has switched away from.

  workspace is NULL during output teardown, which is one of the paths that
  reaches here, so it is tested rather than assumed. */
  bool obscured = false;
  struct hikari_workspace *workspace = output->workspace;

  if (workspace != NULL) {
    struct hikari_view *view;
    wl_list_for_each (view, &workspace->views, workspace_views) {
      if (hikari_view_is_fullscreen(view) && !hikari_view_is_hidden(view)) {
        obscured = true;
        break;
      }
    }
  }

  if (obscured == bar->obscured) {
    return;
  }

  bar->obscured = obscured;

  if (bar->scene_buffer != NULL) {
    wlr_scene_node_set_enabled(&bar->scene_buffer->node, !obscured);
  }

  /* [COMMENT] Action purpose: The bar's strip has just changed what it shows.
  Nothing else damages it -- the view underneath was already drawn, and the
  scene node toggling does not itself schedule a frame. */
  hikari_output_damage_whole(output);
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

  /* [COMMENT] Action purpose: Re-assert the obscured state on every refresh,
  BEFORE the cache short-circuit below.

  The order is load-bearing. hikari_bar_update_visibility() sets the node's
  enabled bit directly at the moment a view enters or leaves fullscreen, and
  that is the fast path; this is the belt-and-braces one, covering any path that
  changes the answer without routing through a call site -- and it would be
  useless underneath the cache check, because the telemetry text is unchanged on
  exactly the frames where only visibility moved, so the function would return
  before ever reaching it. */
  if (bar->scene_buffer != NULL) {
    wlr_scene_node_set_enabled(&bar->scene_buffer->node, !bar->obscured);
  }

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

  /* [COMMENT] Action purpose: Paint the bar background from its own configured
  colour, honouring alpha. Two things changed here: the colour is no longer
  borrowed from `clear` (the output background), so tinting or fading the bar no
  longer alters the desktop behind every window; and the alpha component is
  actually used instead of being overridden with a hardcoded 1.0, which is what
  made the bar unconditionally opaque no matter what was configured.

  cairo_paint() with alpha < 1 writes a premultiplied translucent surface, and
  the buffer this ends up in is DRM_FORMAT_ARGB8888 (also premultiplied), so the
  two agree and wlr_scene blends the bar over whatever is beneath it. See
  DECISIONS_LOG Phase 60. */
  float *bg = hikari_configuration->bar;
  cairo_set_source_rgba(cairo, bg[0], bg[1], bg[2], bg[3]);

  /* [COMMENT] Action purpose: CAIRO_OPERATOR_SOURCE replaces the destination
  rather than blending onto it. The surface is freshly created and therefore
  already transparent, but painting a translucent colour with the default OVER
  operator would still be a no-op-ish blend against transparent black; SOURCE
  makes the resulting buffer carry exactly the requested alpha. */
  cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cairo);
  cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);

  /* [COMMENT] Action purpose: Two-pass layout. The centred and right-aligned
  runs are measured first so each can be given a starting origin that depends on
  its own total width; the left run needs no measurement because it simply flows
  from the left edge. The centre run is anchored to the true midpoint of the
  output, which is what makes it stay centred regardless of how wide the left
  run happens to be -- the previous fixed-width spacer could not do that. */
  int cap = hikari_configuration->bar_config.max_block_chars;

  /* [COMMENT] Action purpose: Resolve every block's DISPLAYED text once, up
  front, and lay out from that rather than from full_text. The measure pass, the
  origins and the draw must all agree on the same string, or the centre run is
  measured at one width and painted at another.

  Held as (pointer, byte length) rather than as copies. A block that fits points
  straight at full_text with the length of its VALID UTF-8 prefix -- no copy, and
  no upper bound needed on how long the helper's text may be, which matters
  because max-block-chars = 0 disables capping entirely. Only a block that is
  actually scrolling needs a buffer, and that is bounded by the cap. */
  const char *display[HIKARI_BAR_MAX_BLOCKS];
  int display_len[HIKARI_BAR_MAX_BLOCKS];
  char scroll_buf[HIKARI_BAR_MAX_BLOCKS][HIKARI_BAR_SCROLL_BYTES];

  for (int i = 0; i < source->nr_blocks; i++) {
    struct hikari_bar_block *block = &source->blocks[i];
    const char *text = block->full_text != NULL ? block->full_text : "";

    if (block_scroll_period(block, cap) == 0) {
      display[i] = text;
      display_len[i] = (int)utf8_valid_prefix_len(text);
    } else {
      display[i] = block_display_text(
          block, cap, scroll_buf[i], HIKARI_BAR_SCROLL_BYTES);
      display_len[i] = (int)strlen(display[i]);
    }
  }

  int center_width = 0;
  int right_width = 0;
  for (int i = 0; i < source->nr_blocks; i++) {
    struct hikari_bar_block *block = &source->blocks[i];

    if (block->align == HIKARI_BAR_ALIGN_LEFT) {
      continue;
    }

    pango_layout_set_text(layout, display[i], display_len[i]);
    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    int advance = w > block->min_width ? w : block->min_width;

    if (block->align == HIKARI_BAR_ALIGN_CENTER) {
      center_width += advance;
    } else {
      right_width += advance;
    }
  }

  int left_x = HIKARI_BAR_PADDING;
  int center_x = (width - center_width) / 2;
  int right_x = width - HIKARI_BAR_PADDING - right_width;

  /* [COMMENT] Action purpose: Clamp both computed origins to the left padding.
  A centre or right run wider than the output produced a NEGATIVE origin, which
  drew that run off the left edge and straight across the left run. The
  character cap makes this unlikely, but the guarantee below must not depend on
  the cap being configured -- max-block-chars = 0 disables it. */
  if (center_x < HIKARI_BAR_PADDING) {
    center_x = HIKARI_BAR_PADDING;
  }
  if (right_x < HIKARI_BAR_PADDING) {
    right_x = HIKARI_BAR_PADDING;
  }

  /* [COMMENT] Action purpose: The right edge each run may not paint past.

  This is what actually stops the media block writing across the clock, and it
  is deliberately structural rather than a length policy: no helper output, however
  long or however hostile, can now paint outside its own run. The left run stops
  where the centre run begins -- or where the right run begins when there is no
  centre content -- and the centre run stops where the right run begins.

  The previous guard tested `x > width`, i.e. whether a block's ORIGIN had left
  the output. A block starting inside and running 900px wide passed it and drew
  the whole way across, which is exactly what was happening. */
  int center_limit = right_x - HIKARI_BAR_PADDING;
  int left_limit = center_width > 0 ? center_x - HIKARI_BAR_PADDING
                                    : right_x - HIKARI_BAR_PADDING;
  int right_limit = width - HIKARI_BAR_PADDING;

  for (int i = 0; i < source->nr_blocks; i++) {
    struct hikari_bar_block *block = &source->blocks[i];

    pango_layout_set_text(layout, display[i], display_len[i]);
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

    /* [COMMENT] Action purpose: Each run advances its own cursor, so blocks
    within a run keep their emission order left-to-right regardless of which
    run they belong to. */
    int x;
    int limit;
    switch (block->align) {
      case HIKARI_BAR_ALIGN_CENTER:
        x = center_x;
        limit = center_limit;
        center_x += advance;
        break;

      case HIKARI_BAR_ALIGN_RIGHT:
        x = right_x;
        limit = right_limit;
        right_x += advance;
        break;

      case HIKARI_BAR_ALIGN_LEFT:
      default:
        x = left_x;
        limit = left_limit;
        left_x += advance;
        break;
    }

    /* [COMMENT] Action purpose: Skip a block with no room left in its run,
    without stopping the loop. The three runs are laid out from independent
    origins in the same pass, so one overflowing block must not suppress the
    blocks of another run that still fit. */
    int room = limit - x;
    if (room <= 0) {
      continue;
    }

    /* [COMMENT] Action purpose: Hard-clip each block to the room its run has
    left. This is the structural half of the containment guarantee -- the
    character cap is a presentation policy on top of it, and can be switched off
    (max-block-chars = 0) without reopening the hole.

    cairo_clip() is used rather than pango_layout_set_width() plus
    PANGO_ELLIPSIZE_END: set_width without ellipsize WRAPS instead of
    truncating, and the exact interaction of width, ellipsize and height on a
    single-line layout varies enough between Pango versions that it is not the
    right tool for something that has to hold unconditionally. A clip either
    holds or it does not, and it holds. */
    cairo_save(cairo);
    cairo_rectangle(cairo, (double)x, 0.0, (double)room, (double)height);
    cairo_clip(cairo);

    /* [COMMENT] Action purpose: Centre the text vertically. The division is
    done in floating point because cairo_move_to takes doubles -- integer
    division here would quantise the baseline and shift text off-centre by up
    to a pixel on odd height differences. */
    cairo_move_to(cairo, (double)x, (double)(height - h) / 2.0);
    pango_cairo_update_layout(cairo, layout);
    pango_cairo_show_layout(cairo, layout);

    cairo_restore(cairo);
  }

  cairo_surface_flush(surface);

  unsigned char *data = cairo_image_surface_get_data(surface);
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pixel_width);

  struct wlr_buffer *buffer =
      hikari_buffer_create_argb8888(pixel_width, pixel_height, data, stride);

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
          wlr_scene_buffer_create(hikari_server.layers.top, buffer);
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
      /* [COMMENT] Action purpose: Raise only once, at creation, and now only
      within the top layer -- the bar sits above any layer-shell TOP surface on
      the same output. This can no longer reach the lock indicator or the
      indicator bars, which live in higher layers of their own; before the
      scene was split into layers this raise competed with theirs on a shared
      root, which is why it had to be rationed to creation time. */
      if (newly_created) {
        wlr_scene_node_raise_to_top(&bar->scene_buffer->node);
      }
      /* [COMMENT] Action purpose: `!obscured`, not an unconditional true. A
      repaint triggered by new telemetry must not put the bar back on screen
      over a fullscreen video -- the helper keeps ticking either way, so an
      unconditional enable here would make the bar reappear within 200ms of
      being hidden. */
      wlr_scene_node_set_enabled(&bar->scene_buffer->node, !bar->obscured);
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
