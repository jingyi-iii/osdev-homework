/**
 * rbtree.c — Red-Black Tree implementation
 *
 * This follows the classic Linux-kernel-style intrusive rbtree.
 * The lowest bit of __rb_parent_color stores the colour:
 *   0 = RED     1 = BLACK
 */

#include "lib/rbtree.h"

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

static inline unsigned long rb_color(const rb_node *node)
{
    return node->__rb_parent_color & 1UL;
}

static inline void rb_set_parent_color(rb_node *node, rb_node *parent, unsigned long color)
{
    node->__rb_parent_color = (unsigned long)parent | color;
}

static inline void rb_set_parent(rb_node *node, rb_node *parent)
{
    node->__rb_parent_color = (unsigned long)parent | (node->__rb_parent_color & 1UL);
}

static inline rb_node *rb_parent(const rb_node *node)
{
    return (rb_node *)(node->__rb_parent_color & ~3UL);
}

static inline int rb_is_red(const rb_node *node)
{
    return node && !rb_color(node);
}

static inline int rb_is_black(const rb_node *node)
{
    return !node || rb_color(node);
}

static inline void rb_set_red(rb_node *node)
{
    node->__rb_parent_color &= ~1UL;
}

static inline void rb_set_black(rb_node *node)
{
    node->__rb_parent_color |= 1UL;
}

/* --------------------------------------------------------------------------
 * Rotations
 * -------------------------------------------------------------------------- */

static void __rb_rotate_left(rb_node *node, rb_root *root)
{
    rb_node *right = node->rb_right;
    rb_node *parent = rb_parent(node);

    node->rb_right = right->rb_left;
    if (right->rb_left)
        rb_set_parent(right->rb_left, node);

    right->rb_left = node;
    rb_set_parent(right, parent);

    if (parent) {
        if (node == parent->rb_left)
            parent->rb_left = right;
        else
            parent->rb_right = right;
    } else {
        root->rb_node = right;
    }
    rb_set_parent(node, right);
}

static void __rb_rotate_right(rb_node *node, rb_root *root)
{
    rb_node *left = node->rb_left;
    rb_node *parent = rb_parent(node);

    node->rb_left = left->rb_right;
    if (left->rb_right)
        rb_set_parent(left->rb_right, node);

    left->rb_right = node;
    rb_set_parent(left, parent);

    if (parent) {
        if (node == parent->rb_right)
            parent->rb_right = left;
        else
            parent->rb_left = left;
    } else {
        root->rb_node = left;
    }
    rb_set_parent(node, left);
}

/* --------------------------------------------------------------------------
 * Insert (balancing)
 * -------------------------------------------------------------------------- */

void rb_insert_color(rb_node *node, rb_root *root)
{
    rb_node *parent, *gparent;

    while ((parent = rb_parent(node)) && rb_is_red(parent)) {
        gparent = rb_parent(parent);

        if (parent == gparent->rb_left) {
            rb_node *uncle = gparent->rb_right;

            if (rb_is_red(uncle)) {
                /* Case 1: uncle is red — recolour */
                rb_set_black(uncle);
                rb_set_black(parent);
                rb_set_red(gparent);
                node = gparent;
                continue;
            }

            /* Case 2: node is right child — rotate left */
            if (parent->rb_right == node) {
                __rb_rotate_left(parent, root);
                rb_node *tmp = parent;
                parent = node;
                node = tmp;
            }

            /* Case 3: node is left child — rotate right */
            rb_set_black(parent);
            rb_set_red(gparent);
            __rb_rotate_right(gparent, root);
        } else {
            /* Mirror: parent == gparent->rb_right */
            rb_node *uncle = gparent->rb_left;

            if (rb_is_red(uncle)) {
                rb_set_black(uncle);
                rb_set_black(parent);
                rb_set_red(gparent);
                node = gparent;
                continue;
            }

            if (parent->rb_left == node) {
                __rb_rotate_right(parent, root);
                rb_node *tmp = parent;
                parent = node;
                node = tmp;
            }

            rb_set_black(parent);
            rb_set_red(gparent);
            __rb_rotate_left(gparent, root);
        }
    }

    rb_set_black(root->rb_node);
}

