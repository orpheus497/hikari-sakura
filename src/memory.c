// [COMMENT] Script function and purpose: Memory allocation wrappers for hikari.
// hikari_malloc/hikari_calloc are fail-fast: allocation failure there is
// unrecoverable state loss, so they diagnose and abort instead of propagating
// NULL to unchecked callsites. hikari_try_malloc is the opt-in
// graceful-degradation counterpart for hot paths that can safely skip one
// optional allocation under memory pressure instead of losing the whole
// compositor -- see DECISIONS_LOG Finding 4.

#include <hikari/memory.h>

#include <stdlib.h>

#include <wlr/util/log.h>

// [COMMENT] Function purpose: Allocate `size` bytes; never returns NULL. On failure
// logs a diagnostic naming the request size and aborts, surfacing OOM at the true
// failure point instead of as a later NULL dereference.
void *
hikari_malloc(size_t size)
{
  void *ptr = malloc(size);

  // [COMMENT] Action purpose: Enforce the fail-fast allocation policy; abort (not
  // exit) to produce SIGABRT/core dump and skip atexit handlers on a half-valid heap.
  if (ptr == NULL) {
    wlr_log(WLR_ERROR, "hikari_malloc of %zu bytes failed", size);
    abort();
  }

  return ptr;
}

// [COMMENT] Function purpose: Allocate zeroed storage for `number` elements of
// `size` bytes; never returns NULL. Aborts with a diagnostic on failure, matching
// hikari_malloc.
void *
hikari_calloc(size_t number, size_t size)
{
  void *ptr = calloc(number, size);

  // [COMMENT] Action purpose: Enforce the fail-fast allocation policy; abort (not
  // exit) to produce SIGABRT/core dump and skip atexit handlers on a half-valid heap.
  if (ptr == NULL) {
    wlr_log(WLR_ERROR, "hikari_calloc of %zu x %zu bytes failed", number, size);
    abort();
  }

  return ptr;
}

// [COMMENT] Function purpose: Allocate `size` bytes for a hot-path caller that
// can gracefully skip its own work on failure instead of losing the whole
// compositor. Logs a warning and returns NULL rather than aborting; the
// caller is responsible for checking and degrading.
void *
hikari_try_malloc(size_t size)
{
  void *ptr = malloc(size);

  if (ptr == NULL) {
    wlr_log(WLR_ERROR,
        "hikari_try_malloc of %zu bytes failed -- degrading gracefully",
        size);
  }

  return ptr;
}

// [COMMENT] Function purpose: Release storage obtained from hikari_malloc or
// hikari_calloc; like free(3), NULL is accepted and ignored.
void
hikari_free(void *ptr)
{
  free(ptr);
}
