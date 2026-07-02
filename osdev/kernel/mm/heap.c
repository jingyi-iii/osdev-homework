#include "mm/heap.h"
#include "sync/spinlock.h"
#include "lib/module.h"
#include "lib/string.h"
#include "lib/list.h"

#define HEAP_TOTAL_SIZE         (1024 * 1024 * 10)

typedef struct heapchunk {
    unsigned int    size;
    int             used;
    list_node       this_node;
} heapchunk;

typedef struct heappool {
    int8_t      pool[HEAP_TOTAL_SIZE];
    uint32_t    avail_size;
    spinlock*   lock_dev;
    int8_t      init;
    list_node   head_node;
} heappool;

static heappool pool = {0};

static void kheap_init(void)
{
    heapchunk* chunk = (heapchunk*)pool.pool;

    pool.lock_dev = spinlock_alloc();
    if (!pool.lock_dev)
        return;

    spinlock_lock(pool.lock_dev);
    pool.avail_size = HEAP_TOTAL_SIZE;
    pool.init = 1;
    pool.head_node.prev = &pool.head_node;
    pool.head_node.next = &pool.head_node;

    chunk->size = HEAP_TOTAL_SIZE - sizeof(chunk);
    chunk->used = 0;
    list_add(&chunk->this_node, &pool.head_node);
    spinlock_unlock(pool.lock_dev);
}

void* kmalloc(unsigned int alloc_size)
{
    heapchunk* pck = 0;
    heapchunk* new_pck = 0;
    void* ret_addr = 0;
    unsigned int req_size = sizeof(heapchunk) + alloc_size;

    if (!pool.init)
        kheap_init();

    spinlock_lock(pool.lock_dev);
    if (req_size < alloc_size)         // overflow
        goto ALLOC_FAIL;
    if (req_size > pool.avail_size)
        goto ALLOC_FAIL;
    if (!req_size)
        goto ALLOC_FAIL;

    list_for_each(node, &pool.head_node) {
        heapchunk* p = list_entry(node, heapchunk, this_node);
        if (p->size >= req_size && !p->used) {
            pck = p;
            break;
        }
    }

    if (!pck)
        goto ALLOC_FAIL;

    new_pck = (heapchunk*)((uint8_t*)pck + req_size);
    if ((uint8_t*)new_pck + sizeof(heapchunk) <= (uint8_t*)pool.pool + HEAP_TOTAL_SIZE) {
        new_pck->size = pck->size - req_size;
        new_pck->used = 0;
        list_add(&new_pck->this_node, &pck->this_node);
    }

    ret_addr = (uint8_t*)pck + sizeof(heapchunk);
    memset(ret_addr, 0, alloc_size);
    pck->used = 1;
    pck->size = alloc_size;

    pool.avail_size -= req_size;
    spinlock_unlock(pool.lock_dev);

    return (void*)ret_addr;

ALLOC_FAIL:
    spinlock_unlock(pool.lock_dev);
    return 0;
}

void kfree(void* pointer)
{
    heapchunk* pck = 0;
    heapchunk* free_pck = 0;

    if (!pointer)
        return;

    spinlock_lock(pool.lock_dev);
    free_pck = (heapchunk *)((uint8_t*)pointer - sizeof(heapchunk));

    if (!free_pck->used) {
        spinlock_unlock(pool.lock_dev);
        return;
    }

    // merge with next chunk
    if (list_next(&free_pck->this_node) != &pool.head_node) {
        pck = list_entry(list_next(&free_pck->this_node), heapchunk, this_node);
        if (!pck->used) {
            free_pck->size += sizeof(heapchunk) + pck->size;
            list_del(&pck->this_node);
        }
    }

    // merge with prev chunk
    if (list_prev(&free_pck->this_node) != &pool.head_node) {
        pck = list_entry(list_prev(&free_pck->this_node), heapchunk, this_node);
        if (!pck->used) {
            pck->size += sizeof(heapchunk) + free_pck->size;
            list_del(&free_pck->this_node);
            free_pck = pck;
        }
    }

    pool.avail_size += sizeof(heapchunk) + free_pck->size;
    free_pck->used = 0;
    spinlock_unlock(pool.lock_dev);
}