/* --------------------------------------------------------------------------
 * Insert (BST insertion + balancing)
 * -------------------------------------------------------------------------- */

void rb_insert(rb_node *node, rb_root *root, rb_cmp_node_fn cmp)
{
    rb_node **link = &root->rb_node;
    rb_node *parent = 0;

    /* Standard BST insertion */
    while (*link) {
        parent = *link;
        int result = cmp(node, parent);
        if (result < 0)
            link = &parent->rb_left;
        else
            link = &parent->rb_right;
    }

    /* Link the new node */
    node->__rb_parent_color = (unsigned long)parent; /* RED by default (bit 0 = 0) */
    node->rb_left = 0;
    node->rb_right = 0;
    *link = node;

    /* Rebalance */
    rb_insert_color(node, root);
}

/* --------------------------------------------------------------------------
 * Erase
 * -------------------------------------------------------------------------- */

static void __rb_erase_color(rb_node *parent, rb_root *root)
{
    rb_node *node = 0, *sibling;

    while ((!node || rb_is_black(node)) && node != root->rb_node) {
        if (parent->rb_left == node) {
            sibling = parent->rb_right;

            if (rb_is_red(sibling)) {
                /* Case 1: sibling is red */
                rb_set_black(sibling);
                rb_set_red(parent);
                __rb_rotate_left(parent, root);
                sibling = parent->rb_right;
            }

            if (rb_is_black(sibling->rb_left) && rb_is_black(sibling->rb_right)) {
                /* Case 2: both of sibling's children are black */
                rb_set_red(sibling);
                node = parent;
                parent = rb_parent(node);
            } else {
                if (rb_is_black(sibling->rb_right)) {
                    /* Case 3: sibling's right child is black */
                    rb_set_black(sibling->rb_left);
                    rb_set_red(sibling);
                    __rb_rotate_right(sibling, root);
                    sibling = parent->rb_right;
                }
                /* Case 4 */
                if (rb_is_black(parent))
                    rb_set_black(sibling);
                else
                    rb_set_red(sibling);
                rb_set_black(parent);
                rb_set_black(sibling->rb_right);
                __rb_rotate_left(parent, root);
                node = root->rb_node;
                break;
            }
        } else {
            /* Mirror */
            sibling = parent->rb_left;

            if (rb_is_red(sibling)) {
                rb_set_black(sibling);
                rb_set_red(parent);
                __rb_rotate_right(parent, root);
                sibling = parent->rb_left;
            }

            if (rb_is_black(sibling->rb_left) && rb_is_black(sibling->rb_right)) {
                rb_set_red(sibling);
                node = parent;
                parent = rb_parent(node);
            } else {
                if (rb_is_black(sibling->rb_left)) {
                    rb_set_black(sibling->rb_right);
                    rb_set_red(sibling);
                    __rb_rotate_left(sibling, root);
                    sibling = parent->rb_left;
                }
                if (rb_is_black(parent))
                    rb_set_black(sibling);
                else
                    rb_set_red(sibling);
                rb_set_black(parent);
                rb_set_black(sibling->rb_left);
                __rb_rotate_right(parent, root);
                node = root->rb_node;
                break;
            }
        }
    }

    if (node)
        rb_set_black(node);
}

void rb_erase(rb_node *node, rb_root *root)
{
    rb_node *child, *parent;
    unsigned long color;

    if (!node->rb_left) {
        /* No left child — promote right child */
        child = node->rb_right;
    } else if (!node->rb_right) {
        /* No right child — promote left child */
        child = node->rb_left;
    } else {
        /* Node has two children — find successor */
        rb_node *old = node;
        node = node->rb_right;
        while (node->rb_left)
            node = node->rb_left;

        if (rb_parent(old)) {
            if (rb_parent(old)->rb_left == old)
                rb_parent(old)->rb_left = node;
            else
                rb_parent(old)->rb_right = node;
        } else {
            root->rb_node = node;
        }

        child = node->rb_right;
        parent = rb_parent(node);
        color = rb_color(node);

        if (parent == old) {
            parent = node;
        } else {
            if (child)
                rb_set_parent(child, parent);
            parent->rb_left = child;

            node->rb_right = old->rb_right;
            rb_set_parent(old->rb_right, node);
        }

        node->__rb_parent_color = old->__rb_parent_color;
        node->rb_left = old->rb_left;
        rb_set_parent(old->rb_left, node);

        goto color;
    }

    parent = rb_parent(node);
    color = rb_color(node);

    if (child)
        rb_set_parent(child, parent);

    if (parent) {
        if (parent->rb_left == node)
            parent->rb_left = child;
        else
            parent->rb_right = child;
    } else {
        root->rb_node = child;
    }

color:
    if (color == 1) /* BLACK */
        __rb_erase_color(parent, root);
}

