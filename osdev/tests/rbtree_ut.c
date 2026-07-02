/**
 * rbtree_ut.c — Unit tests for the Red-Black Tree
 *
 * Compiled with host gcc (see tests/CMakeLists.txt).
 * Uses assert() for validation — crashes on failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "rbtree.h"

/* ------------------------------------------------------------------
 * Test container — embeds rbnode just like real kernel code would
 * ------------------------------------------------------------------ */
typedef struct {
    int key;
    rbnode node;
} test_item;

/* ------------------------------------------------------------------
 * Comparison callbacks
 * ------------------------------------------------------------------ */

static int cmp_node(const rbnode* a, const rbnode* b)
{
    test_item* ia = rb_entry(a, test_item, node);
    test_item* ib = rb_entry(b, test_item, node);
    return ia->key - ib->key;
}

static int cmp_key(const void* key, const rbnode* b)
{
    int k = *(const int*)key;
    test_item* ib = rb_entry(b, test_item, node);
    return k - ib->key;
}

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */

/** Verify the in-order traversal matches a sorted array of keys. */
static void verify_inorder(rbtree* tree, const int* expected, int count)
{
    int i = 0;
    rbnode* n;
    rbtree_for_each(n, tree) {
        assert(i < count);
        test_item* item = rb_entry(n, test_item, node);
        assert(item->key == expected[i]);
        i++;
    }
    assert(i == count);
}

/** Count nodes in a tree. */
static int tree_count(rbtree* tree)
{
    int c = 0;
    rbnode* n;
    rbtree_for_each(n, tree) c++;
    return c;
}

/* ==================================================================
 * Test 1 — Empty tree
 * ================================================================== */
