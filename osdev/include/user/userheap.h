#ifndef USER_USERHEAP_H
#define USER_USERHEAP_H

/* User-mode heap for the DRIVER_CLASS_USER servers (see user/userheap.c).
 * The ring-3 servers (terminal_server, kb_server, ...) call malloc/free
 * here instead of the kernel kmalloc/kfree, so their allocations live in
 * their own isolated pool instead of the kernel heap. */

void* malloc(unsigned int size);
void  free(void* ptr);

#endif
