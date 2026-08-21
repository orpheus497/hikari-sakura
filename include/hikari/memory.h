/* [COMMENT] Script function and purpose: Memory allocation wrapper declarations.
hikari_malloc and hikari_calloc are fail-fast -- they never return NULL, aborting
with a diagnostic on allocation failure -- and remain the default for state whose
loss would leave the compositor internally inconsistent. hikari_try_malloc is the
opt-in graceful-degradation counterpart for allocations in hot, high-churn paths
(subsurface/popup tracking, rendered UI buffers) where skipping one optional piece
of bookkeeping under memory pressure is preferable to losing the whole compositor;
see DECISIONS_LOG Finding 4 for which call sites use which policy and why. */

#if !defined(HIKARI_MEMORY_H)
#define HIKARI_MEMORY_H

#include <stdlib.h>

/* Never returns NULL; aborts with a diagnostic on allocation failure. */
void *
hikari_malloc(size_t size);

/* Never returns NULL; aborts with a diagnostic on allocation failure. */
void *
hikari_calloc(size_t number, size_t size);

/* Returns NULL on allocation failure instead of aborting, logging an
error-level diagnostic first. Callers MUST check the return value and degrade
gracefully (e.g. skip creating the optional tracking object, or fall back to
a simpler render path) rather than assuming success. */
void *
hikari_try_malloc(size_t size);

/* free(3) semantics; NULL is accepted and ignored. */
void
hikari_free(void *ptr);

#endif
