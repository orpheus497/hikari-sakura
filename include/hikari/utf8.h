#if !defined(HIKARI_UTF8_H)
#define HIKARI_UTF8_H

#include <stddef.h>
#include <stdint.h>

static inline size_t
utf8_chsize(uint32_t codepoint)
{
  if (codepoint < 0x80) {
    return 1;
  } else if (codepoint < 0x800) {
    return 2;
  } else if (codepoint < 0x10000) {
    return 3;
  }
  return 4;
}

static inline void
utf8_encode(char *buffer, size_t length, uint32_t codepoint)
{
  uint8_t first;

  if (codepoint < 0x80) {
    first = 0;
  } else if (codepoint < 0x800) {
    first = 0xc0;
  } else if (codepoint < 0x10000) {
    first = 0xe0;
  } else {
    first = 0xf0;
  }

  for (size_t i = length - 1; i > 0; --i) {
    buffer[i] = (codepoint & 0x3f) | 0x80;
    codepoint >>= 6;
  }

  buffer[0] = codepoint | first;
}

/* [COMMENT] Function purpose: Decode one UTF-8 sequence at `s`, the inverse of
utf8_encode() above, rejecting everything a strict decoder must reject: a bad
lead byte, a sequence truncated by `len`, a bad continuation byte, an overlong
encoding, a UTF-16 surrogate and anything above U+10FFFF.

Returns the number of bytes consumed, or 0 when the sequence is not well
formed, so that one call both validates and advances. That is what lets a
caller walk a string of unknown provenance a character at a time -- and stop or
truncate -- without ever splitting a character, which is the only safe way to
bound text that is about to be handed to Pango. */
static inline size_t
utf8_decode(const char *s, size_t len, uint32_t *codepoint)
{
  static const uint32_t minimum[5] = { 0, 0, 0x80, 0x800, 0x10000 };

  const unsigned char *bytes = (const unsigned char *)s;
  size_t length;
  uint32_t value;

  if (len == 0) {
    return 0;
  }

  if (bytes[0] < 0x80) {
    length = 1;
    value = bytes[0];
  } else if ((bytes[0] & 0xe0) == 0xc0) {
    length = 2;
    value = bytes[0] & 0x1f;
  } else if ((bytes[0] & 0xf0) == 0xe0) {
    length = 3;
    value = bytes[0] & 0x0f;
  } else if ((bytes[0] & 0xf8) == 0xf0) {
    length = 4;
    value = bytes[0] & 0x07;
  } else {
    return 0;
  }

  if (length > len) {
    return 0;
  }

  for (size_t i = 1; i < length; ++i) {
    if ((bytes[i] & 0xc0) != 0x80) {
      return 0;
    }
    value = (value << 6) | (bytes[i] & 0x3f);
  }

  if (value < minimum[length] || value > 0x10ffff ||
      (value >= 0xd800 && value <= 0xdfff)) {
    return 0;
  }

  *codepoint = value;

  return length;
}

#endif
