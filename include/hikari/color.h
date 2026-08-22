#if !defined(HIKARI_COLOR_H)
#define HIKARI_COLOR_H

#include <stdint.h>

/* [COMMENT] Function purpose: Convert a 0xRRGGBB value to normalised RGBA,
fully opaque. This is the historical form and remains the meaning of an integer
colour in the configuration, so every existing config keeps its exact
appearance. Alpha is expressed with the string form instead -- see
hikari_color_convert_rgba() and parse_color() in src/configuration.c, and
DECISIONS_LOG Phase 60 for why integers could not carry alpha unambiguously. */
static inline void
hikari_color_convert(float dst[static 4], uint32_t color)
{
  dst[0] = ((color >> 16) & 0xff) / 255.0;
  dst[1] = ((color >> 8) & 0xff) / 255.0;
  dst[2] = (color & 0xff) / 255.0;
  dst[3] = 1.0;
}

/* [COMMENT] Function purpose: Convert a 0xRRGGBBAA value to normalised RGBA,
honouring the low byte as alpha. Used for the "#RRGGBBAA" configuration form. */
static inline void
hikari_color_convert_rgba(float dst[static 4], uint32_t color)
{
  dst[0] = ((color >> 24) & 0xff) / 255.0;
  dst[1] = ((color >> 16) & 0xff) / 255.0;
  dst[2] = ((color >> 8) & 0xff) / 255.0;
  dst[3] = (color & 0xff) / 255.0;
}

/* [COMMENT] Function purpose: Convert straight RGBA to premultiplied, as
required by wlr_scene_rect_create()/wlr_scene_rect_set_color(). Config colours
are stored straight because Cairo expects that; the two only agree at alpha
1.0. Use at every wlr_scene_rect_* site, never at a Cairo one. */
static inline void
hikari_color_premultiply(float dst[static 4], const float src[static 4])
{
  dst[0] = src[0] * src[3];
  dst[1] = src[1] * src[3];
  dst[2] = src[2] * src[3];
  dst[3] = src[3];
}

#endif
