/* [COMMENT] Script function and purpose: Fail-fast memory allocation wrapper declarations; hikari_malloc and hikari_calloc never return NULL (they abort with a diagnostic on allocation failure), so callers may assume success. */

#if !defined(HIKARI_MEMORY_H)
#define HIKARI_MEMORY_H

#include <stdlib.h>

/* Never returns NULL; aborts with a diagnostic on allocation failure. */
void *
hikari_malloc(size_t size);

/* Never returns NULL; aborts with a diagnostic on allocation failure. */
void *
hikari_calloc(size_t number, size_t size);

/* free(3) semantics; NULL is accepted and ignored. */
void
hikari_free(void *ptr);

#endif
