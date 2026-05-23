#include <stdio.h>

int lock_ut(void);
int list_ut(void);
int heap_ut(void);
int cxa_guard_ut(void);

int main(void)
{
    lock_ut();
    list_ut();
    heap_ut();
    cxa_guard_ut();

    return 0;
}