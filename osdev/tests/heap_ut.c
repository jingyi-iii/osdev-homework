#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "heap.h"

#define HEAP_TOTAL_SIZE         (1024 * 1024 * 10)

// Test 1: Basic allocation
void test_basic_kmalloc(void) {
    printf("Test 1: Basic kmalloc allocation\n");

    int8_t* ptr = kmalloc(100);
    assert(ptr != NULL);

    // Verify memory is zeroed
    for (int i = 0; i < 100; i++) {
        assert(ptr[i] == 0);
    }

    kfree(ptr);
    printf("  ✓ Passed\n");
}

// Test 2: Multiple allocations
void test_multiple_kmalloc(void) {
    printf("Test 2: Multiple kmalloc allocations\n");

    int8_t* ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(100 + i * 10);
        assert(ptrs[i] != NULL);
    }

    for (int i = 0; i < 10; i++) {
        kfree(ptrs[i]);
    }
    printf("  ✓ Passed\n");
}

// Test 3: Allocation and free cycle
void test_alloc_free_cycle(void) {
    printf("Test 3: Allocation and free cycle\n");

    int8_t* ptr1 = kmalloc(500);
    assert(ptr1 != NULL);

    kfree(ptr1);

    // Allocate again - should reuse freed space
    int8_t* ptr2 = kmalloc(500);
    assert(ptr2 != NULL);

    kfree(ptr2);
    printf("  ✓ Passed\n");
}

// Test 4: NULL pointer free
void test_null_free(void) {
    printf("Test 4: NULL pointer free handling\n");

    // Should not crash
    kfree(NULL);
    printf("  ✓ Passed\n");
}

// Test 5: Zero size allocation
void test_zero_allocation(void) {
    printf("Test 5: Zero size allocation\n");

    // Note: The heap implementation adds sizeof(heapchunk_t) to alloc_size,
    // so even zero-size requests will return a valid chunk
    int8_t* ptr = kmalloc(0);
    // Zero size allocation returns a valid (minimal) pointer
    assert(ptr != NULL);

    kfree(ptr);
    printf("  ✓ Passed\n");
}

// Test 6: Large allocation
void test_large_allocation(void) {
    printf("Test 6: Large allocation\n");

    // Allocate a large chunk
    int8_t* ptr = kmalloc(1024 * 1024);  // 1MB
    assert(ptr != NULL);

    // Verify zeroed
    for (int i = 0; i < 1000; i++) {
        assert(ptr[i] == 0);
    }

    kfree(ptr);
    printf("  ✓ Passed\n");
}

// Test 7: Memory write and read
void test_memory_write_read(void) {
    printf("Test 7: Memory write and read\n");

    int8_t* ptr = kmalloc(256);
    assert(ptr != NULL);

    // Write pattern
    for (int i = 0; i < 256; i++) {
        ptr[i] = (int8_t)i;
    }

    // Verify pattern
    for (int i = 0; i < 256; i++) {
        assert(ptr[i] == (int8_t)i);
    }

    kfree(ptr);
    printf("  ✓ Passed\n");
}

// Test 8: Fragmentation test
void test_fragmentation(void) {
    printf("Test 8: Fragmentation test\n");

    int8_t* ptrs[20];

    // Allocate many small chunks
    for (int i = 0; i < 20; i++) {
        ptrs[i] = kmalloc(100);
        assert(ptrs[i] != NULL);
    }

    // Free every other chunk
    for (int i = 0; i < 20; i += 2) {
        kfree(ptrs[i]);
        ptrs[i] = NULL;
    }

    // Allocate again - should use freed space
    for (int i = 0; i < 20; i += 2) {
        ptrs[i] = kmalloc(100);
        assert(ptrs[i] != NULL);
    }

    // Free all
    for (int i = 0; i < 20; i++) {
        if (ptrs[i]) {
            kfree(ptrs[i]);
        }
    }
    printf("  ✓ Passed\n");
}

// Test 9: Various allocation sizes
void test_various_sizes(void) {
    printf("Test 9: Various allocation sizes\n");

    size_t sizes[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 4096, 8192};
    int8_t* ptrs[sizeof(sizes)/sizeof(sizes[0])];

    for (int i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        ptrs[i] = kmalloc(sizes[i]);
        assert(ptrs[i] != NULL);
        // Verify zeroed
        assert(ptrs[i][0] == 0);
    }

    for (int i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        kfree(ptrs[i]);
    }
    printf("  ✓ Passed\n");
}

