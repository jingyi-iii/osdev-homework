#ifndef HEAP_H
#define HEAP_H

#include "lib/types.h"

void* kmalloc(unsigned int alloc_size);
void kfree(void* pointer);

#endif
