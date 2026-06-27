#ifndef RBTREE_H
#define RBTREE_H

#include <stddef.h>
#include <stdint.h>

typedef enum color {
	RED = 0,
	BLACK = 1,
} color;

typedef struct rbnode {
	color col;
	struct rbnode* parent;
	struct rbnode* left;
	struct rbnode* right;
} rbnode;

typedef int (*rbtree_node_cmp)(const rbnode* left, const rbnode* right);
typedef int (*rbtree_key_cmp)(const void* key, const rbnode* node);

typedef struct rbtree {
	rbnode* root;
	rbnode* nil;
} rbtree;

rbtree* rbtree_create	(void);
void    rbtree_destroy	(rbtree* tree);
void    rbtree_insert	(rbtree* tree, rbnode* new_node, rbtree_node_cmp cmp);
rbnode* rbtree_search	(rbtree* tree, const void* key, rbtree_key_cmp cmp);
void    rbtree_delete	(rbtree* tree, rbnode* node);

#define rb_entry(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

#endif
