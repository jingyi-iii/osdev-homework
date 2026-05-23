#ifndef __COMPILER_H__
#define __COMPILER_H__

#define ATTR(x)                         \
    __attribute__((x))
#define ATTR_ALIGINED(T)                \
    __attribute__((aligned(sizeof(T))))
#define ATTR_PACKED                     \
    __attribute__((packed))

#endif
