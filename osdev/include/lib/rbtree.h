#ifndef __RBTREE_H__
#define __RBTREE_H__

#include <stddef.h>

/**
 * rbtree.h — Intrusive Red-Black Tree
 *
 * Usage pattern (same as list.h):
 *   1. Embed rb_node inside your struct.
 *   2. Use rb_entry() to get the container struct from a node pointer.
 *   3. Provide a comparison callback for insert / find operations.
 */

/* --------------------------------------------------------------------------
 * Data structures
 * -------------------------------------------------------------------------- */

typedef struct rb_node {
    unsigned long __rb_parent_color;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
} rb_node;

typedef struct rb_root {
    rb_node *rb_node;
} rb_root;

/* --------------------------------------------------------------------------
 * Comparison function types
 * -------------------------------------------------------------------------- */

/** Node-vs-node comparison: return <0, 0, >0 for a<b, a==b, a>b */
typedef int (*rb_cmp_node_fn)(const rb_node *a, const rb_node *b);

/** Key-vs-node comparison: return <0, 0, >0 for key<node, key==node, key>node */
typedef int (*rb_cmp_key_fn)(const void *key, const rb_node *node);

/* --------------------------------------------------------------------------
 * Core API
 * -------------------------------------------------------------------------- */

void rb_insert_color(rb_node *node, rb_root *root);
void rb_erase(rb_node *node, rb_root *root);

/** Replace @old with @new (caller must fix up search structures) */
void rb_replace_node(rb_node *old, rb_node *new_node, rb_root *root);

/* --------------------------------------------------------------------------
 * Traversal
 * -------------------------------------------------------------------------- */

rb_node *rb_first(const rb_root *root);
rb_node *rb_last(const rb_root *root);
rb_node *rb_next(const rb_node *node);
rb_node *rb_prev(const rb_node *node);

/* --------------------------------------------------------------------------
 * Search / insert helpers (generic C interface)
 * -------------------------------------------------------------------------- */

/**
 * rb_find – locate a node matching @key using @cmp.
 * Returns NULL if not found.
 */
rb_node *rb_find(const void *key, rb_root *root, rb_cmp_key_fn cmp);

/**
 * rb_insert – insert @node into @root using node-vs-node @cmp.
 * Caller must allocate and initialise @node before calling.
 */
void rb_insert(rb_node *node, rb_root *root, rb_cmp_node_fn cmp);

/**
 * rb_find_or_insert – find an existing node matching @key, or insert @node.
 * Returns the existing node if found; otherwise returns NULL after insertion.
 *
 * Typical usage:
 *   existing = rb_find_or_insert(&key, &new->rb, &root, cmp_key, cmp_node);
 *   if (existing) {  free(new);  return rb_entry(existing, ...);  }
 *   // new was inserted
 */
rb_node *rb_find_or_insert(const void *key, rb_node *node, rb_root *root,
                           rb_cmp_key_fn cmp_key, rb_cmp_node_fn cmp_node);

/* --------------------------------------------------------------------------
 * Macros
 * -------------------------------------------------------------------------- */

#define RB_ROOT ((rb_root) { NULL })

/**
 * rb_entry – get container struct from embedded rb_node pointer.
 * Same semantics as list_entry / container_of.
 */
#define rb_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))

/** Iterate over every node in in-order. */
#define rb_for_each(pos, root) \
    for (rb_node *pos = rb_first(root); pos; pos = rb_next(pos))

/** Iterate over every node safely (allows removal of current node). */
#define rb_for_each_safe(pos, n, root) \
    for (rb_node *pos = rb_first(root), *n = pos ? rb_next(pos) : 0; \
         pos; \
         pos = n, n = pos ? rb_next(pos) : 0)

#endif /* __RBTREE_H__ */