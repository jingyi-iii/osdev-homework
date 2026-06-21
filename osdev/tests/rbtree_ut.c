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
 * Test container — embeds rb_node just like real kernel code would
 * ------------------------------------------------------------------ */
typedef struct {
    int key;
    rb_node node;
} test_item;

/* ------------------------------------------------------------------
 * Comparison callbacks
 * ------------------------------------------------------------------ */

static int cmp_node(const rb_node *a, const rb_node *b)
{
    test_item *ia = rb_entry(a, test_item, node);
    test_item *ib = rb_entry(b, test_item, node);
    return ia->key - ib->key;
}

static int cmp_key(const void *key, const rb_node *b)
{
    int k = *(const int *)key;
    test_item *ib = rb_entry(b, test_item, node);
    return k - ib->key;
}

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */

/** Verify the in-order traversal matches a sorted array of keys. */
static void verify_inorder(rb_root *root, const int *expected, int count)
{
    int i = 0;
    rb_node *n;
    rb_for_each(n, root) {
        assert(i < count);
        test_item *item = rb_entry(n, test_item, node);
        assert(item->key == expected[i]);
        i++;
    }
    assert(i == count);
}

/* ==================================================================
 * Test 1 — Empty tree
 * ================================================================== */
static void test_empty_tree(void)
{
    printf("Test 1: Empty tree operations\n");

    rb_root root = RB_ROOT;
    assert(rb_first(&root) == NULL);
    assert(rb_last(&root) == NULL);

    int key = 42;
    assert(rb_find(&key, &root, cmp_key) == NULL);

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 2 — Single node insert / find
 * ================================================================== */
static void test_single_insert_find(void)
{
    printf("Test 2: Single node insert and find\n");

    rb_root root = RB_ROOT;
    test_item item;
    item.key = 100;

    rb_insert(&item.node, &root, cmp_node);

    /* Find by key */
    int key = 100;
    rb_node *found = rb_find(&key, &root, cmp_key);
    assert(found != NULL);
    assert(rb_entry(found, test_item, node)->key == 100);

    /* Find non-existing */
    key = 200;
    assert(rb_find(&key, &root, cmp_key) == NULL);

    /* Traversal */
    assert(rb_first(&root) == &item.node);
    assert(rb_last(&root) == &item.node);
    assert(rb_next(&item.node) == NULL);
    assert(rb_prev(&item.node) == NULL);

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 3 — Multiple nodes insert, in-order verification
 * ================================================================== */
static void test_multiple_insert(void)
{
    printf("Test 3: Multiple nodes insert\n");

    rb_root root = RB_ROOT;
    test_item items[7];
    int keys[] = {50, 30, 70, 20, 40, 60, 80};
    int expected[] = {20, 30, 40, 50, 60, 70, 80};

    for (int i = 0; i < 7; i++) {
        items[i].key = keys[i];
        rb_insert(&items[i].node, &root, cmp_node);
    }

    /* Verify in-order traversal gives sorted order */
    verify_inorder(&root, expected, 7);

    /* Verify first / last */
    assert(rb_entry(rb_first(&root), test_item, node)->key == 20);
    assert(rb_entry(rb_last(&root), test_item, node)->key == 80);

    /* Verify each key is findable */
    for (int i = 0; i < 7; i++) {
        int key = keys[i];
        assert(rb_find(&key, &root, cmp_key) != NULL);
    }

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 4 — Duplicate keys go to the right
 * ================================================================== */
static void test_duplicate_keys(void)
{
    printf("Test 4: Duplicate key handling\n");

    rb_root root = RB_ROOT;
    test_item a, b;
    a.key = 50;
    b.key = 50;  /* duplicate */

    rb_insert(&a.node, &root, cmp_node);
    rb_insert(&b.node, &root, cmp_node);

    /* Both should be in the tree */
    int key = 50;
    assert(rb_find(&key, &root, cmp_key) != NULL);

    /* Two nodes total */
    int count = 0;
    rb_node *n;
    rb_for_each(n, &root) count++;
    assert(count == 2);

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 5 — rb_next / rb_prev full walk
 * ================================================================== */
static void test_next_prev_walk(void)
{
    printf("Test 5: rb_next / rb_prev walk\n");

    rb_root root = RB_ROOT;
    test_item items[5];
    for (int i = 0; i < 5; i++) {
        items[i].key = (i + 1) * 10;  /* 10, 20, 30, 40, 50 */
        rb_insert(&items[i].node, &root, cmp_node);
    }

    /* Forward walk */
    int count = 0;
    rb_node *n = rb_first(&root);
    while (n) {
        count++;
        n = rb_next(n);
    }
    assert(count == 5);

    /* Backward walk */
    count = 0;
    n = rb_last(&root);
    while (n) {
        count++;
        n = rb_prev(n);
    }
    assert(count == 5);

    /* Forward values */
    int expected[] = {10, 20, 30, 40, 50};
    int i = 0;
    for (n = rb_first(&root); n; n = rb_next(n)) {
        assert(rb_entry(n, test_item, node)->key == expected[i++]);
    }

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 6 — Erase leaf node
 * ================================================================== */
static void test_erase_leaf(void)
{
    printf("Test 6: Erase leaf node\n");

    rb_root root = RB_ROOT;
    test_item items[3];
    int keys[] = {50, 30, 70};
    for (int i = 0; i < 3; i++) {
        items[i].key = keys[i];
        rb_insert(&items[i].node, &root, cmp_node);
    }

    /* Erase leaf 70 */
    rb_erase(&items[2].node, &root);

    int expected[] = {30, 50};
    verify_inorder(&root, expected, 2);

    int key = 70;
    assert(rb_find(&key, &root, cmp_key) == NULL);

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 7 — Erase node with one child
 * ================================================================== */
static void test_erase_single_child(void)
{
    printf("Test 7: Erase node with one child\n");

    rb_root root = RB_ROOT;
    test_item items[4];
    int keys[] = {50, 30, 70, 80};
    for (int i = 0; i < 4; i++) {
        items[i].key = keys[i];
        rb_insert(&items[i].node, &root, cmp_node);
    }

    /* 70 has one child (80). Erase 70. */
    rb_erase(&items[2].node, &root);  /* items[2] key=70 */

    int expected[] = {30, 50, 80};
    verify_inorder(&root, expected, 3);

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 8 — Erase node with two children
 * ================================================================== */
static void test_erase_two_children(void)
{
    printf("Test 8: Erase node with two children\n");

    rb_root root = RB_ROOT;
    test_item items[7];
    int keys[] = {50, 30, 70, 20, 40, 60, 80};
    for (int i = 0; i < 7; i++) {
        items[i].key = keys[i];
        rb_insert(&items[i].node, &root, cmp_node);
    }

    /* Erase root (50) — has two children */
    rb_erase(&items[0].node, &root);

    int expected[] = {20, 30, 40, 60, 70, 80};
    verify_inorder(&root, expected, 6);

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 9 — Erase all nodes one by one
 * ================================================================== */
static void test_erase_all(void)
{
    printf("Test 9: Erase all nodes\n");

    rb_root root = RB_ROOT;
    test_item items[10];
    for (int i = 0; i < 10; i++) {
        items[i].key = i * 10;
        rb_insert(&items[i].node, &root, cmp_node);
    }

    /* Erase from smallest to largest */
    for (int i = 0; i < 10; i++) {
        rb_erase(&items[i].node, &root);
    }

    assert(rb_first(&root) == NULL);
    assert(rb_last(&root) == NULL);

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 10 — rb_replace_node
 * ================================================================== */
static void test_replace_node(void)
{
    printf("Test 10: rb_replace_node\n");

    rb_root root = RB_ROOT;
    test_item a, b, c;
    a.key = 30;
    b.key = 10;
    c.key = 50;

    rb_insert(&a.node, &root, cmp_node);
    rb_insert(&b.node, &root, cmp_node);
    rb_insert(&c.node, &root, cmp_node);

    /* Replace 'a' (30) with a new node */
    test_item replacement;
    replacement.key = 30;  /* same logical key */
    rb_replace_node(&a.node, &replacement.node, &root);

    int expected[] = {10, 30, 50};
    verify_inorder(&root, expected, 3);

    int key = 30;
    assert(rb_find(&key, &root, cmp_key) == &replacement.node);

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 11 — find_or_insert (existing)
 * ================================================================== */
static void test_find_or_insert_existing(void)
{
    printf("Test 11: find_or_insert — existing key\n");

    rb_root root = RB_ROOT;
    test_item a;
    a.key = 42;
    rb_insert(&a.node, &root, cmp_node);

    test_item b;
    b.key = 42;  /* same key */
    int key = 42;
    rb_node *existing = rb_find_or_insert(&key, &b.node, &root, cmp_key, cmp_node);

    assert(existing == &a.node);  /* returned the existing one */
    assert(rb_entry(existing, test_item, node)->key == 42);

    /* 'b' was NOT inserted, tree still has 1 node */
    int count = 0;
    rb_node *n;
    rb_for_each(n, &root) count++;
    assert(count == 1);

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 12 — find_or_insert (new)
 * ================================================================== */
static void test_find_or_insert_new(void)
{
    printf("Test 12: find_or_insert — new key\n");

    rb_root root = RB_ROOT;
    test_item a;
    a.key = 42;
    rb_insert(&a.node, &root, cmp_node);

    test_item b;
    b.key = 99;  /* different key */
    int key = 99;
    rb_node *existing = rb_find_or_insert(&key, &b.node, &root, cmp_key, cmp_node);

    assert(existing == NULL);  /* NULL means inserted */

    int expected[] = {42, 99};
    verify_inorder(&root, expected, 2);

    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 13 — Stress: 100 random inserts, verify sorted order
 * ================================================================== */
static void test_stress_insert_find(void)
{
    printf("Test 13: Stress test — 100 nodes\n");

    #define STRESS_COUNT 100
    rb_root root = RB_ROOT;
    test_item *items = malloc(sizeof(test_item) * STRESS_COUNT);
    assert(items != NULL);

    /* Insert 100 keys (descending — worst-case-ish for naive BST) */
    for (int i = 0; i < STRESS_COUNT; i++) {
        items[i].key = STRESS_COUNT - i;
        rb_insert(&items[i].node, &root, cmp_node);
    }

    /* Verify in-order traversal is sorted ascending */
    int prev = -1;
    rb_node *n;
    rb_for_each(n, &root) {
        int cur = rb_entry(n, test_item, node)->key;
        assert(cur > prev);
        prev = cur;
    }

    /* Find every key */
    for (int i = 0; i < STRESS_COUNT; i++) {
        int k = i + 1;
        assert(rb_find(&k, &root, cmp_key) != NULL);
    }

    /* Erase all */
    for (int i = 0; i < STRESS_COUNT; i++) {
        rb_erase(&items[i].node, &root);
    }
    assert(rb_first(&root) == NULL);

    free(items);
    #undef STRESS_COUNT
    printf("  ✓ Passed\n");
}

/* ==================================================================
 * Test 14 — rb_for_each_safe (erase while iterating)
 * ================================================================== */
static void test_for_each_safe(void)
{
    printf("Test 14: rb_for_each_safe erase-while-iterating\n");

    rb_root root = RB_ROOT;
    test_item items[5];
    for (int i = 0; i < 5; i++) {
        items[i].key = (i + 1) * 10;
        rb_insert(&items[i].node, &root, cmp_node);
    }

    /* Erase nodes with key < 30 while iterating */
    rb_node *pos, *n;
    int erased = 0;
    rb_for_each_safe(pos, n, &root) {
        test_item *item = rb_entry(pos, test_item, node);
        if (item->key < 30) {
            rb_erase(pos, &root);
            erased++;
        }
    }
    assert(erased == 2);  /* 10, 20 */

    int expected[] = {30, 40, 50};
    verify_inorder(&root, expected, 3);

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
    test_replace_node();
    test_find_or_insert_existing();
    test_find_or_insert_new();
    test_stress_insert_find();
    test_for_each_safe();

    printf("\nAll Red-Black Tree tests passed!\n\n");
    return 0;
}
