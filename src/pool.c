##Script function and purpose: Implementation of the contiguous object pool allocator for DOD architectures.
#include <hikari/pool.h>
#include <hikari/memory.h>
#include <assert.h>

##Function purpose: Initializes a memory pool with specified capacity and item size.
void
hikari_pool_init(struct hikari_pool *pool, size_t capacity, size_t item_size)
{
  ##Step purpose: Ensure capacity and item_size are valid, and alignment is sufficient.
  assert(capacity > 0);
  assert(item_size >= sizeof(void *));

  pool->capacity = capacity;
  pool->item_size = item_size;
  pool->count = 0;
  
  ##Step purpose: Allocate the contiguous memory slab.
  pool->buffer = hikari_malloc(capacity * item_size);
  pool->free_list = pool->buffer;

  ##Step purpose: Initialize the intrusive free list within the buffer.
  char *ptr = (char *)pool->buffer;
  ##Loop purpose: Wire up the free list pointers block by block.
  for (size_t i = 0; i < capacity - 1; i++) {
    void **current = (void **)(ptr + (i * item_size));
    *current = (void *)(ptr + ((i + 1) * item_size));
  }
  ##Step purpose: Terminate the last item in the free list.
  void **last = (void **)(ptr + ((capacity - 1) * item_size));
  *last = NULL;
}

##Function purpose: Allocates a single item from the pool's free list in O(1) time.
void *
hikari_pool_alloc(struct hikari_pool *pool)
{
  ##Condition purpose: Ensure the pool is not exhausted before allocating.
  if (pool->free_list == NULL) {
    return NULL;
  }

  ##Step purpose: Pop the head of the free list.
  void *item = pool->free_list;
  pool->free_list = *(void **)item;
  pool->count++;

  return item;
}

##Function purpose: Returns an item back to the pool's free list.
void
hikari_pool_free(struct hikari_pool *pool, void *ptr)
{
  ##Condition purpose: Prevent freeing a null pointer.
  if (ptr == NULL) {
    return;
  }

  ##Step purpose: Push the item back onto the head of the free list.
  *(void **)ptr = pool->free_list;
  pool->free_list = ptr;
  pool->count--;
}

##Function purpose: Destroys the pool and frees the contiguous memory buffer.
void
hikari_pool_destroy(struct hikari_pool *pool)
{
  ##Condition purpose: Prevent freeing an already destroyed or uninitialized pool buffer.
  if (pool->buffer != NULL) {
    hikari_free(pool->buffer);
    pool->buffer = NULL;
    pool->free_list = NULL;
    pool->capacity = 0;
    pool->count = 0;
  }
}
