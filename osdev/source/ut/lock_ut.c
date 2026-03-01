#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include "lockmgr.h"

#define NUM_THREADS 10
#define NUM_ITERATIONS 10000
#define NUM_LOCKS 50
#define SPIN_LOCK_MAX_COUNT (1024)

// Test 1: Basic allocation and release
void test_basic_allocation(void) {
    printf("Test 1: Basic allocation and release\n");

    spinlock_dev *lock = NULL;
    int ret = SPIN_LOCK_INIT(lock);

    assert(ret == 0);
    assert(lock != NULL);
    assert(lock->allocated == 1);
    assert(lock->state == 0);
    assert(lock->lock != NULL);
    assert(lock->trylock != NULL);
    assert(lock->unlock != NULL);

    SPIN_LOCK_RELEASE(lock);
    assert(lock == NULL);

    printf("  ✓ Passed\n");
}

// Test 2: Multiple allocations
void test_multiple_allocations(void) {
    printf("Test 2: Multiple allocations\n");

    spinlock_dev *locks[NUM_LOCKS];

    for (int i = 0; i < NUM_LOCKS; i++) {
        locks[i] = NULL;
        int ret = SPIN_LOCK_INIT(locks[i]);
        assert(ret == 0);
        assert(locks[i] != NULL);
        assert(locks[i]->allocated == 1);
    }

    for (int i = 0; i < NUM_LOCKS; i++) {
        SPIN_LOCK_RELEASE(locks[i]);
        assert(locks[i] == NULL);
    }

    printf("  ✓ Passed\n");
}

// Test 3: Lock and unlock operations
void test_lock_unlock(void) {
    printf("Test 3: Lock and unlock operations\n");

    spinlock_dev *lock = NULL;
    SPIN_LOCK_INIT(lock);
    assert(lock != NULL);

    // Test lock
    int ret = lock->lock(lock);
    assert(ret == 0);
    assert(lock->state == 1);

    // Test unlock
    ret = lock->unlock(lock);
    assert(ret == 0);
    assert(lock->state == 0);

    SPIN_LOCK_RELEASE(lock);
    printf("  ✓ Passed\n");
}

// Test 4: Trylock operations
void test_trylock(void) {
    printf("Test 4: Trylock operations\n");

    spinlock_dev *lock = NULL;
    SPIN_LOCK_INIT(lock);
    assert(lock != NULL);

    // Initial trylock should succeed
    int ret = lock->trylock(lock);
    assert(ret == 0);  // Should return 0 on success
    assert(lock->state == 1);

    // Second trylock should fail
    ret = lock->trylock(lock);
    assert(ret == 1);  // Should return 1 when lock is held
    assert(lock->state == 1);

    // Unlock and try again
    lock->unlock(lock);
    ret = lock->trylock(lock);
    assert(ret == 0);
    assert(lock->state == 1);

    lock->unlock(lock);
    SPIN_LOCK_RELEASE(lock);
    printf("  ✓ Passed\n");
}

// Test 5: NULL pointer handling
void test_null_handling(void) {
    printf("Test 5: NULL pointer handling\n");

    spinlock_dev *lock = NULL;

    // Test lock functions with NULL
    assert(lock == NULL);

    // These should not crash
    SPIN_LOCK_RELEASE(lock);

    // Allocate and then free with NULL check
    SPIN_LOCK_INIT(lock);
    assert(lock != NULL);

    // Test lock operations with valid pointer
    assert(lock->lock(lock) == 0);
    assert(lock->unlock(lock) == 0);

    SPIN_LOCK_RELEASE(lock);
    printf("  ✓ Passed\n");
}

// Test 6: Maximum allocation limit
void test_max_allocation(void) {
    printf("Test 6: Maximum allocation limit\n");

    spinlock_dev *locks[SPIN_LOCK_MAX_COUNT + 1] = {NULL};
    int success_count = 0;

    // Try to allocate more than maximum
    for (int i = 0; i < SPIN_LOCK_MAX_COUNT + 1; i++) {
        int ret = SPIN_LOCK_INIT(locks[i]);
        if (ret == 0) {
            success_count++;
            assert(locks[i] != NULL);
        } else {
            assert(locks[i] == NULL);
        }
    }

    // Should only allocate up to MAX_COUNT
    assert(success_count == SPIN_LOCK_MAX_COUNT);

    // Clean up
    for (int i = 0; i < success_count; i++) {
        SPIN_LOCK_RELEASE(locks[i]);
    }

    printf("  ✓ Passed\n");
}

// Thread test structure
typedef struct {
    spinlock_dev *lock;
    int thread_id;
    int iterations;
    int *shared_counter;
} thread_arg_t;

// Thread function for concurrent testing
void* thread_func(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;

    for (int i = 0; i < targ->iterations; i++) {
        // Lock and increment counter
        targ->lock->lock(targ->lock);
        (*targ->shared_counter)++;
        targ->lock->unlock(targ->lock);

        // Random small delay to increase chance of contention
        if (i % 100 == 0) {
            usleep(1);
        }
    }

    return NULL;
}

