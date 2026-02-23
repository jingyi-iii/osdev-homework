#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include "lock.h"

// Test framework macros
#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return -1; \
    } \
} while(0)

#define TEST_PASS 0
#define TEST_FAIL -1

// Test counters
static int total_tests = 0;
static int passed_tests = 0;

// Thread test data
typedef struct {
    spinlock_dev *lock;
    int *counter;
    int iterations;
    int thread_id;
} thread_data_t;

// Helper function to run a test
#define RUN_TEST(test) do { \
    total_tests++; \
    printf("Running %s... ", #test); \
    fflush(stdout); \
    if (test() == TEST_PASS) { \
        printf("PASSED\n"); \
        passed_tests++; \
    } else { \
        printf("FAILED\n"); \
    } \
} while(0)

// Test 1: Basic allocation and free
int test_basic_allocation_free() {
    spinlock_dev *lock = NULL;

    // Test allocation
    int result = spinlock_alloc_dev(&lock);
    TEST_ASSERT(result == 0);
    TEST_ASSERT(lock != NULL);
    TEST_ASSERT(lock->state == 0);
    TEST_ASSERT(lock->allocated == 1);
    TEST_ASSERT(lock->lock != NULL);
    TEST_ASSERT(lock->trylock != NULL);
    TEST_ASSERT(lock->unlock != NULL);

    // Test free
    result = spinlock_free_dev(lock);
    TEST_ASSERT(result == 0);
    TEST_ASSERT(lock->state == 0);
    TEST_ASSERT(lock->allocated == 0);
    TEST_ASSERT(lock->lock == NULL);
    TEST_ASSERT(lock->trylock == NULL);
    TEST_ASSERT(lock->unlock == NULL);

    return TEST_PASS;
}

// Test 2: Multiple allocations
int test_multiple_allocations() {
    spinlock_dev *locks[10] = {NULL};
    int i;

    // Allocate multiple locks
    for (i = 0; i < 10; i++) {
        TEST_ASSERT(spinlock_alloc_dev(&locks[i]) == 0);
        TEST_ASSERT(locks[i] != NULL);
    }

    // Verify they're all different
    for (i = 0; i < 10; i++) {
        int j;
        for (j = i + 1; j < 10; j++) {
            TEST_ASSERT(locks[i] != locks[j]);
        }
    }

    // Free all locks
    for (i = 0; i < 10; i++) {
        TEST_ASSERT(spinlock_free_dev(locks[i]) == 0);
    }

    return TEST_PASS;
}

// Test 3: Allocate maximum locks
int test_max_allocations() {
    spinlock_dev *locks[1025] = {NULL};
    int i;

    printf("\n  Testing max allocations (1024)... ");
    fflush(stdout);

    // Allocate up to maximum
    for (i = 0; i < 1024; i++) {
        int result = spinlock_alloc_dev(&locks[i]);
        if (result != 0) {
            printf("Failed at allocation %d\n", i);
            TEST_ASSERT(result == 0);
        }
        TEST_ASSERT(locks[i] != NULL);
    }

    // Next allocation should fail
    spinlock_dev *extra_lock = NULL;
    TEST_ASSERT(spinlock_alloc_dev(&extra_lock) == -1);
    TEST_ASSERT(extra_lock == NULL);

    // Free all
    for (i = 0; i < 1024; i++) {
        TEST_ASSERT(spinlock_free_dev(locks[i]) == 0);
    }

    return TEST_PASS;
}

// Test 4: Basic lock/unlock operations
int test_basic_lock_unlock() {
    spinlock_dev *lock = NULL;

    TEST_ASSERT(spinlock_alloc_dev(&lock) == 0);

    // Test lock
    TEST_ASSERT(lock->lock(lock) == 0);
    TEST_ASSERT(lock->state == 1);

    // Test unlock
    TEST_ASSERT(lock->unlock(lock) == 0);
    TEST_ASSERT(lock->state == 0);

    TEST_ASSERT(spinlock_free_dev(lock) == 0);

    return TEST_PASS;
}

// Test 5: Trylock operations
int test_trylock() {
    spinlock_dev *lock = NULL;

    TEST_ASSERT(spinlock_alloc_dev(&lock) == 0);

    // Trylock when unlocked
    int result = lock->trylock(lock);
    TEST_ASSERT(result != 0); // Should return non-zero (true)
    TEST_ASSERT(lock->state == 1);

    // Trylock when locked
    result = lock->trylock(lock);
    TEST_ASSERT(result == 0); // Should return 0 (false)
    TEST_ASSERT(lock->state == 1);

    // Unlock
    TEST_ASSERT(lock->unlock(lock) == 0);
    TEST_ASSERT(lock->state == 0);

    TEST_ASSERT(spinlock_free_dev(lock) == 0);

    return TEST_PASS;
}

// Thread function for concurrent testing
void* thread_increment_counter(void *arg) {
    thread_data_t *data = (thread_data_t*)arg;

    for (int i = 0; i < data->iterations; i++) {
        data->lock->lock(data->lock);
        (*data->counter)++;
        data->lock->unlock(data->lock);
    }

    return NULL;
}

