#ifndef __LIST_H__
#define __LIST_H__

typedef struct list_node {
    struct list_node* prev;
    struct list_node* next;
} list_node;

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) \
    list_node name = LIST_HEAD_INIT(name)

static inline void list_init(list_node* node)
{
    node->prev = node;
    node->next = node;
}

static inline void list_add(list_node* new_node, list_node* head)
{
    if (!new_node || !head)
        return;

    new_node->next = head->next;
    new_node->prev = head;
    head->next->prev = new_node;
    head->next = new_node;
}

static inline void list_del(list_node* node)
{
    if (!node)
        return;

    node->prev->next = node->next;
    node->next->prev = node->prev;
    list_init(node);
}

#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

#define list_for_each(pos, head) \
    for (list_node* pos = (head)->next; pos != (head); pos = pos->next)

#endif
