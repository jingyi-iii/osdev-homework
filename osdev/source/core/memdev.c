#include "memdev.h"

int mem_alloc_dev(const char *name, size_t size, memdev **out_dev)
{
    (void)name;
    (void)size;
    (void)out_dev;
    return 0;
}

int mem_free_dev(memdev *dev)
{
    (void)dev;
    return 0;
}