/* --------------------------------------------------------------------------
 * Replace
 * -------------------------------------------------------------------------- */

void rb_replace_node(rb_node *old, rb_node *new_node, rb_root *root)
{
    rb_node *parent = rb_parent(old);

    /* Copy children */
    new_node->rb_left = old->rb_left;
    new_node->rb_right = old->rb_right;
    new_node->__rb_parent_color = old->__rb_parent_color;

    /* Update child -> parent links */
    if (old->rb_left)
        rb_set_parent(old->rb_left, new_node);
    if (old->rb_right)
        rb_set_parent(old->rb_right, new_node);

    /* Update parent -> child link */
    if (parent) {
        if (parent->rb_left == old)
            parent->rb_left = new_node;
        else
            parent->rb_right = new_node;
    } else {
        root->rb_node = new_node;
    }
}

/* --------------------------------------------------------------------------
 * Traversal
 * -------------------------------------------------------------------------- */

rb_node *rb_first(const rb_root *root)
{
    rb_node *node = root->rb_node;
    if (!node)
        return 0;
    while (node->rb_left)
        node = node->rb_left;
    return node;
}

rb_node *rb_last(const rb_root *root)
{
    rb_node *node = root->rb_node;
    if (!node)
        return 0;
    while (node->rb_right)
        node = node->rb_right;
    return node;
}

rb_node *rb_next(const rb_node *node)
{
    /* If right subtree exists, go to leftmost of right subtree */
    if (node->rb_right) {
        node = node->rb_right;
        while (node->rb_left)
            node = node->rb_left;
        return (rb_node *)node;
    }

    /* Otherwise, go up until we're a left child */
    rb_node *parent;
    while ((parent = rb_parent(node)) && node == parent->rb_right)
        node = parent;

    return parent;
}

rb_node *rb_prev(const rb_node *node)
{
    /* If left subtree exists, go to rightmost of left subtree */
    if (node->rb_left) {
        node = node->rb_left;
        while (node->rb_right)
            node = node->rb_right;
        return (rb_node *)node;
    }

    /* Otherwise, go up until we're a right child */
    rb_node *parent;
    while ((parent = rb_parent(node)) && node == parent->rb_left)
        node = parent;

    return parent;
}

/* --------------------------------------------------------------------------
 * Search
 * -------------------------------------------------------------------------- */

rb_node *rb_find(const void *key, rb_root *root, rb_cmp_key_fn cmp)
{
    rb_node *node = root->rb_node;

    while (node) {
        int result = cmp(key, node);
        if (result < 0)
            node = node->rb_left;
        else if (result > 0)
            node = node->rb_right;
        else
            return node;
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Find-or-insert
 * -------------------------------------------------------------------------- */

rb_node *rb_find_or_insert(const void *key, rb_node *node, rb_root *root,
                           rb_cmp_key_fn cmp_key, rb_cmp_node_fn cmp_node)
{
    rb_node **link = &root->rb_node;
    rb_node *parent = 0;

    while (*link) {
        parent = *link;
        int result = cmp_key(key, parent);
        if (result < 0)
            link = &parent->rb_left;
        else if (result > 0)
            link = &parent->rb_right;
        else
            return parent; /* already exists */
    }

    /* Not found — insert */
    node->__rb_parent_color = (unsigned long)parent; /* RED */
    node->rb_left = 0;
    node->rb_right = 0;
    *link = node;

    rb_insert_color(node, root);
    return 0;
}
