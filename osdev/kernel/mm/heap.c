#include "mm/heap.h"
#include "sync/spinlock.h"
#include "lib/string.h"
#include "lib/list.h"

#define HEAP_TOTAL_SIZE         (1024 * 1024 * 10)
#define HEAP_ALIGN              8

typedef struct heapchunk {
    unsigned int    size;
    int             used;
    list_node       this_node;
} heapchunk;

/* Header size rounded up so chunk payloads stay HEAP_ALIGN-aligned
 * (chunk bases are HEAP_ALIGN multiples and all sizes are rounded). */
#define HEAP_HDR_SIZE   ((sizeof(heapchunk) + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1))

typedef struct heappool {
    i8      pool[HEAP_TOTAL_SIZE];
    u32    avail_size;
    spinlock*   lock_dev;
    i8      init;
    list_node   head_node;
} heappool;

static heappool pool __attribute__((aligned(HEAP_ALIGN))) = {0};

static void kheap_init(void)
{
    heapchunk* chunk = (heapchunk*)pool.pool;

    pool.lock_dev = spinlock_alloc();
    if (!pool.lock_dev)
        return;

    u32 eflags = spinlock_lock_irqsave(pool.lock_dev);
    pool.avail_size = HEAP_TOTAL_SIZE - HEAP_HDR_SIZE;
    pool.init = 1;
    pool.head_node.prev = &pool.head_node;
    pool.head_node.next = &pool.head_node;

    chunk->size = HEAP_TOTAL_SIZE - HEAP_HDR_SIZE;
    chunk->used = 0;
    list_add(&chunk->this_node, &pool.head_node);
    spinlock_unlock_irqrestore(pool.lock_dev, eflags);
}

void* kmalloc(unsigned int alloc_size)
{
    heapchunk* chunk = 0;
    heapchunk* new_chunk = 0;
    void* ret_addr = 0;
    unsigned int aligned_size;
    unsigned int req_size;

    if (!pool.init)
        kheap_init();

    /* Round the request up so every payload is HEAP_ALIGN-aligned
     * (chunk bases are HEAP_ALIGN multiples and so is the header). */
    if (alloc_size > (0xFFFFFFFFu - (HEAP_ALIGN - 1)))   /* align overflow */
        return 0;
    aligned_size = (alloc_size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
    req_size = HEAP_HDR_SIZE + aligned_size;

    u32 eflags = spinlock_lock_irqsave(pool.lock_dev);
    if (req_size > pool.avail_size)
        goto ALLOC_FAIL;

    list_for_each(node, &pool.head_node) {
        heapchunk* p = list_entry(node, heapchunk, this_node);
        if (p->size >= req_size && !p->used) {
            chunk = p;
            break;
        }
    }

    if (!chunk)
        goto ALLOC_FAIL;

    new_chunk = (heapchunk*)((u8*)chunk + req_size);
    if ((u8*)new_chunk + HEAP_HDR_SIZE <= (u8*)pool.pool + HEAP_TOTAL_SIZE) {
        new_chunk->size = chunk->size - req_size;
        new_chunk->used = 0;
        list_add(&new_chunk->this_node, &chunk->this_node);
    }

    ret_addr = (u8*)chunk + HEAP_HDR_SIZE;
    memset(ret_addr, 0, alloc_size);
    chunk->used = 1;
    chunk->size = aligned_size;

    pool.avail_size -= req_size;
    spinlock_unlock_irqrestore(pool.lock_dev, eflags);

    return (void*)ret_addr;

ALLOC_FAIL:
    spinlock_unlock_irqrestore(pool.lock_dev, eflags);
    return 0;
}

void kfree(void* pointer)
{
    heapchunk* chunk = 0;
    heapchunk* free_chunk = 0;

    if (!pointer)
        return;

    u32 eflags = spinlock_lock_irqsave(pool.lock_dev);
    free_chunk = (heapchunk *)((u8*)pointer - HEAP_HDR_SIZE);

    if (!free_chunk->used) {
        spinlock_unlock_irqrestore(pool.lock_dev, eflags);
        return;
    }

    // merge with next chunk
    if (list_next(&free_chunk->this_node) != &pool.head_node) {
        chunk = list_entry(list_next(&free_chunk->this_node), heapchunk, this_node);
        if (!chunk->used) {
            free_chunk->size += HEAP_HDR_SIZE + chunk->size;
            list_del(&chunk->this_node);
        }
    }

    // merge with prev chunk
    if (list_prev(&free_chunk->this_node) != &pool.head_node) {
        chunk = list_entry(list_prev(&free_chunk->this_node), heapchunk, this_node);
        if (!chunk->used) {
            chunk->size += HEAP_HDR_SIZE + free_chunk->size;
            list_del(&free_chunk->this_node);
            free_chunk = chunk;
        }
    }

    pool.avail_size += HEAP_HDR_SIZE + free_chunk->size;
    free_chunk->used = 0;
    spinlock_unlock_irqrestore(pool.lock_dev, eflags);
}
