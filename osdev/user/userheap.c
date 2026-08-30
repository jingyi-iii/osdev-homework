/*
 * user/userheap.c — user-mode heap for the DRIVER_CLASS_USER servers.
 *
 * The user-mode servers (terminal_server, kb_server, ...) run as ring-3
 * processes but are compiled into the kernel image.  They previously
 * called the kernel heap (kmalloc/kfree) directly, reaching kernel heap
 * structures from ring-3 with no isolation.  This module gives them
 * their own fixed pool via malloc()/free().
 *
 * The pool is a static array in the kernel image (low identity map,
 * PTE_USER), so it is directly accessible from the ring-3 server
 * processes.  The allocator is modelled on kernel/mm/heap.c, using the
 * FIXED accounting:
 *
 *   - avail_size tracks the total free bytes (header + payload of every
 *     free chunk), initialised to the whole pool size;
 *   - kmalloc/malloc subtracts req_size (new chunk's header+payload);
 *   - kfree/free accounts the freed chunk's header+payload BEFORE the
 *     merge pass — free neighbours were already counted, so adding them
 *     again would double-count and let the allocator hand out memory
 *     past the end of the pool.
 *
 * The lock is embedded in the pool (no kernel spinlock slot needed);
 * spinlock_lock_irqsave() degrades gracefully at CPL3 (see
 * kernel/sync/spinlock.c).
 */

#include "user/userheap.h"
#include "sync/spinlock.h"
#include "lib/string.h"
#include "lib/list.h"

#define USER_HEAP_TOTAL_SIZE     (1024 * 1024)
#define USER_HEAP_ALIGN          8

typedef struct userheap_chunk {
    unsigned int    size;
    int             used;
    list_node       this_node;
} userheap_chunk;

/* Header size rounded up so chunk payloads stay USER_HEAP_ALIGN-aligned. */
#define USER_HEAP_HDR_SIZE ((sizeof(userheap_chunk) + USER_HEAP_ALIGN - 1) & ~(USER_HEAP_ALIGN - 1))

typedef struct userheap_pool {
    i8          pool[USER_HEAP_TOTAL_SIZE];
    u32         avail_size;
    spinlock    lock;               /* embedded — no kernel spinlock slot */
    i8          init;
    list_node   head_node;
} userheap_pool;

static userheap_pool pool __attribute__((aligned(USER_HEAP_ALIGN))) = {
    .lock = { .state = LOCK_UNLOCKED },
};

static void userheap_init(void)
{
    userheap_chunk* chunk = (userheap_chunk*)pool.pool;

    u32 eflags = spinlock_lock_irqsave(&pool.lock);
    if (!pool.init) {
        pool.avail_size = USER_HEAP_TOTAL_SIZE;
        pool.init = 1;
        pool.head_node.prev = &pool.head_node;
        pool.head_node.next = &pool.head_node;

        chunk->size = USER_HEAP_TOTAL_SIZE - USER_HEAP_HDR_SIZE;
        chunk->used = 0;
        list_add(&chunk->this_node, &pool.head_node);
    }
    spinlock_unlock_irqrestore(&pool.lock, eflags);
}

void* malloc(unsigned int alloc_size)
{
    userheap_chunk* chunk = 0;
    userheap_chunk* new_chunk = 0;
    void* ret_addr = 0;
    unsigned int aligned_size;
    unsigned int req_size;

    if (!pool.init)
        userheap_init();

    /* Reject requests that cannot fit the pool: also prevents u32 wrap
     * of req_size for absurd sizes, which would bypass the avail_size
     * check and corrupt the heap (same guard as kmalloc). */
    if (alloc_size > USER_HEAP_TOTAL_SIZE)
        return 0;
    if (alloc_size > (0xFFFFFFFFu - (USER_HEAP_ALIGN - 1)))   /* align overflow */
        return 0;
    aligned_size = (alloc_size + USER_HEAP_ALIGN - 1) & ~(USER_HEAP_ALIGN - 1);
    req_size = USER_HEAP_HDR_SIZE + aligned_size;

    u32 eflags = spinlock_lock_irqsave(&pool.lock);
    if (req_size > pool.avail_size)
        goto ALLOC_FAIL;

    list_for_each(node, &pool.head_node) {
        userheap_chunk* p = list_entry(node, userheap_chunk, this_node);
        if (p->size >= req_size && !p->used) {
            chunk = p;
            break;
        }
    }

    if (!chunk)
        goto ALLOC_FAIL;

    new_chunk = (userheap_chunk*)((u8*)chunk + req_size);
    if ((u8*)new_chunk + USER_HEAP_HDR_SIZE <= (u8*)pool.pool + USER_HEAP_TOTAL_SIZE) {
        new_chunk->size = chunk->size - req_size;
        new_chunk->used = 0;
        list_add(&new_chunk->this_node, &chunk->this_node);
    }

    ret_addr = (u8*)chunk + USER_HEAP_HDR_SIZE;
    memset(ret_addr, 0, alloc_size);
    chunk->used = 1;
    chunk->size = aligned_size;

    pool.avail_size -= req_size;
    spinlock_unlock_irqrestore(&pool.lock, eflags);

    return (void*)ret_addr;

ALLOC_FAIL:
    spinlock_unlock_irqrestore(&pool.lock, eflags);
    return 0;
}

void free(void* pointer)
{
    userheap_chunk* chunk = 0;
    userheap_chunk* free_chunk = 0;

    if (!pointer)
        return;

    u32 eflags = spinlock_lock_irqsave(&pool.lock);
    free_chunk = (userheap_chunk*)((u8*)pointer - USER_HEAP_HDR_SIZE);

    if (!free_chunk->used) {
        spinlock_unlock_irqrestore(&pool.lock, eflags);
        return;
    }

    /* Account the newly freed space BEFORE merging: free neighbours are
     * already counted in avail_size, so merging below must only
     * reorganise the chunk list (see the note at the top of this file). */
    pool.avail_size += USER_HEAP_HDR_SIZE + free_chunk->size;

    /* merge with next chunk */
    if (list_next(&free_chunk->this_node) != &pool.head_node) {
        chunk = list_entry(list_next(&free_chunk->this_node), userheap_chunk, this_node);
        if (!chunk->used) {
            free_chunk->size += USER_HEAP_HDR_SIZE + chunk->size;
            list_del(&chunk->this_node);
        }
    }

    /* merge with prev chunk */
    if (list_prev(&free_chunk->this_node) != &pool.head_node) {
        chunk = list_entry(list_prev(&free_chunk->this_node), userheap_chunk, this_node);
        if (!chunk->used) {
            chunk->size += USER_HEAP_HDR_SIZE + free_chunk->size;
            list_del(&free_chunk->this_node);
            free_chunk = chunk;
        }
    }

    free_chunk->used = 0;
    spinlock_unlock_irqrestore(&pool.lock, eflags);
}
