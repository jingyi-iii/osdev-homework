/*******************************************************************************
 *                                                                             *
 *    Red-Black Tree API Test Suite                                            *
 *                                                                             *
 *    Tests the rbtree.h interface inside the kernel environment:              *
 *      - rb_insert / rb_insert_color                                          *
 *      - rb_find                                                              *
 *      - rb_first / rb_last / rb_next / rb_prev                               *
 *      - rb_erase                                                             *
 *      - rb_replace_node                                                      *
 *      - rb_find_or_insert                                                    *
 *      - rb_for_each / rb_for_each_safe                                       *
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
    rb_node node;
} rbtest_item;

/* ------------------------------------------------------------------
 * Comparison callbacks
 * ------------------------------------------------------------------ */
static int cmp_node(const rb_node *a, const rb_node *b)
{
    return rb_entry(a, rbtest_item, node)->key -
           rb_entry(b, rbtest_item, node)->key;
}

static int cmp_key(const void *key, const rb_node *b)
{
    return *(const int *)key - rb_entry(b, rbtest_item, node)->key;
}

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */
static void term_pass(const char *name)
{
    terminal_write_color("[PASS] ", to_vga_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_write(name);
    terminal_write("\n");
}

static void term_fail(const char *name)
{
    terminal_write_color("[FAIL] ", to_vga_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write(name);
    terminal_write("\n");
}

static void term_int(const char *prefix, int val)
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
static int tree_count(rb_root *root)
{
    int c = 0;
    rb_node *n;
    rb_for_each(n, root) c++;
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
    static rb_root root;
    root = RB_ROOT;

    /* ==============================================================
     * Test 1 — Empty tree
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 1] Empty tree: rb_first / rb_last / rb_find\n");
        int ok = (rb_first(&root) == NULL) &&
                 (rb_last(&root) == NULL);
        int key = 99;
        ok = ok && (rb_find(&key, &root, cmp_key) == NULL);
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
            rb_insert(&items[i].node, &root, cmp_node);
        }

        /* Verify sorted order */
        int expected[] = {20, 30, 40, 50, 70};
        int i = 0;
        int ok = 1;
        rb_node *n;
        rb_for_each(n, &root) {
            if (i >= 5 || rb_entry(n, rbtest_item, node)->key != expected[i]) {
                ok = 0;
                break;
            }
            i++;
        }
        ok = ok && (i == 5);

        /* first / last */
        ok = ok && (rb_entry(rb_first(&root), rbtest_item, node)->key == 20);
        ok = ok && (rb_entry(rb_last(&root), rbtest_item, node)->key == 70);

        if (ok) term_pass("insert 5 nodes + inorder");
        else    term_fail("insert 5 nodes + inorder");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 3 — rb_find
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 3] rb_find existing / missing\n");
        int key30 = 30, key99 = 99;
        int ok = (rb_find(&key30, &root, cmp_key) != NULL);
        ok = ok && (rb_find(&key99, &root, cmp_key) == NULL);
        if (ok) term_pass("rb_find");
        else    term_fail("rb_find");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 4 — Erase leaf (40), verify
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 4] Erase leaf (40)\n");
        /* items[4] has key 40 */
        rb_erase(&items[4].node, &root);

        int key40 = 40;
        int ok = (rb_find(&key40, &root, cmp_key) == NULL);
        ok = ok && (tree_count(&root) == 4);
        if (ok) term_pass("erase leaf");
        else    term_fail("erase leaf");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 5 — Erase node with two children (50, the root)
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 5] Erase root (50) — two children\n");
        rb_erase(&items[0].node, &root);

        int key50 = 50;
        int ok = (rb_find(&key50, &root, cmp_key) == NULL);
        ok = ok && (tree_count(&root) == 3);
        /* Should still be sorted: 20, 30, 70 */
        int exp[] = {20, 30, 70};
        int i = 0;
        rb_node *n;
        rb_for_each(n, &root) {
            if (i >= 3 || rb_entry(n, rbtest_item, node)->key != exp[i]) {
                ok = 0;
                break;
            }
            i++;
        }
        ok = ok && (i == 3);
        if (ok) term_pass("erase two-children");
        else    term_fail("erase two-children");
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
        rb_insert(&items[5].node, &root, cmp_node);
        rb_insert(&items[6].node, &root, cmp_node);
        rb_insert(&items[7].node, &root, cmp_node);

        int ok = (tree_count(&root) == 6);
        int exp[] = {10, 20, 30, 60, 70, 80};
        int i = 0;
        rb_node *n;
        rb_for_each(n, &root) {
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
     * Test 7 — rb_next / rb_prev full walk
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 7] rb_next forward / rb_prev backward\n");
        int ok = 1;
        int count = 0;
        rb_node *n = rb_first(&root);
        while (n) { count++; n = rb_next(n); }
        ok = (count == 6);
        count = 0;
        n = rb_last(&root);
        while (n) { count++; n = rb_prev(n); }
        ok = ok && (count == 6);
        if (ok) term_pass("rb_next / rb_prev");
        else    term_fail("rb_next / rb_prev");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 8 — rb_replace_node
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 8] rb_replace_node (replace key=20)\n");
        /* Replace items[3] (key=20) with a new node carrying key=25 */
        static rbtest_item replacement;
        replacement.key = 25;
        /* Save old children/parent — actually rb_replace_node handles it */
        rb_replace_node(&items[3].node, &replacement.node, &root);

        /* Old key 20 should be gone; new key 25 should be present */
        int key20 = 20, key25 = 25;
        int ok = (rb_find(&key20, &root, cmp_key) == NULL);
        ok = ok && (rb_find(&key25, &root, cmp_key) == &replacement.node);
        ok = ok && (tree_count(&root) == 6);
        if (ok) term_pass("rb_replace_node");
        else    term_fail("rb_replace_node");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 9 — rb_find_or_insert (existing)
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 9] rb_find_or_insert — existing key (30)\n");
        static rbtest_item dup;
        dup.key = 30;
        int key30 = 30;
        rb_node *existing = rb_find_or_insert(&key30, &dup.node, &root,
                                               cmp_key, cmp_node);
        /* Should return the EXISTING node for key 30, not insert dup */
        int ok = (existing != NULL);
        ok = ok && (existing != &dup.node);
        ok = ok && (tree_count(&root) == 6);  /* no new node added */
        if (ok) term_pass("find_or_insert(existing)");
        else    term_fail("find_or_insert(existing)");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 10 — rb_find_or_insert (new)
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 10] rb_find_or_insert — new key (55)\n");
        static rbtest_item new_item;
        new_item.key = 55;
        int key55 = 55;
        rb_node *existing = rb_find_or_insert(&key55, &new_item.node, &root,
                                               cmp_key, cmp_node);
        /* Should return NULL (meaning inserted) */
        int ok = (existing == NULL);
        ok = ok && (tree_count(&root) == 7);
        /* Verify key 55 is now findable */
        ok = ok && (rb_find(&key55, &root, cmp_key) == &new_item.node);
        if (ok) term_pass("find_or_insert(new)");
        else    term_fail("find_or_insert(new)");
        timer_delay_ms(800);
    }

    /* ==============================================================
     * Test 11 — rb_for_each_safe (erase while iterating)
     * ============================================================== */
    {
        check_flush();
        terminal_write("[TEST 11] rb_for_each_safe: remove keys > 50\n");
        rb_node *pos, *n;
        int removed = 0;
        rb_for_each_safe(pos, n, &root) {
            rbtest_item *item = rb_entry(pos, rbtest_item, node);
            if (item->key > 50) {
                rb_erase(pos, &root);
                removed++;
            }
        }
        /* Should have removed: 55, 60, 70, 80 = 4 nodes */
        int ok = (removed == 4);
        ok = ok && (tree_count(&root) == 3);  /* 10, 25, 30 remain */

        int exp[] = {10, 25, 30};
        int i = 0;
        rb_node *cur;
        rb_for_each(cur, &root) {
            if (i >= 3 || rb_entry(cur, rbtest_item, node)->key != exp[i]) {
                ok = 0;
                break;
            }
            i++;
        }
        if (ok) term_pass("rb_for_each_safe erase");
        else    term_fail("rb_for_each_safe erase");
        timer_delay_ms(800);
    }

    /* --------------------------------------------------------------
     * All done — signal the entry and exit this process
     * -------------------------------------------------------------- */
    terminal_write("\n========== Red-Black Tree Test Suite COMPLETE ==========\n");
    terminal_write("Returning to menu in 3 seconds...\n");
    timer_delay_ms(3000);
    test_finished_flag = 1;
    proc_exit(proc_get_pid());
}
