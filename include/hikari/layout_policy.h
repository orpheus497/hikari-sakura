/* [COMMENT] Script function and purpose: The automatic layout policy -- whether
a sheet re-tiles itself when its set of views changes, and which layout it
reaches for when it has none yet.

This is deliberately a separate concern from `struct hikari_layout_config` (the
`layouts` section), which owns the layout REGISTERS. This owns the POLICY that
decides when a register is applied without the user asking. Merging the two
would put a boolean in a list keyed by layout register letter. */

#if !defined(HIKARI_LAYOUT_POLICY_H)
#define HIKARI_LAYOUT_POLICY_H

#include <stdbool.h>

struct hikari_configuration;
struct hikari_sheet;
struct hikari_split;

/* [COMMENT] Class purpose: Where an automatically incorporated view lands in
the layout order. The two members mirror the two existing manual actions --
`layout-restack-append` and `layout-restack-prepend` -- and the reflow path
dispatches to those same two functions, so automatic and manual insertion
cannot drift apart. */
enum hikari_layout_insert {
  HIKARI_LAYOUT_INSERT_APPEND,
  HIKARI_LAYOUT_INSERT_PREPEND
};

struct hikari_layout_policy {
  /* [COMMENT] Class purpose: The master switch. False reproduces hikari's
  historical behaviour exactly -- a new view is stacked on top and an existing
  layout is left alone until the user issues a tiling action. See the LAYOUTS
  section of hikari(1) for why that is the default. */
  bool automatic;

  /* [COMMENT] Class purpose: Whether closing a view also re-tiles. Separate
  from `automatic` because the two are genuinely different preferences: folding
  a new window in is additive and predictable, whereas closing one moves every
  surviving window, which some users would rather do by hand. */
  bool on_close;

  enum hikari_layout_insert insert;

  /* [COMMENT] Class purpose: Layout register used when a sheet has no layout at
  all. '\0' means unset, in which case the reflow falls back to the per-sheet
  default register ('0' + sheet number) that hikari_sheet_default_split() has
  always used -- so configuring nothing here changes nothing. */
  char default_register;
};

/* Function purpose: Establish the historical no-op defaults. */
void
hikari_layout_policy_init(struct hikari_layout_policy *policy);

/* [COMMENT] Function purpose: Resolve the split a sheet with no layout should
adopt, or NULL if the user has configured none -- in which case the sheet is
left stacking, which is the correct outcome rather than a failure. */
struct hikari_split *
hikari_layout_policy_split(struct hikari_layout_policy *policy,
    struct hikari_configuration *configuration,
    struct hikari_sheet *sheet);

#endif
