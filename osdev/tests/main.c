#include <stdio.h>

int lock_ut(void);
int list_ut(void);
int heap_ut(void);
int cxa_guard_ut(void);
int rbtree_ut(void);

int main(void)
{
    int ret = 0;

    ret |= lock_ut();
    ret |= list_ut();
    ret |= heap_ut();
    ret |= cxa_guard_ut();
    ret |= rbtree_ut();

    /* A failing suite must fail the process, not be silently swallowed. */
    return ret != 0;
}