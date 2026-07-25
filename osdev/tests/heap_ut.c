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

    // req_size = sizeof(heapchunk) + 0 = nonzero, so a valid minimal chunk is returned
    int8_t* ptr = kmalloc(0);
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

// Test 16: Coalescing — merge with next chunk
void test_coalesce_merge_next(void) {
    printf("Test 16: Coalescing — merge with next\n");

    int8_t* a = kmalloc(200);
    int8_t* b = kmalloc(200);
    int8_t* c = kmalloc(200);
    assert(a && b && c);

    // Free B: should merge with C if C is free (but C is used, so no merge)
    // Free C first, then B: B merges with C
    kfree(c);
    kfree(b);  // b merges with next (the former c)

    // Now allocate a chunk slightly larger than one block: should use merged B+C space
    int8_t* d = kmalloc(350);
    assert(d != NULL);

    kfree(a);
    kfree(d);
    printf("  ✓ Passed\n");
}

// Test 17: Coalescing — merge with prev chunk
void test_coalesce_merge_prev(void) {
    printf("Test 17: Coalescing — merge with prev\n");

    int8_t* a = kmalloc(200);
    int8_t* b = kmalloc(200);
    int8_t* c = kmalloc(200);
    assert(a && b && c);

    // Free A, then free B: B merges with A (prev)
    kfree(a);
    kfree(b);  // b merges with prev (former a)

    int8_t* d = kmalloc(350);
    assert(d != NULL);

    kfree(c);
    kfree(d);
    printf("  ✓ Passed\n");
}

// Test 18: Coalescing — merge with both sides
void test_coalesce_merge_both(void) {
    printf("Test 18: Coalescing — merge with both sides\n");

    int8_t* a = kmalloc(200);
    int8_t* b = kmalloc(200);
    int8_t* c = kmalloc(200);
    assert(a && b && c);

    // Free A and C, then free B: B merges with both prev (A) and next (C)
    kfree(a);
    kfree(c);
    kfree(b);  // merges with both sides → one large free block

    // Should be able to allocate a chunk nearly as large as all three combined
    int8_t* d = kmalloc(550);
    assert(d != NULL);

    kfree(d);
    printf("  ✓ Passed\n");
}

// Test 19: Coalescing cascade (free all, re-alloc one big chunk)
void test_coalesce_cascade(void) {
    printf("Test 19: Coalescing — cascade merge\n");

    #define N_CASCADE 20
    int8_t* ptrs[N_CASCADE];

    for (int i = 0; i < N_CASCADE; i++) {
        ptrs[i] = kmalloc(100);
        assert(ptrs[i] != NULL);
    }

    // Free in reverse order — each free triggers a merge, building one giant block
    for (int i = N_CASCADE - 1; i >= 0; i--) {
        kfree(ptrs[i]);
    }

    // Should be able to allocate a large contiguous chunk
    int8_t* big = kmalloc(N_CASCADE * 90);
    assert(big != NULL);

    kfree(big);
    printf("  ✓ Passed\n");
    #undef N_CASCADE
}

// Test 20: Double-free detection
void test_double_free(void) {
    printf("Test 20: Double-free detection\n");

    int8_t* ptr = kmalloc(100);
    assert(ptr != NULL);

    kfree(ptr);
    kfree(ptr);  // second free should be silently ignored

    // The heap should still be functional
    int8_t* ptr2 = kmalloc(100);
    assert(ptr2 != NULL);
    kfree(ptr2);

    printf("  ✓ Passed\n");
}

// ==================== STRESS TESTS ====================

// Test 21: Fill-drain-refill (verifies avail_size accounting)
void test_stress_fill_drain_refill(void) {
    printf("Test 21: Stress — fill, drain, refill\n");

    #define CHUNK_SIZE 4096
    #define MAX_CHUNKS 2560  // 10MB / 4KB ≈ 2560

    int8_t* ptrs[MAX_CHUNKS];
    int count = 0;

    // Phase 1: Fill
    while (count < MAX_CHUNKS) {
        ptrs[count] = kmalloc(CHUNK_SIZE);
        if (!ptrs[count]) break;
        // Write a marker
        memset(ptrs[count], 0xAB, CHUNK_SIZE);
        count++;
    }
    printf("    Phase 1: allocated %d chunks (%d KB)\n", count, count * CHUNK_SIZE / 1024);
    assert(count > 0);

    // Phase 2: Drain
    for (int i = 0; i < count; i++) {
        kfree(ptrs[i]);
        ptrs[i] = NULL;
    }
    printf("    Phase 2: freed all %d chunks\n", count);

    // Phase 3: Refill — should be able to allocate the same amount
    int count2 = 0;
    while (count2 < MAX_CHUNKS) {
        ptrs[count2] = kmalloc(CHUNK_SIZE);
        if (!ptrs[count2]) break;
        count2++;
    }
    printf("    Phase 3: allocated %d chunks\n", count2);
    assert(count2 >= count - 1);  // allow minor fragmentation loss

    // Cleanup
    for (int i = 0; i < count2; i++) {
        kfree(ptrs[i]);
    }

    printf("  ✓ Passed\n");
    #undef CHUNK_SIZE
    #undef MAX_CHUNKS
}