// Test 7: Concurrent access with locks
void test_concurrent_access(void) {
    printf("Test 7: Concurrent access with locks\n");

    spinlock_dev *lock = NULL;
    SPIN_LOCK_INIT(lock);
    assert(lock != NULL);

    pthread_t threads[NUM_THREADS];
    thread_arg_t thread_args[NUM_THREADS];
    int shared_counter = 0;

    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_args[i].lock = lock;
        thread_args[i].thread_id = i;
        thread_args[i].iterations = NUM_ITERATIONS;
        thread_args[i].shared_counter = &shared_counter;

        pthread_create(&threads[i], NULL, thread_func, &thread_args[i]);
    }

    // Wait for all threads to complete
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Verify final counter value
    int expected = NUM_THREADS * NUM_ITERATIONS;
    assert(shared_counter == expected);
    printf("  ✓ Passed (counter=%d, expected=%d)\n", shared_counter, expected);

    SPIN_LOCK_RELEASE(lock);
}

// Thread function for trylock testing
void* trylock_thread_func(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    int success_count = 0;

    for (int i = 0; i < targ->iterations; i++) {
        // Try to acquire lock
        if (targ->lock->trylock(targ->lock) == 0) {
            success_count++;
            (*targ->shared_counter)++;
            targ->lock->unlock(targ->lock);
        }
        usleep(1);
    }

    return (void*)(long)success_count;
}

// Test 8: Concurrent trylock operations
void test_concurrent_trylock(void) {
    printf("Test 8: Concurrent trylock operations\n");

    spinlock_dev *lock = NULL;
    SPIN_LOCK_INIT(lock);
    assert(lock != NULL);

    pthread_t threads[NUM_THREADS];
    thread_arg_t thread_args[NUM_THREADS];
    int shared_counter = 0;
    int total_successes = 0;

    // Create threads for trylock testing
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_args[i].lock = lock;
        thread_args[i].thread_id = i;
        thread_args[i].iterations = NUM_ITERATIONS / 10; // Fewer iterations for trylock
        thread_args[i].shared_counter = &shared_counter;

        pthread_create(&threads[i], NULL, trylock_thread_func, &thread_args[i]);
    }

    // Collect results
    for (int i = 0; i < NUM_THREADS; i++) {
        void *retval;
        pthread_join(threads[i], &retval);
        total_successes += (int)(long)retval;
    }

    // Total successes should equal the number of increments
    assert(shared_counter == total_successes);
    assert(shared_counter <= NUM_THREADS * (NUM_ITERATIONS / 10));
    printf("  ✓ Passed (successful locks=%d, max possible=%d)\n",
           total_successes, NUM_THREADS * (NUM_ITERATIONS / 10));

    SPIN_LOCK_RELEASE(lock);
}

// Test 9: Reinitialize after release
void test_reinitialize(void) {
    printf("Test 9: Reinitialize after release\n");

    spinlock_dev *lock = NULL;

    // First allocation
    SPIN_LOCK_INIT(lock);
    assert(lock != NULL);
    void *first_addr = lock;

    // Use and release
    lock->lock(lock);
    lock->unlock(lock);
    SPIN_LOCK_RELEASE(lock);
    assert(lock == NULL);

    // Second allocation - might get same or different address
    SPIN_LOCK_INIT(lock);
    assert(lock != NULL);

    // Should be properly initialized
    assert(lock->allocated == 1);
    assert(lock->state == 0);
    assert(lock->lock != NULL);

    // Verify it works
    assert(lock->trylock(lock) == 0);
    assert(lock->state == 1);
    lock->unlock(lock);

    SPIN_LOCK_RELEASE(lock);
    printf("  ✓ Passed\n");
}

// Test 10: Multiple locks concurrent access
void test_multiple_locks_concurrent(void) {
    printf("Test 10: Multiple locks concurrent access\n");

    spinlock_dev *locks[5] = {NULL};
    pthread_t threads[NUM_THREADS];
    int counters[5] = {0};

    // Initialize multiple locks
    for (int i = 0; i < 5; i++) {
        SPIN_LOCK_INIT(locks[i]);
        assert(locks[i] != NULL);
    }

    // Create threads that use different locks
    for (int i = 0; i < NUM_THREADS; i++) {
        int lock_index = i % 5;
        thread_arg_t *arg = malloc(sizeof(thread_arg_t));
        arg->lock = locks[lock_index];
        arg->thread_id = i;
        arg->iterations = NUM_ITERATIONS / 2;
        arg->shared_counter = &counters[lock_index];

        pthread_create(&threads[i], NULL, thread_func, arg);
    }

    // Wait for threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Verify each counter
    int total = 0;
    for (int i = 0; i < 5; i++) {
        int expected = 0;
        for (int j = 0; j < NUM_THREADS; j++) {
            if (j % 5 == i) {
                expected += NUM_ITERATIONS / 2;
            }
        }
        assert(counters[i] == expected);
        total += counters[i];
        printf("    Lock %d: %d/%d\n", i, counters[i], expected);
    }
    printf("  ✓ Passed (total=%d)\n", total);

    // Cleanup
    for (int i = 0; i < 5; i++) {
        SPIN_LOCK_RELEASE(locks[i]);
    }
}

int lock_ut(void) {
    printf("=== Lock Manager Unit Tests ===\n\n");

    test_basic_allocation();
    test_multiple_allocations();
    test_lock_unlock();
    test_trylock();
    test_null_handling();
    test_max_allocation();
    test_concurrent_access();
    test_concurrent_trylock();
    test_reinitialize();
    test_multiple_locks_concurrent();

    printf("\n=== All tests passed! ===\n");
    return 0;
}