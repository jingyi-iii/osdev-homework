#include "heap.h"
#include "lock.h"
#include "module.h"

#define HEAP_TOTAL_SIZE         (1024 * 1024 * 10)

typedef struct heapchunk {
        struct heapchunk* prev;
        struct heapchunk* next;
        unsigned int size;
} heapchunk_t;

typedef struct heappool {
    int8_t pool[HEAP_TOTAL_SIZE];
    heapchunk_t ckstart;
    heapchunk_t ckend;
    uint32_t avail_size;
    spinlock_t lock;
    int8_t init;
} heappool_t;

static heappool_t heappool = {0};

static inline void memset(void *dest, uint8_t data, uint32_t size)
{
    uint32_t i = 0;

    for (i = 0; i < size; i++)
        *((uint8_t *)dest + i) = data;
}

static void kheap_init(void)
{
    spinlock_init(&heappool.lock);

    spinlock_lock(&heappool.lock);
    heappool.ckstart.next = (heapchunk_t *)heappool.pool;
    heappool.ckstart.size = 0;
    heappool.ckstart.prev = 0;

    heappool.ckend.prev = (heapchunk_t*)heappool.pool;
    heappool.ckend.next = 0;
    heappool.ckend.size = HEAP_TOTAL_SIZE;

    ((heapchunk_t*)heappool.pool)->prev = &heappool.ckstart;
    ((heapchunk_t*)heappool.pool)->next = &heappool.ckend;
    ((heapchunk_t*)heappool.pool)->size = HEAP_TOTAL_SIZE;

    heappool.avail_size = HEAP_TOTAL_SIZE;
    heappool.init = 1;
    spinlock_unlock(&heappool.lock);
}

int8_t* kmalloc(unsigned int alloc_size)
{
    heapchunk_t* pck = 0;
    heapchunk_t* new_pck = 0;
    uint8_t* ret_addr = 0;
    unsigned int req_size = sizeof(heapchunk_t) + alloc_size;

    spinlock_lock(&heappool.lock);
    if (!heappool.init) {
        spinlock_unlock(&heappool.lock);
        kheap_init();
        spinlock_lock(&heappool.lock);
    }

    if (req_size < alloc_size)         // overflow
        goto ALLOC_FAIL;
    if (req_size > heappool.avail_size)
        goto ALLOC_FAIL;
    if (!req_size)
        goto ALLOC_FAIL;

    pck = &heappool.ckstart;
    do {
        pck = pck->next;
    } while (pck->next && pck->size < req_size);

    if (pck >= &heappool.ckend)
        goto ALLOC_FAIL;

    ret_addr = (uint8_t*)pck + sizeof(heapchunk_t);
    memset(ret_addr, 0, alloc_size);
    pck->prev->next = pck->next;
    pck->next->prev = pck->prev;

    if (pck->size - req_size >= sizeof(heapchunk_t)) {
        new_pck = (heapchunk_t*)((uint8_t*)pck + req_size);
        new_pck->size = pck->size - req_size;

        pck = &heappool.ckstart;
        while (new_pck->size > pck->next->size) {
            pck = pck->next;
        }

        new_pck->prev = pck;
        new_pck->next = pck->next;
        pck->next->prev = new_pck;
        pck->next = new_pck;
    }
    heappool.avail_size -= req_size;
    spinlock_unlock(&heappool.lock);

    return (int8_t*)ret_addr;

ALLOC_FAIL:
    spinlock_unlock(&heappool.lock);
    return 0;
}

void kfree(void* pointer)
{
    heapchunk_t* pck = 0;
    heapchunk_t* free_pck = 0;
    
    if (!pointer)
        return;

    spinlock_lock(&heappool.lock);
    free_pck = (heapchunk_t *)((uint8_t*)pointer - sizeof(heapchunk_t));

    pck = &heappool.ckstart;
    while (free_pck->size > pck->next->size) {
        pck = pck->next;
    }
    free_pck->prev = pck;
    free_pck->next = pck->next;
    pck->next->prev = free_pck;
    pck->next = free_pck;

    heappool.avail_size += free_pck->size;
    spinlock_unlock(&heappool.lock);
}

module_init(kheap_init);