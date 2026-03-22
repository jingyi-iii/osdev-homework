#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "list.h"

// Container structure for testing list_entry macro
typedef struct {
    int value;
    list_node node;
} test_entry;

// Test 1: Basic list initialization
void test_list_init(void) {
    printf("Test 1: Basic list initialization\n");

    list_node head;
    list_init(&head);

    assert(head.prev == &head);
    assert(head.next == &head);

    printf("  ✓ Passed\n");
}

// Test 2: LIST_HEAD macro
void test_list_head_macro(void) {
    printf("Test 2: LIST_HEAD macro\n");

    LIST_HEAD(head);

    assert(head.prev == &head);
    assert(head.next == &head);

    printf("  ✓ Passed\n");
}

// Test 3: Single node addition
void test_single_add(void) {
    printf("Test 3: Single node addition\n");

    LIST_HEAD(head);
    list_node node;
    list_init(&node);

    list_add(&node, &head);

    assert(head.next == &node);
    assert(head.prev == &node);
    assert(node.prev == &head);
    assert(node.next == &head);

    printf("  ✓ Passed\n");
}

// Test 4: Multiple nodes addition (FIFO order)
void test_multiple_add(void) {
    printf("Test 4: Multiple nodes addition\n");

    LIST_HEAD(head);
    list_node nodes[5];

    // Add 5 nodes
    for (int i = 0; i < 5; i++) {
        list_init(&nodes[i]);
        list_add(&nodes[i], &head);
    }

    // Verify order (LIFO - last added is first)
    list_node* current = head.next;
    for (int i = 4; i >= 0; i--) {
        assert(current == &nodes[i]);
        current = current->next;
    }
    assert(current == &head);

    printf("  ✓ Passed\n");
}

// Test 5: Node deletion
void test_node_del(void) {
    printf("Test 5: Node deletion\n");

    LIST_HEAD(head);
    list_node nodes[3];

    // Add 3 nodes
    for (int i = 0; i < 3; i++) {
        list_init(&nodes[i]);
        list_add(&nodes[i], &head);
    }

    // Delete middle node (nodes[1], which is actually at position 2 due to LIFO)
    list_del(&nodes[1]);

    // Verify node[1] is detached
    assert(nodes[1].prev == &nodes[1]);
    assert(nodes[1].next == &nodes[1]);

    // Verify list integrity - should have nodes[2], nodes[0]
    assert(head.next == &nodes[2]);
    assert(nodes[2].next == &nodes[0]);
    assert(nodes[0].next == &head);
    assert(head.prev == &nodes[0]);

    printf("  ✓ Passed\n");
}

// Test 6: Delete all nodes
void test_del_all_nodes(void) {
    printf("Test 6: Delete all nodes\n");

    LIST_HEAD(head);
    list_node nodes[3];

    // Add 3 nodes
    for (int i = 0; i < 3; i++) {
        list_init(&nodes[i]);
        list_add(&nodes[i], &head);
    }

    // Delete all nodes
    list_del(&nodes[0]);
    list_del(&nodes[1]);
    list_del(&nodes[2]);

    // Head should be back to initial state
    assert(head.next == &head);
    assert(head.prev == &head);

    printf("  ✓ Passed\n");
}

// Test 7: list_entry macro
void test_list_entry(void) {
    printf("Test 7: list_entry macro\n");

    test_entry entries[3];
    LIST_HEAD(head);

    // Initialize and add entries
    for (int i = 0; i < 3; i++) {
        entries[i].value = i + 1;
        list_init(&entries[i].node);
        list_add(&entries[i].node, &head);
    }

    // Retrieve entries using list_entry (LIFO order: 3, 2, 1)
    list_node* pos;
    int expected = 3;
    list_for_each(pos, &head) {
        test_entry* entry = list_entry(pos, test_entry, node);
        assert(entry->value == expected);
        expected--;
    }

    printf("  ✓ Passed\n");
}

// Test 8: list_for_each iteration
void test_list_for_each(void) {
    printf("Test 8: list_for_each iteration\n");

    test_entry entries[5];
    LIST_HEAD(head);
    int count = 0;

    // Add 5 entries
    for (int i = 0; i < 5; i++) {
        entries[i].value = i;
        list_init(&entries[i].node);
        list_add(&entries[i].node, &head);
    }

    // Iterate and count
    list_node* pos;
    list_for_each(pos, &head) {
        count++;
    }
    assert(count == 5);

    // Verify values (LIFO order)
    int expected = 4;
    list_for_each(pos, &head) {
        test_entry* entry = list_entry(pos, test_entry, node);
        assert(entry->value == expected);
        expected--;
    }

    printf("  ✓ Passed\n");
}

