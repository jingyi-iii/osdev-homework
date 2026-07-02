/*******************************************************************************
 *                                                                             *
 *    Red-Black Tree API Test Suite                                            *
 *                                                                             *
 *    Tests the rbtree.h interface inside the kernel environment:              *
 *      - rbtree_insert                                                       *
 *      - rbtree_search                                                       *
 *      - rbtree_first / rbtree_last / rbtree_next / rbtree_prev              *
 *      - rbtree_delete                                                       *
 *      - rbtree_find_or_insert                                               *
 *      - rbtree_for_each / rbtree_for_each_safe                              *
 *                                                                             *
 *******************************************************************************/

#include "drivers/terminal_driver.h"
#include "drivers/timer_driver.h"
#include "kernel/process.h"
#include "lib/rbtree.h"
#include "lib/string.h"

extern volatile int test_finished_flag;

/* ------------------------------------------------------------------
 * Test container
 * ------------------------------------------------------------------ */
typedef struct {
    int key;
    rbnode node;
} rbtest_item;

/* ------------------------------------------------------------------
 * Comparison callbacks
 * ------------------------------------------------------------------ */
static int cmp_node(const rbnode* a, const rbnode* b)
{
    return rb_entry(a, rbtest_item, node)->key -
           rb_entry(b, rbtest_item, node)->key;
}