// Test 10: Stress test with many allocations
void test_stress_allocation(void) {
    printf("Test 10: Stress test with many allocations\n");

    int8_t* ptrs[100];
    int allocated = 0;

    // Allocate until we can't anymore or reach limit
    for (int i = 0; i < 100; i++) {
        ptrs[i] = kmalloc(10000);  // 10KB each
        if (ptrs[i] != NULL) {
            allocated++;
        }
    }

    printf("    Allocated %d/100 chunks\n", allocated);
    assert(allocated > 0);  // Should be able to allocate at least some

    // Free all
    for (int i = 0; i < 100; i++) {
        if (ptrs[i] != NULL) {
            kfree(ptrs[i]);
        }
    }
    printf("  ✓ Passed\n");
}

// Test 11: Reuse freed memory
void test_memory_reuse(void) {
    printf("Test 11: Memory reuse after free\n");

    int8_t* ptr1 = kmalloc(1000);
    assert(ptr1 != NULL);

    // Write to memory
    memset(ptr1, 0xAA, 1000);

    kfree(ptr1);

    // Allocate same size
    int8_t* ptr2 = kmalloc(1000);
    assert(ptr2 != NULL);

    // Memory should be zeroed again
    for (int i = 0; i < 1000; i++) {
        assert(ptr2[i] == 0);
    }

    kfree(ptr2);
    printf("  ✓ Passed\n");
}

// Test 12: Allocation order preservation
void test_allocation_order(void) {
    printf("Test 12: Allocation order preservation\n");

    int8_t* ptr1 = kmalloc(100);
    int8_t* ptr2 = kmalloc(200);
    int8_t* ptr3 = kmalloc(300);

    assert(ptr1 != NULL);
    assert(ptr2 != NULL);
    assert(ptr3 != NULL);

    // Free in different order
    kfree(ptr2);
    kfree(ptr1);
    kfree(ptr3);

    printf("  ✓ Passed\n");
}

// Test 13: Boundary size allocations
void test_boundary_sizes(void) {
    printf("Test 13: Boundary size allocations\n");

    // Test sizes around typical boundary values
    size_t boundary_sizes[] = {
        1, 2, 3, 4, 5, 6, 7, 8,
        15, 16, 17,
        31, 32, 33,
        63, 64, 65,
        127, 128, 129
    };

    int8_t* ptrs[sizeof(boundary_sizes)/sizeof(boundary_sizes[0])];

    for (int i = 0; i < sizeof(boundary_sizes)/sizeof(boundary_sizes[0]); i++) {
        ptrs[i] = kmalloc(boundary_sizes[i]);
        assert(ptrs[i] != NULL);
    }

    for (int i = 0; i < sizeof(boundary_sizes)/sizeof(boundary_sizes[0]); i++) {
        kfree(ptrs[i]);
    }
    printf("  ✓ Passed\n");
}

// Test 14: Interleaved allocation and freeing
void test_interleaved_alloc_free(void) {
    printf("Test 14: Interleaved allocation and freeing\n");

    int8_t* ptr_a = kmalloc(500);
    assert(ptr_a != NULL);

    int8_t* ptr_b = kmalloc(500);
    assert(ptr_b != NULL);

    kfree(ptr_a);

    int8_t* ptr_c = kmalloc(500);
    assert(ptr_c != NULL);

    kfree(ptr_b);

    int8_t* ptr_d = kmalloc(500);
    assert(ptr_d != NULL);

    kfree(ptr_c);
    kfree(ptr_d);

    printf("  ✓ Passed\n");
}

// Test 15: Consecutive same-size allocations
void test_consecutive_same_size(void) {
    printf("Test 15: Consecutive same-size allocations\n");

    int8_t* ptrs[50];

    // Allocate 50 chunks of same size
    for (int i = 0; i < 50; i++) {
        ptrs[i] = kmalloc(256);
        assert(ptrs[i] != NULL);
    }

    // Write unique patterns
    for (int i = 0; i < 50; i++) {
        memset(ptrs[i], i, 256);
    }

    // Verify patterns
    for (int i = 0; i < 50; i++) {
        for (int j = 0; j < 256; j++) {
            assert(ptrs[i][j] == i);
        }
    }

    // Free all
    for (int i = 0; i < 50; i++) {
        kfree(ptrs[i]);
    }
    printf("  ✓ Passed\n");
}

int heap_ut(void) {
    printf("=== Heap Unit Tests ===\n\n");

    test_basic_kmalloc();
    test_multiple_kmalloc();
    test_alloc_free_cycle();
    test_null_free();
    test_zero_allocation();
    test_large_allocation();
    test_memory_write_read();
    test_fragmentation();
    test_various_sizes();
    test_stress_allocation();
    test_memory_reuse();
    test_allocation_order();
    test_boundary_sizes();
    test_interleaved_alloc_free();
    test_consecutive_same_size();

    printf("\n=== All tests passed! ===\n");
    return 0;
}
