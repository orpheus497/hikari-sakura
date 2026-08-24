/**
 * @file bar_config.h
 * @brief Top bar appearance limits -- the `ui { bar { ... } }` block.
 *
 * Separate from the `bar` colour in struct hikari_configuration, which predates
 * this and stays where it is.
 *
 * These live compositor-side rather than in the hikari-topbar helper on
 * purpose. The helper is exec'd exactly once from server_init(), with no argv
 * and no environment of its own, and there is no path that restarts it -- so a
 * limit configured in the helper could never be changed by a config reload.
 * Consumed in src/bar.c, which already owns layout and already links Pango.
 */

#if !defined(HIKARI_BAR_CONFIG_H)
#define HIKARI_BAR_CONFIG_H

struct hikari_bar_config {
  /* [COMMENT] Class purpose: Longest block rendered without scrolling, counted
  in CODEPOINTS rather than bytes -- a track title is routinely non-ASCII and a
  byte limit would cut a multi-byte sequence in half. 0 disables capping
  entirely, restoring the pre-Phase-90 behaviour for anyone who wants it. */
  int max_block_chars;

  /* [COMMENT] Class purpose: Milliseconds per one-codepoint step of the banner
  scroll. The timer is only armed while some block is actually over the cap, so
  this costs nothing on a desktop with no media playing. */
  int scroll_interval;

  /* [COMMENT] Class purpose: Inserted between the end of an over-long block and
  its own beginning as the banner wraps, so the text reads as a continuous loop
  rather than snapping back. Owned; freed by hikari_bar_config_fini(). */
  char *scroll_separator;
};

void
hikari_bar_config_init(struct hikari_bar_config *bar_config);

void
hikari_bar_config_fini(struct hikari_bar_config *bar_config);

#endif