static int cmp_key(const void* key, const rbnode* b)
{
    return *(const int*)key - rb_entry(b, rbtest_item, node)->key;
}

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */
static void term_pass(const char* name)
{
    terminal_write_color("[PASS] ", to_vga_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_write(name);
    terminal_write("\n");
}

static void term_fail(const char* name)
{
    terminal_write_color("[FAIL] ", to_vga_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write(name);
    terminal_write("\n");
}

static void term_int(const char* prefix, int val)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%d\n", prefix, val);
    terminal_write(buf);
}

static void check_flush(void)
{
    if (terminal_get_row() >= 12) {
        timer_delay_ms(1500);
        terminal_flush(0);
    }
}

/* Count nodes in a tree */
static int tree_count(rbtree* tree)
{
    int c = 0;
    rbnode* n;
    rbtree_for_each(n, tree) c++;
    return c;
}

/* ==================================================================
 * Main test entry
 * ================================================================== */
void rbtree_test_main(void)
{
    terminal_flush(0);
    terminal_write("\n========== Red-Black Tree API Test Suite ==========\n\n");

    /* Use static storage — kernel stack is small */
    static rbtest_item items[8];
    rbtree* tree = rbtree_create();
    if (!tree) {
        terminal_write("FATAL: rbtree_create failed\n");
        test_finished_flag = 1;
        proc_exit(proc_get_pid());
        return;
    }

    /* ==============================================================
     * Test 1 — Empty tree
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 1] Empty tree: rbtree_first / rbtree_last / rbtree_search\n");
        int ok = (rbtree_first(tree) == 0) &&
                 (rbtree_last(tree) == 0);
        int key = 99;
        ok = ok && (rbtree_search(tree, &key, cmp_key) == 0);
        if (ok) term_pass("empty tree");
        else    term_fail("empty tree");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 2 — Insert 5 nodes, verify in-order
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 2] Insert 5 nodes (50,30,70,20,40)\n");
        int keys[] = {50, 30, 70, 20, 40};
        for (int i = 0; i < 5; i++) {
            items[i].key = keys[i];
            rbtree_insert(tree, &items[i].node, cmp_node);
        }

        /* Verify sorted order */
        int expected[] = {20, 30, 40, 50, 70};
        int i = 0;
        int ok = 1;
        rbnode* n;
        rbtree_for_each(n, tree) {
            if (i >= 5 || rb_entry(n, rbtest_item, node)->key != expected[i]) {
                ok = 0;
                break;
            }
            i++;
        }
        ok = ok && (i == 5);

        /* first / last */
        ok = ok && (rb_entry(rbtree_first(tree), rbtest_item, node)->key == 20);
        ok = ok && (rb_entry(rbtree_last(tree), rbtest_item, node)->key == 70);

        if (ok) term_pass("insert 5 nodes + inorder");
        else    term_fail("insert 5 nodes + inorder");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 3 — rbtree_search
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 3] rbtree_search existing / missing\n");
        int key30 = 30, key99 = 99;
        int ok = (rbtree_search(tree, &key30, cmp_key) != 0);
        ok = ok && (rbtree_search(tree, &key99, cmp_key) == 0);
        if (ok) term_pass("rbtree_search");
        else    term_fail("rbtree_search");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 4 — Delete leaf (40), verify
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 4] Delete leaf (40)\n");
        /* items[4] has key 40 */
        rbtree_delete(tree, &items[4].node);

        int key40 = 40;
        int ok = (rbtree_search(tree, &key40, cmp_key) == 0);
        ok = ok && (tree_count(tree) == 4);
        if (ok) term_pass("delete leaf");
        else    term_fail("delete leaf");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 5 — Delete node with two children (50, the root)
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 5] Delete root (50) — two children\n");
        rbtree_delete(tree, &items[0].node);

        int key50 = 50;
        int ok = (rbtree_search(tree, &key50, cmp_key) == 0);
        ok = ok && (tree_count(tree) == 3);
        /* Should still be sorted: 20, 30, 70 */
        int exp[] = {20, 30, 70};
        int i = 0;
        rbnode* n;
        rbtree_for_each(n, tree) {
            if (i >= 3 || rb_entry(n, rbtest_item, node)->key != exp[i]) {
                ok = 0;
                break;
            }
            i++;
        }
        ok = ok && (i == 3);
        if (ok) term_pass("delete two-children");
        else    term_fail("delete two-children");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 6 — Insert new nodes for later tests
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 6] Re-populate tree (60, 10, 80)\n");
        items[5].key = 60;
        items[6].key = 10;
        items[7].key = 80;
        rbtree_insert(tree, &items[5].node, cmp_node);
        rbtree_insert(tree, &items[6].node, cmp_node);
        rbtree_insert(tree, &items[7].node, cmp_node);

        int ok = (tree_count(tree) == 6);
        int exp[] = {10, 20, 30, 60, 70, 80};
        int i = 0;
        rbnode* n;
        rbtree_for_each(n, tree) {
            if (i >= 6 || rb_entry(n, rbtest_item, node)->key != exp[i]) {
                ok = 0;
                break;
            }
            i++;
        }
        if (ok) term_pass("re-populate");
        else    term_fail("re-populate");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 7 — rbtree_next / rbtree_prev full walk
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 7] rbtree_next forward / rbtree_prev backward\n");
        int ok = 1;
        int count = 0;
        rbnode* n = rbtree_first(tree);
        while (n) { count++; n = rbtree_next(tree, n); }
        ok = (count == 6);
        count = 0;
        n = rbtree_last(tree);
        while (n) { count++; n = rbtree_prev(tree, n); }
        ok = ok && (count == 6);
        if (ok) term_pass("rbtree_next / rbtree_prev");
        else    term_fail("rbtree_next / rbtree_prev");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 8 — rbtree_find_or_insert (existing)
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 8] rbtree_find_or_insert — existing key (30)\n");
        static rbtest_item dup;
        dup.key = 30;
        int key30 = 30;
        rbnode* existing = rbtree_find_or_insert(tree, &key30, &dup.node,
                                                  cmp_key, cmp_node);
        /* Should return the EXISTING node for key 30, not insert dup */
        int ok = (existing != 0);
        ok = ok && (existing != &dup.node);
        ok = ok && (tree_count(tree) == 6);  /* no new node added */
        if (ok) term_pass("find_or_insert(existing)");
        else    term_fail("find_or_insert(existing)");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 9 — rbtree_find_or_insert (new)
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 9] rbtree_find_or_insert — new key (55)\n");
        static rbtest_item new_item;
        new_item.key = 55;
        int key55 = 55;
        rbnode* existing = rbtree_find_or_insert(tree, &key55, &new_item.node,
                                                  cmp_key, cmp_node);
        /* Should return NULL (meaning inserted) */
        int ok = (existing == 0);
        ok = ok && (tree_count(tree) == 7);
        /* Verify key 55 is now findable */
        ok = ok && (rbtree_search(tree, &key55, cmp_key) == &new_item.node);
        if (ok) term_pass("find_or_insert(new)");
        else    term_fail("find_or_insert(new)");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 10 — rbtree_for_each_safe (erase while iterating)
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 10] rbtree_for_each_safe: remove keys > 50\n");
        rbnode* pos;
        rbnode* n;
        int removed = 0;
        rbtree_for_each_safe(pos, n, tree) {
            rbtest_item* item = rb_entry(pos, rbtest_item, node);
            if (item->key > 50) {
                rbtree_delete(tree, pos);
                removed++;
            }
        }
        /* Should have removed: 55, 60, 70, 80 = 4 nodes */
        int ok = (removed == 4);
        ok = ok && (tree_count(tree) == 3);  /* 10, 20, 30 remain */

        int exp[] = {10, 20, 30};
        int i = 0;
        rbnode* cur;
        rbtree_for_each(cur, tree) {
            if (i >= 3 || rb_entry(cur, rbtest_item, node)->key != exp[i]) {
                ok = 0;
                break;
            }
            i++;
        }
        if (ok) term_pass("rbtree_for_each_safe delete");
        else    term_fail("rbtree_for_each_safe delete");
        timer_delay_ms(800);
    }

    /* --------------------------------------------------------------
     * All done — signal the entry and exit this process
     * -------------------------------------------------------------- */
    rbtree_destroy(tree);
    terminal_write("\n========== Red-Black Tree Test Suite COMPLETE ==========\n");
    terminal_write("Returning to menu in 3 seconds...\n");
    timer_delay_ms(3000);
    test_finished_flag = 1;
    proc_exit(proc_get_pid());
}
