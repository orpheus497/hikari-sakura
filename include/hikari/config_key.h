/* [COMMENT] Script function and purpose: Diagnose a configuration key that was
given more than one value.

WHY THIS HAS TO EXIST. libucl neither merges duplicate keys nor rejects them: it
chains the values off the first one, and ucl_object_iterate_safe() then yields
only the head of that chain. Its `expand_values` argument does not change this --
that flag governs arrays proper, not the implicit chain a repeated key builds --
so EVERY parser in this tree sees the first value for a duplicated key and is
structurally incapable of seeing the rest, whatever it does with the object.

The result was silence. A key written twice -- two sections of a config that both
claim `L+n`, say -- left the second binding doing nothing at all, with no
diagnostic from libucl, from hikari, or from the reload. Nothing distinguished it
from a binding that was simply broken, which is the shape of bug that never gets
reported because it never gets attributed.

WHY IT WARNS RATHER THAN REJECTS. Every other configuration mistake in this tree
is fatal -- an unknown key, an unknown action, an out-of-range value. A duplicate
is deliberately not, because unlike those it produces a configuration that is
still coherent and still runs, and because a config that has quietly carried a
duplicate for months would otherwise stop the desktop from starting on the next
upgrade. Loud is the fix here; fatal would be a regression. */

#if !defined(HIKARI_CONFIG_KEY_H)
#define HIKARI_CONFIG_KEY_H

#include <stdbool.h>
#include <stdio.h>

#include <ucl.h>

/* [COMMENT] Function purpose: Whether this value is a scalar whose text is worth
showing the user.

ucl_object_tostring_forced() renders EVERY type, including the containers -- it
answers "object" for an object and "array" for an array, which is a description
of the type rather than of the value and is worse than printing nothing. The
scalars below all render as what the user actually typed. UCL_USERDATA is
excluded with the containers: nothing in a configuration file can produce one, so
whatever it would render is not something the user wrote. */
static inline bool
config_key_is_renderable(const ucl_object_t *obj)
{
  switch (ucl_object_type(obj)) {
    case UCL_STRING:
    case UCL_INT:
    case UCL_FLOAT:
    case UCL_BOOLEAN:
    case UCL_TIME:
    case UCL_NULL:
      return true;

    default:
      return false;
  }
}

/* [COMMENT] Function purpose: Report `obj`'s key if it carries a duplicate
chain, naming what took effect and what was discarded.

`context` names the enclosing block for the message and should read as a
location -- "bindings.keyboard", "ui.palette", "an outputs entry".

A no-op on anything that is not a duplicated object key, which specifically
includes ARRAY ELEMENTS: those carry neither a key nor a chain, and both are
tested, so this is safe to call from any iteration without the caller having to
know which kind it is walking. */
static inline void
hikari_config_warn_duplicate_key(
    const ucl_object_t *obj, const char *context)
{
  if (obj == NULL || obj->next == NULL) {
    return;
  }

  const char *key = ucl_object_key(obj);

  if (key == NULL) {
    return;
  }

  int count = 1;
  for (const ucl_object_t *dup = obj->next; dup != NULL; dup = dup->next) {
    count++;
  }

  fprintf(stderr,
      "configuration warning: \"%s\" is set %d times in %s -- hikari uses the "
      "first and ignores the rest\n",
      key,
      count,
      context);

  /* [COMMENT] Action purpose: Print the values as well as the count, because
  the count alone does not say WHICH of the two the user is now running.

  Gated on the TYPE, not on the return value. An earlier version of this claimed
  ucl_object_tostring_forced() yields NULL for an object or an array, and that is
  simply untrue -- verified against the libucl this builds on (0.9.4), it returns
  the literal strings "object" and "array". So a duplicated nested block printed

      in effect: object
      ignored:   object

  which tells the reader nothing and reads like a fault in hikari rather than in
  their configuration. A nested block is identified well enough by the key in the
  line above, so the values are simply omitted for those. */
  if (config_key_is_renderable(obj)) {
    fprintf(stderr, "    in effect: %s\n", ucl_object_tostring_forced(obj));
  }

  for (const ucl_object_t *dup = obj->next; dup != NULL; dup = dup->next) {
    if (config_key_is_renderable(dup)) {
      fprintf(stderr, "    ignored:   %s\n", ucl_object_tostring_forced(dup));
    }
  }
}

#endif