static void test_empty_tree(void)
{
    printf("Test 1: Empty tree operations\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);
    assert(rbtree_first(tree) == NULL);
    assert(rbtree_last(tree) == NULL);

    int key = 42;
    assert(rbtree_search(tree, &key, cmp_key) == NULL);

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 2 — Single node insert / find
 * ================================================================== */
static void test_single_insert_find(void)
{
    printf("Test 2: Single node insert and find\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item item;
    item.key = 100;

    rbtree_insert(tree, &item.node, cmp_node);

    /* Find by key */
    int key = 100;
    rbnode* found = rbtree_search(tree, &key, cmp_key);
    assert(found != NULL);
    assert(rb_entry(found, test_item, node)->key == 100);

    /* Find non-existing */
    key = 200;
    assert(rbtree_search(tree, &key, cmp_key) == NULL);

    /* Traversal */
    assert(rbtree_first(tree) == &item.node);
    assert(rbtree_last(tree) == &item.node);
    assert(rbtree_next(tree, &item.node) == NULL);
    assert(rbtree_prev(tree, &item.node) == NULL);

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 3 — Multiple nodes insert, in-order verification
 * ================================================================== */
static void test_multiple_insert(void)
{
    printf("Test 3: Multiple nodes insert\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item items[7];
    int keys[] = {50, 30, 70, 20, 40, 60, 80};
    int expected[] = {20, 30, 40, 50, 60, 70, 80};

    for (int i = 0; i < 7; i++) {
        items[i].key = keys[i];
        rbtree_insert(tree, &items[i].node, cmp_node);
    }

    /* Verify in-order traversal gives sorted order */
    verify_inorder(tree, expected, 7);

    /* Verify first / last */
    assert(rb_entry(rbtree_first(tree), test_item, node)->key == 20);
    assert(rb_entry(rbtree_last(tree), test_item, node)->key == 80);

    /* Verify each key is findable */
    for (int i = 0; i < 7; i++) {
        int key = keys[i];
        assert(rbtree_search(tree, &key, cmp_key) != NULL);
    }

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 4 — Duplicate keys go to the right
 * ================================================================== */
static void test_duplicate_keys(void)
{
    printf("Test 4: Duplicate key handling\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item a, b;
    a.key = 50;
    b.key = 50;  /* duplicate */

    rbtree_insert(tree, &a.node, cmp_node);
    rbtree_insert(tree, &b.node, cmp_node);

    /* Both should be in the tree */
    int key = 50;
    assert(rbtree_search(tree, &key, cmp_key) != NULL);

    /* Two nodes total */
    assert(tree_count(tree) == 2);

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 5 — rbtree_next / rbtree_prev full walk
 * ================================================================== */
static void test_next_prev_walk(void)
{
    printf("Test 5: rbtree_next / rbtree_prev walk\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item items[5];
    for (int i = 0; i < 5; i++) {
        items[i].key = (i + 1) * 10;  /* 10, 20, 30, 40, 50 */
        rbtree_insert(tree, &items[i].node, cmp_node);
    }

    /* Forward walk */
    int count = 0;
    rbnode* n = rbtree_first(tree);
    while (n) {
        count++;
        n = rbtree_next(tree, n);
    }
    assert(count == 5);

    /* Backward walk */
    count = 0;
    n = rbtree_last(tree);
    while (n) {
        count++;
        n = rbtree_prev(tree, n);
    }
    assert(count == 5);

    /* Forward values */
    int expected[] = {10, 20, 30, 40, 50};
    int i = 0;
    for (n = rbtree_first(tree); n; n = rbtree_next(tree, n)) {
        assert(rb_entry(n, test_item, node)->key == expected[i++]);
    }

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 6 — Erase leaf node
 * ================================================================== */
static void test_erase_leaf(void)
{
    printf("Test 6: Erase leaf node\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item items[3];
    int keys[] = {50, 30, 70};
    for (int i = 0; i < 3; i++) {
        items[i].key = keys[i];
        rbtree_insert(tree, &items[i].node, cmp_node);
    }

    /* Erase leaf 70 */
    rbtree_delete(tree, &items[2].node);

    int expected[] = {30, 50};
    verify_inorder(tree, expected, 2);

    int key = 70;
    assert(rbtree_search(tree, &key, cmp_key) == NULL);

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 7 — Erase node with one child
 * ================================================================== */
static void test_erase_single_child(void)
{
    printf("Test 7: Erase node with one child\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item items[4];
    int keys[] = {50, 30, 70, 80};
    for (int i = 0; i < 4; i++) {
        items[i].key = keys[i];
        rbtree_insert(tree, &items[i].node, cmp_node);
    }

    /* 70 has one child (80). Erase 70. */
    rbtree_delete(tree, &items[2].node);  /* items[2] key=70 */

    int expected[] = {30, 50, 80};
    verify_inorder(tree, expected, 3);

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 8 — Erase node with two children
 * ================================================================== */
static void test_erase_two_children(void)
{
    printf("Test 8: Erase node with two children\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item items[7];
    int keys[] = {50, 30, 70, 20, 40, 60, 80};
    for (int i = 0; i < 7; i++) {
        items[i].key = keys[i];
        rbtree_insert(tree, &items[i].node, cmp_node);
    }

    /* Erase root (50) — has two children */
    rbtree_delete(tree, &items[0].node);

    int expected[] = {20, 30, 40, 60, 70, 80};
    verify_inorder(tree, expected, 6);

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 9 — Erase all nodes one by one
 * ================================================================== */
static void test_erase_all(void)
{
    printf("Test 9: Erase all nodes\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item items[10];
    for (int i = 0; i < 10; i++) {
        items[i].key = i * 10;
        rbtree_insert(tree, &items[i].node, cmp_node);
    }

    /* Erase from smallest to largest */
    for (int i = 0; i < 10; i++) {
        rbtree_delete(tree, &items[i].node);
    }

    assert(rbtree_first(tree) == NULL);
    assert(rbtree_last(tree) == NULL);

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 10 — find_or_insert (existing)
 * ================================================================== */
static void test_find_or_insert_existing(void)
{
    printf("Test 10: find_or_insert — existing key\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item a;
    a.key = 42;
    rbtree_insert(tree, &a.node, cmp_node);

    test_item b;
    b.key = 42;  /* same key */
    int key = 42;
    rbnode* existing = rbtree_find_or_insert(tree, &key, &b.node,
                                              cmp_key, cmp_node);

    assert(existing == &a.node);  /* returned the existing one */
    assert(rb_entry(existing, test_item, node)->key == 42);

    /* 'b' was NOT inserted, tree still has 1 node */
    assert(tree_count(tree) == 1);

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 11 — find_or_insert (new)
 * ================================================================== */
static void test_find_or_insert_new(void)
{
    printf("Test 11: find_or_insert — new key\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item a;
    a.key = 42;
    rbtree_insert(tree, &a.node, cmp_node);

    test_item b;
    b.key = 99;  /* different key */
    int key = 99;
    rbnode* existing = rbtree_find_or_insert(tree, &key, &b.node,
                                              cmp_key, cmp_node);

    assert(existing == NULL);  /* NULL means inserted */

    int expected[] = {42, 99};
    verify_inorder(tree, expected, 2);

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 12 — Stress: 100 random inserts, verify sorted order
 * ================================================================== */
static void test_stress_insert_find(void)
{
    printf("Test 12: Stress test — 100 nodes\n");

    #define STRESS_COUNT 100
    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item* items = malloc(sizeof(test_item) * STRESS_COUNT);
    assert(items != NULL);

    /* Insert 100 keys (descending — worst-case-ish for naive BST) */
    for (int i = 0; i < STRESS_COUNT; i++) {
        items[i].key = STRESS_COUNT - i;
        rbtree_insert(tree, &items[i].node, cmp_node);
    }

    /* Verify in-order traversal is sorted ascending */
    int prev = -1;
    rbnode* n;
    rbtree_for_each(n, tree) {
        int cur = rb_entry(n, test_item, node)->key;
        assert(cur > prev);
        prev = cur;
    }

    /* Find every key */
    for (int i = 0; i < STRESS_COUNT; i++) {
        int k = i + 1;
        assert(rbtree_search(tree, &k, cmp_key) != NULL);
    }

    /* Erase all */
    for (int i = 0; i < STRESS_COUNT; i++) {
        rbtree_delete(tree, &items[i].node);
    }
    assert(rbtree_first(tree) == NULL);

    free(items);
    rbtree_destroy(tree);
    #undef STRESS_COUNT
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 13 — rbtree_for_each_safe (erase while iterating)
 * ================================================================== */
static void test_for_each_safe(void)
{
    printf("Test 13: rbtree_for_each_safe erase-while-iterating\n");

    rbtree* tree = rbtree_create();
    assert(tree != NULL);

    test_item items[5];
    for (int i = 0; i < 5; i++) {
        items[i].key = (i + 1) * 10;
        rbtree_insert(tree, &items[i].node, cmp_node);
    }

    /* Erase nodes with key < 30 while iterating */
    rbnode* pos;
    rbnode* n;
    int erased = 0;
    rbtree_for_each_safe(pos, n, tree) {
        test_item* item = rb_entry(pos, test_item, node);
        if (item->key < 30) {
            rbtree_delete(tree, pos);
            erased++;
        }
    }
    assert(erased == 2);  /* 10, 20 */

    int expected[] = {30, 40, 50};
    verify_inorder(tree, expected, 3);

    rbtree_destroy(tree);
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Entry point
 * ================================================================== */
int rbtree_ut(void)
{
    printf("\n========== Red-Black Tree Unit Tests ==========\n\n");

    test_empty_tree();
    test_single_insert_find();
    test_multiple_insert();
    test_duplicate_keys();
    test_next_prev_walk();
    test_erase_leaf();
    test_erase_single_child();
    test_erase_two_children();
    test_erase_all();
    test_find_or_insert_existing();
    test_find_or_insert_new();
    test_stress_insert_find();
    test_for_each_safe();

    printf("\nAll Red-Black Tree tests passed!\n\n");
    return 0;
}
