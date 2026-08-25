// [COMMENT] Script function and purpose: Defaults and register resolution for
// the automatic layout policy.

#include <hikari/layout_policy.h>

#include <assert.h>

#include <hikari/configuration.h>
#include <hikari/sheet.h>
#include <hikari/split.h>

// [COMMENT] Function purpose: Establish defaults that reproduce hikari's
// historical behaviour exactly, so an existing configuration that says nothing
// about layout policy behaves as it always did.
void
hikari_layout_policy_init(struct hikari_layout_policy *policy)
{
  assert(policy != NULL);

  policy->automatic = false;
  policy->on_close = true;
  policy->insert = HIKARI_LAYOUT_INSERT_APPEND;
  policy->default_register = '\0';
}

struct hikari_split *
hikari_layout_policy_split(struct hikari_layout_policy *policy,
    struct hikari_configuration *configuration,
    struct hikari_sheet *sheet)
{
  assert(policy != NULL);
  assert(configuration != NULL);
  assert(sheet != NULL);

  /* [COMMENT] Action purpose: An explicit `default-register` wins, but only
  when it actually resolves. A register naming a layout the user later deleted
  falls through to the per-sheet default rather than silently disabling the
  whole feature -- the failure a typo would otherwise produce is "automatic
  tiling stopped working" with nothing logged at the point of use. */
  if (policy->default_register != '\0') {
    struct hikari_split *split = hikari_configuration_lookup_layout(
        configuration, policy->default_register);

    if (split != NULL) {
      return split;
    }
  }

  return hikari_sheet_default_split(sheet);
}
