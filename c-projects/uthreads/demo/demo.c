/*
 * demo.c — Demonstrates the uthreads user-level threading library.
 *
 * Tests:
 *   1. Basic yield-based concurrency across N worker threads
 *   2. Return value propagation through uthread_join
 *   3. Nested dependency: thread A joins thread B before exiting
 */

#include "uthreads.h"

#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Test 1: Concurrent workers                                           */
/* ------------------------------------------------------------------ */

#define N_WORKERS 4
#define ITERATIONS 5

static void *worker(void *arg) {
    int id = *(int *)arg;
    free(arg);

    for (int i = 0; i < ITERATIONS; i++) {
        printf("  [tid=%d] worker %d — iteration %d/%d\n",
               uthread_self(), id, i + 1, ITERATIONS);
        uthread_yield();
    }
    printf("  [tid=%d] worker %d — done\n", uthread_self(), id);
    return NULL;
}

static void test_concurrent_workers(void) {
    printf("=== Test 1: Concurrent workers ===\n");

    uthread_t tids[N_WORKERS];
    for (int i = 0; i < N_WORKERS; i++) {
        int *id = malloc(sizeof *id);
        *id = i + 1;
        if (uthread_create(&tids[i], worker, id) != 0) {
            fprintf(stderr, "uthread_create failed\n");
            exit(1);
        }
    }

    for (int i = 0; i < N_WORKERS; i++) {
        uthread_join(tids[i], NULL);
    }

    printf("All %d workers finished.\n\n", N_WORKERS);
}

/* ------------------------------------------------------------------ */
/* Test 2: Return value via join                                        */
/* ------------------------------------------------------------------ */

static void *sum_range(void *arg) {
    long n   = (long)arg;
    long sum = 0;
    long step = n / 8;          /* yield 8 times throughout the loop */

    for (long i = 1; i <= n; i++) {
        sum += i;
        if (step > 0 && i % step == 0) {
            printf("  [tid=%d] summing... i=%ld\n", uthread_self(), i);
            uthread_yield();
        }
    }
    return (void *)sum;
}

static void test_return_value(void) {
    printf("=== Test 2: Return value via uthread_join ===\n");

    uthread_t tid;
    uthread_create(&tid, sum_range, (void *)1000000L);

    void *result;
    uthread_join(tid, &result);

    long expected = 1000000L * 1000001L / 2;  /* n*(n+1)/2 */
    long got      = (long)result;

    printf("  sum(1..1000000) = %ld  (expected %ld) — %s\n\n",
           got, expected, got == expected ? "PASS" : "FAIL");
}

/* ------------------------------------------------------------------ */
/* Test 3: Nested join dependency                                       */
/* ------------------------------------------------------------------ */

static uthread_t inner_tid;

static void *inner_thread(void *arg) {
    (void)arg;
    printf("  [tid=%d] inner: started\n", uthread_self());
    uthread_yield();
    printf("  [tid=%d] inner: finishing\n", uthread_self());
    return (void *)42L;
}

static void *outer_thread(void *arg) {
    (void)arg;
    printf("  [tid=%d] outer: waiting for inner tid=%d\n",
           uthread_self(), inner_tid);

    void *inner_ret;
    uthread_join(inner_tid, &inner_ret);

    printf("  [tid=%d] outer: inner returned %ld\n",
           uthread_self(), (long)inner_ret);
    return (void *)((long)inner_ret * 2);
}

static void test_nested_join(void) {
    printf("=== Test 3: Nested join dependency ===\n");

    uthread_t outer;
    uthread_create(&inner_tid, inner_thread, NULL);
    uthread_create(&outer, outer_thread, NULL);

    void *outer_ret;
    uthread_join(outer, &outer_ret);

    long expected = 84L;
    long got      = (long)outer_ret;
    printf("  outer returned %ld (expected %ld) — %s\n\n",
           got, expected, got == expected ? "PASS" : "FAIL");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("uthreads demo — user-level cooperative threading via ucontext\n");
    printf("--------------------------------------------------------------\n\n");

    uthread_init();

    test_concurrent_workers();
    test_return_value();
    test_nested_join();

    printf("All tests complete.\n");
    return 0;
}