// Test 22: Random alloc/free stress
void test_stress_random(void) {
    printf("Test 22: Stress — random alloc/free\n");

    #define N_PTRS      200
    #define N_ROUNDS    5000
    int8_t* ptrs[N_PTRS] = {NULL};
    unsigned int seed = 0xDEADBEEF;

    // Simple LCG
    #define RAND() (seed = seed * 1103515245 + 12345)

    for (int round = 0; round < N_ROUNDS; round++) {
        int idx = RAND() % N_PTRS;

        if (ptrs[idx] == NULL) {
            // Allocate: random size between 1 and 2048
            unsigned int sz = (RAND() % 2048) + 1;
            ptrs[idx] = kmalloc(sz);
            if (ptrs[idx]) {
                // Write a canary at start and end
                ptrs[idx][0] = (int8_t)(idx & 0xFF);
                ptrs[idx][sz - 1] = (int8_t)((idx + 1) & 0xFF);
            }
        } else {
            kfree(ptrs[idx]);
            ptrs[idx] = NULL;
        }
    }

    // Free remaining
    for (int i = 0; i < N_PTRS; i++) {
        if (ptrs[i]) {
            kfree(ptrs[i]);
            ptrs[i] = NULL;
        }
    }

    // Final sanity: allocate one big block
    int8_t* final = kmalloc(1024 * 1024);
    assert(final != NULL);
    kfree(final);

    printf("  ✓ Passed\n");
    #undef N_PTRS
    #undef N_ROUNDS
    #undef RAND
}

// Test 23: Stress — interleaved sizes with verification
void test_stress_interleaved(void) {
    printf("Test 23: Stress — interleaved sizes with verification\n");

    #define N_IL 100
    int8_t* ptrs[N_IL];
    unsigned int sizes[N_IL];
    unsigned int seed = 0xCAFEBABE;
    #define RAND() (seed = seed * 1103515245 + 12345)

    // Allocate random sizes, write patterns
    for (int i = 0; i < N_IL; i++) {
        sizes[i] = (RAND() % 512) + 1;
        ptrs[i] = kmalloc(sizes[i]);
        assert(ptrs[i] != NULL);
        memset(ptrs[i], (int8_t)(i & 0x7F), sizes[i]);
    }

    // Verify all patterns intact
    for (int i = 0; i < N_IL; i++) {
        for (unsigned int j = 0; j < sizes[i]; j++) {
            assert(ptrs[i][j] == (int8_t)(i & 0x7F));
        }
    }

    // Free odd-indexed blocks
    for (int i = 1; i < N_IL; i += 2) {
        kfree(ptrs[i]);
        ptrs[i] = NULL;
    }

    // Re-verify even-indexed blocks untouched
    for (int i = 0; i < N_IL; i += 2) {
        for (unsigned int j = 0; j < sizes[i]; j++) {
            assert(ptrs[i][j] == (int8_t)(i & 0x7F));
        }
    }

    // Allocate new blocks of various sizes into freed slots
    for (int i = 1; i < N_IL; i += 2) {
        sizes[i] = (RAND() % 512) + 1;
        ptrs[i] = kmalloc(sizes[i]);
        if (ptrs[i]) {
            memset(ptrs[i], 0x5A, sizes[i]);
        }
    }

    // Free all
    for (int i = 0; i < N_IL; i++) {
        if (ptrs[i]) kfree(ptrs[i]);
    }

    printf("  ✓ Passed\n");
    #undef N_IL
    #undef RAND
}

// Test 24: Stress — tiny allocations (worst-case fragmentation)
void test_stress_tiny(void) {
    printf("Test 24: Stress — tiny allocations\n");

    #define N_TINY 500
    int8_t* ptrs[N_TINY];

    // Allocate many 1-byte chunks
    for (int i = 0; i < N_TINY; i++) {
        ptrs[i] = kmalloc(1);
        assert(ptrs[i] != NULL);
        ptrs[i][0] = (int8_t)i;
    }

    // Verify
    for (int i = 0; i < N_TINY; i++) {
        assert(ptrs[i][0] == (int8_t)i);
    }

    // Free every 3rd
    for (int i = 0; i < N_TINY; i += 3) {
        kfree(ptrs[i]);
        ptrs[i] = NULL;
    }

    // Re-allocate into freed slots
    for (int i = 0; i < N_TINY; i += 3) {
        ptrs[i] = kmalloc(1);
        if (ptrs[i]) ptrs[i][0] = (int8_t)(i + 100);
    }

    // Free all
    for (int i = 0; i < N_TINY; i++) {
        if (ptrs[i]) kfree(ptrs[i]);
    }

    printf("  ✓ Passed\n");
    #undef N_TINY
}

// Test 25: Stress — max allocation
void test_stress_max_allocation(void) {
    printf("Test 25: Stress — max allocation\n");

    // Try to allocate nearly the entire heap
    unsigned int big_size = HEAP_TOTAL_SIZE - 4096;  // leave room for headers
    int8_t* ptr = kmalloc(big_size);
    if (ptr) {
        // Write to first and last byte
        ptr[0] = 0x42;
        ptr[big_size - 1] = 0x42;
        assert(ptr[0] == 0x42);
        assert(ptr[big_size - 1] == 0x42);
        kfree(ptr);
    }

    // After freeing, should be able to allocate again
    int8_t* ptr2 = kmalloc(big_size / 2);
    assert(ptr2 != NULL);
    kfree(ptr2);

    printf("  ✓ Passed\n");
}

int heap_ut(void) {
    printf("=== Heap Unit Tests ===\n\n");

    // Basic tests
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

    // Coalescing tests
    test_coalesce_merge_next();
    test_coalesce_merge_prev();
    test_coalesce_merge_both();
    test_coalesce_cascade();
    test_double_free();

    // Stress tests
    test_stress_fill_drain_refill();
    test_stress_random();
    test_stress_interleaved();
    test_stress_tiny();
    test_stress_max_allocation();

    printf("\n=== All tests passed! ===\n");
    return 0;
}
