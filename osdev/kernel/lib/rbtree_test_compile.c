#include "lib/rbtree.h"

/* Dummy struct embedding rb_node */
struct my_node {
    int key;
    rb_node rb;
};

static int my_cmp(const rb_node *a, const rb_node *b)
{
    struct my_node *ma = rb_entry(a, struct my_node, rb);
    struct my_node *mb = rb_entry(b, struct my_node, rb);
    return ma->key - mb->key;
}

void test_compile(void)
{
    rb_root root = RB_ROOT;
    struct my_node n;
}
