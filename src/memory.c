// [COMMENT] Script function and purpose: Fail-fast memory allocation wrappers for hikari; allocation failure is unrecoverable for the compositor, so the wrappers diagnose and abort instead of propagating NULL to unchecked callsites.

#include <hikari/memory.h>

#include <stdio.h>
#include <stdlib.h>

// [COMMENT] Function purpose: Allocate `size` bytes; never returns NULL. On failure
// prints a diagnostic naming the request size and aborts, surfacing OOM at the true
// failure point instead of as a later NULL dereference.
void *
hikari_malloc(size_t size)
{
  void *ptr = malloc(size);

  // [COMMENT] Action purpose: Enforce the fail-fast allocation policy; abort (not
  // exit) to produce SIGABRT/core dump and skip atexit handlers on a half-valid heap.
  if (ptr == NULL) {
    fprintf(stderr, "error: hikari_malloc of %zu bytes failed\n", size);
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
    fprintf(
        stderr, "error: hikari_calloc of %zu x %zu bytes failed\n", number, size);
    abort();
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
