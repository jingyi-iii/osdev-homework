#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>

int8_t* kmalloc(unsigned int alloc_size);
void kfree(void* pointer);

#endif