// Test 6: Concurrent lock protection
int test_concurrent_protection() {
    spinlock_dev *lock = NULL;
    pthread_t threads[10];
    thread_data_t thread_data[10];
    int counter = 0;
    int iterations = 10000;
    int num_threads = 10;

    printf("\n  Testing concurrent protection (%d threads, %d iterations each)... ",
           num_threads, iterations);
    fflush(stdout);

    TEST_ASSERT(spinlock_alloc_dev(&lock) == 0);

    // Create threads
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].lock = lock;
        thread_data[i].counter = &counter;
        thread_data[i].iterations = iterations;
        thread_data[i].thread_id = i;

        pthread_create(&threads[i], NULL, thread_increment_counter, &thread_data[i]);
    }

    // Wait for threads to complete
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // Verify counter value
    TEST_ASSERT(counter == iterations * num_threads);

    TEST_ASSERT(spinlock_free_dev(lock) == 0);

    return TEST_PASS;
}

// Thread function for testing trylock
void* thread_trylock_test(void *arg) {
    thread_data_t *data = (thread_data_t*)arg;
    int local_count = 0;

    for (int i = 0; i < data->iterations; i++) {
        if (data->lock->trylock(data->lock)) {
            (*data->counter)++;
            local_count++;
            data->lock->unlock(data->lock);
        }
        // Small delay to increase chance of contention
        for (int j = 0; j < 10; j++) {
            __asm__ __volatile__("pause");
        }
    }

    return (void*)(long)local_count;
}

// Test 7: Trylock concurrent behavior
int test_trylock_concurrent() {
    spinlock_dev *lock = NULL;
    pthread_t threads[10];
    thread_data_t thread_data[10];
    int counter = 0;
    int iterations = 1000;
    int num_threads = 10;
    int total_acquired = 0;

    printf("\n  Testing trylock concurrent (%d threads, %d iterations each)... ",
           num_threads, iterations);
    fflush(stdout);

    TEST_ASSERT(spinlock_alloc_dev(&lock) == 0);

    // Create threads
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].lock = lock;
        thread_data[i].counter = &counter;
        thread_data[i].iterations = iterations;
        thread_data[i].thread_id = i;

        pthread_create(&threads[i], NULL, thread_trylock_test, &thread_data[i]);
    }

    // Wait for threads and collect results
    for (int i = 0; i < num_threads; i++) {
        void *result;
        pthread_join(threads[i], &result);
        total_acquired += (int)(long)result;
    }

    // Total acquired should equal final counter value
    TEST_ASSERT(counter == total_acquired);
    TEST_ASSERT(counter <= iterations * num_threads);
    TEST_ASSERT(counter > 0); // At least some locks should be acquired

    TEST_ASSERT(spinlock_free_dev(lock) == 0);

    return TEST_PASS;
}

// Test 8: Null parameter handling
int test_null_handling() {
    spinlock_dev *lock = NULL;

    // Test alloc with NULL out_dev
    TEST_ASSERT(spinlock_alloc_dev(NULL) == -1);

    // Test free with NULL
    TEST_ASSERT(spinlock_free_dev(NULL) == -1);

    // Test with valid allocation
    TEST_ASSERT(spinlock_alloc_dev(&lock) == 0);

    // Test function pointers with NULL
    TEST_ASSERT(lock->lock(NULL) == -1); // Should return error
    TEST_ASSERT(lock->trylock(NULL) == 0); // Should return false
    TEST_ASSERT(lock->unlock(NULL) == -1); // Should return error

    TEST_ASSERT(spinlock_free_dev(lock) == 0);

    return TEST_PASS;
}

// Test 9: Reuse after free
int test_reuse_after_free() {
    spinlock_dev *lock1 = NULL;
    spinlock_dev *lock2 = NULL;

    // Allocate first lock
    TEST_ASSERT(spinlock_alloc_dev(&lock1) == 0);
    void* addr1 = (void*)lock1;

    // Free it
    TEST_ASSERT(spinlock_free_dev(lock1) == 0);

    // Allocate again - should get the same address
    TEST_ASSERT(spinlock_alloc_dev(&lock2) == 0);
    void* addr2 = (void*)lock2;
    TEST_ASSERT(addr1 == addr2);

    // Verify it's properly reinitialized
    TEST_ASSERT(lock2->state == 0);
    TEST_ASSERT(lock2->allocated == 1);
    TEST_ASSERT(lock2->lock != NULL);
    TEST_ASSERT(lock2->trylock != NULL);
    TEST_ASSERT(lock2->unlock != NULL);

    TEST_ASSERT(spinlock_free_dev(lock2) == 0);

    return TEST_PASS;
}

// Test 10: Memory barrier macros (compile-time check)
int test_memory_barriers() {
    // Just verify the macros compile and don't crash
    volatile int test_var = 0;

    barrier();
    test_var++;

    mb();
    test_var++;

    rmb();
    test_var++;

    wmb();
    test_var++;

    // If we got here, compilation succeeded
    return TEST_PASS;
}

int lock_ut() {
    printf("=== Spinlock Unit Tests ===\n\n");

    // Run all tests
    RUN_TEST(test_basic_allocation_free);
    RUN_TEST(test_multiple_allocations);
    RUN_TEST(test_max_allocations);
    RUN_TEST(test_basic_lock_unlock);
    RUN_TEST(test_trylock);
    RUN_TEST(test_concurrent_protection);
    RUN_TEST(test_trylock_concurrent);
    RUN_TEST(test_null_handling);
    RUN_TEST(test_reuse_after_free);
    RUN_TEST(test_memory_barriers);

    // Summary
    printf("\n=== Test Summary ===\n");
    printf("Total tests: %d\n", total_tests);
    printf("Passed: %d\n", passed_tests);
    printf("Failed: %d\n", total_tests - passed_tests);

    return (passed_tests == total_tests) ? 0 : 1;
}