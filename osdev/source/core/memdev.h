#ifndef __MEMDEV_H__
#define __MEMDEV_H__

#include <stdint.h>
#include <stddef.h>

typedef struct memdev {
    const char *name;
    void *base_addr;
    size_t size;
    struct memdev *next;
} memdev;

int mem_alloc_dev(const char *name, size_t size, memdev **out_dev);
int mem_free_dev(memdev *dev);

#endif