// Test 9: NULL pointer handling for list_add
void test_add_null_handling(void) {
    printf("Test 9: NULL pointer handling for list_add\n");

    LIST_HEAD(head);
    list_node node;
    list_init(&node);

    // These should not crash
    list_add(NULL, &head);
    list_add(&node, NULL);
    list_add(NULL, NULL);

    // List should remain unchanged
    assert(head.next == &head);
    assert(head.prev == &head);

    printf("  ✓ Passed\n");
}

// Test 10: NULL pointer handling for list_del
void test_del_null_handling(void) {
    printf("Test 10: NULL pointer handling for list_del\n");

    // This should not crash
    list_del(NULL);

    printf("  ✓ Passed\n");
}

// Test 11: Add and delete in sequence
void test_add_del_sequence(void) {
    printf("Test 11: Add and delete in sequence\n");

    LIST_HEAD(head);
    list_node nodes[4];

    // Add, delete, add again pattern
    for (int i = 0; i < 4; i++) {
        list_init(&nodes[i]);
        list_add(&nodes[i], &head);
    }

    // Delete first two (which are nodes[3] and nodes[2] due to LIFO)
    list_del(&nodes[3]);
    list_del(&nodes[2]);

    // Add two more
    list_node nodes2[2];
    for (int i = 0; i < 2; i++) {
        list_init(&nodes2[i]);
        list_add(&nodes2[i], &head);
    }

    // Should have: nodes2[1], nodes2[0], nodes[1], nodes[0]
    int count = 0;
    list_node* pos;
    list_for_each(pos, &head) {
        count++;
    }
    assert(count == 4);

    printf("  ✓ Passed\n");
}

// Test 12: Empty list iteration
void test_empty_list_iteration(void) {
    printf("Test 12: Empty list iteration\n");

    LIST_HEAD(head);
    int count = 0;

    list_node* pos;
    list_for_each(pos, &head) {
        count++;
    }

    assert(count == 0);

    printf("  ✓ Passed\n");
}

// Test 13: Re-add deleted node
void test_readd_deleted_node(void) {
    printf("Test 13: Re-add deleted node\n");

    LIST_HEAD(head);
    list_node node;
    list_init(&node);

    // Add, delete, and re-add same node
    list_add(&node, &head);
    list_del(&node);
    list_add(&node, &head);

    // Should work correctly
    assert(head.next == &node);
    assert(head.prev == &node);
    assert(node.prev == &head);
    assert(node.next == &head);

    printf("  ✓ Passed\n");
}

// Test 14: Complex container iteration
void test_complex_container_iteration(void) {
    printf("Test 14: Complex container iteration\n");

    typedef struct {
        int id;
        char name[16];
        list_node node;
    } complex_entry;

    complex_entry entries[3] = {
        {1, "first", {NULL, NULL}},
        {2, "second", {NULL, NULL}},
        {3, "third", {NULL, NULL}}
    };

    LIST_HEAD(head);

    // Add entries
    for (int i = 0; i < 3; i++) {
        list_init(&entries[i].node);
        list_add(&entries[i].node, &head);
    }

    // Iterate and verify
    int expected_id = 3;
    list_node* pos;
    list_for_each(pos, &head) {
        complex_entry* entry = list_entry(pos, complex_entry, node);
        assert(entry->id == expected_id);
        expected_id--;
    }

    printf("  ✓ Passed\n");
}

// Test 15: Delete head node behavior (should not happen, but test safety)
void test_delete_from_empty(void) {
    printf("Test 15: Delete from empty list\n");

    LIST_HEAD(head);

    // Try to delete head itself (should reinitialize it)
    list_del(&head);

    // Head should be reinitialized
    assert(head.next == &head);
    assert(head.prev == &head);

    printf("  ✓ Passed\n");
}

int list_ut(void) {
    printf("=== List Unit Tests ===\n\n");

    test_list_init();
    test_list_head_macro();
    test_single_add();
    test_multiple_add();
    test_node_del();
    test_del_all_nodes();
    test_list_entry();
    test_list_for_each();
    test_add_null_handling();
    test_del_null_handling();
    test_add_del_sequence();
    test_empty_list_iteration();
    test_readd_deleted_node();
    test_complex_container_iteration();
    test_delete_from_empty();

    printf("\n=== All tests passed! ===\n");
    return 0;
}
