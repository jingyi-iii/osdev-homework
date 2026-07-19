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

void 	rbtree_init		(rbtree* tree, rbnode* nil);
rbtree* rbtree_create	(void);
void    rbtree_destroy	(rbtree* tree);
void    rbtree_insert	(rbtree* tree, rbnode* new_node, rbtree_node_cmp cmp);
rbnode* rbtree_search	(rbtree* tree, const void* key, rbtree_key_cmp cmp);
void    rbtree_delete	(rbtree* tree, rbnode* node);

#define rb_entry(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

/** Return the leftmost (smallest) node in the tree, or 0 if empty. */
static inline rbnode* rbtree_first(rbtree* tree)
{
	rbnode* n = tree->root;
	if (n == tree->nil)
		return 0;

	while (n->left != tree->nil)
		n = n->left;
	return n;
}

/** Return the rightmost (largest) node in the tree, or 0 if empty. */
static inline rbnode* rbtree_last(rbtree* tree)
{
	rbnode* n = tree->root;
	if (n == tree->nil)
		return 0;

	while (n->right != tree->nil)
		n = n->right;
	return n;
}

/** Return the in-order successor of @node, or 0 if none. */
static inline rbnode* rbtree_next(rbtree* tree, rbnode* node)
{
	if (node->right != tree->nil) {
		node = node->right;
		while (node->left != tree->nil)
			node = node->left;
		return node;
	}

	rbnode* p = node->parent;
	while (p != tree->nil && node == p->right) {
		node = p;
		p = p->parent;
	}
	return (p == tree->nil) ? 0 : p;
}

/** Return the in-order predecessor of @node, or 0 if none. */
static inline rbnode* rbtree_prev(rbtree* tree, rbnode* node)
{
	if (node->left != tree->nil) {
		node = node->left;
		while (node->right != tree->nil)
			node = node->right;
		return node;
	}

	rbnode* p = node->parent;
	while (p != tree->nil && node == p->left) {
		node = p;
		p = p->parent;
	}
	return (p == tree->nil) ? 0 : p;
}

/** Iterate @pos over every node in @tree in sorted order. */
#define rbtree_for_each(pos, tree) \
	for (rbnode* (pos) = rbtree_first(tree); \
	     (pos) != 0; \
	     (pos) = rbtree_next((tree), (pos)))

/** Iterate @pos over every node, safe against deletion of @pos. */
#define rbtree_for_each_safe(pos, n, tree) \
	for (rbnode* (pos) = rbtree_first(tree), *(n) = (pos) ? rbtree_next((tree), (pos)) : 0; \
	     (pos) != 0; \
	     (pos) = (n), (n) = (pos) ? rbtree_next((tree), (pos)) : 0)

/**
 * Search for @key; if found return the existing node, otherwise insert
 * @new_node and return 0.
 */
static inline rbnode* rbtree_find_or_insert(rbtree* tree, const void* key,
                                            rbnode* new_node,
                                            rbtree_key_cmp key_cmp,
                                            rbtree_node_cmp node_cmp)
{
	rbnode* found = rbtree_search(tree, key, key_cmp);
	if (found)
		return found;
	rbtree_insert(tree, new_node, node_cmp);
	return 0;
}

#endif
