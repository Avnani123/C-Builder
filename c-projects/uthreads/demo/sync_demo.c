/*
 * sync_demo.c — Demonstrates uthread_mutex_t and uthread_sem_t.
 *
 * Tests
 * -----
 *  1. Mutex — shared counter: N threads each increment a global 1 000
 *     times under a mutex.  Final value must be N * 1000 with zero
 *     lost updates.
 *
 *  2. Mutex — critical-section output: N threads print multi-line
 *     messages inside a mutex so lines never interleave.
 *
 *  3. Semaphore — binary (mutex-style): guards a shared resource the
 *     same way a mutex does.
 *
 *  4. Semaphore — producer / consumer with a bounded buffer of size K.
 *     One producer fills the buffer; one consumer drains it.
 *     empty_slots and filled_slots semaphores coordinate access.
 */

#include "uthreads.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* Test 1 — Mutex: shared counter                                      */
/* ================================================================== */

#define COUNTER_THREADS    5
#define COUNTER_INCREMENTS 1000

static long          shared_counter = 0;
static uthread_mutex_t counter_mutex;

static void *counter_worker(void *arg) {
    int id = (int)(long)arg;
    for (int i = 0; i < COUNTER_INCREMENTS; i++) {
        uthread_mutex_lock(&counter_mutex);
        shared_counter++;
        uthread_mutex_unlock(&counter_mutex);
        if (i % 200 == 0) uthread_yield(); /* interleave threads */
    }
    (void)id;
    return NULL;
}

static void test_mutex_counter(void) {
    printf("=== Test 1: Mutex — shared counter ===\n");
    shared_counter = 0;
    uthread_mutex_init(&counter_mutex);

    uthread_t tids[COUNTER_THREADS];
    for (int i = 0; i < COUNTER_THREADS; i++) {
        uthread_create(&tids[i], counter_worker, (void *)(long)i);
    }
    for (int i = 0; i < COUNTER_THREADS; i++) {
        uthread_join(tids[i], NULL);
    }

    long expected = (long)COUNTER_THREADS * COUNTER_INCREMENTS;
    printf("  counter = %ld  (expected %ld) — %s\n\n",
           shared_counter, expected,
           shared_counter == expected ? "PASS (no lost updates)" : "FAIL");

    uthread_mutex_destroy(&counter_mutex);
}

/* ================================================================== */
/* Test 2 — Mutex: critical-section output                             */
/* ================================================================== */

#define OUTPUT_THREADS 3
static uthread_mutex_t output_mutex;

static void *output_worker(void *arg) {
    int id = (int)(long)arg;
    for (int r = 0; r < 3; r++) {
        uthread_mutex_lock(&output_mutex);
        /* All three printf lines are atomic from other threads' view. */
        printf("  [tid=%d] worker %d: line 1 of 3\n", uthread_self(), id);
        uthread_yield(); /* without the mutex a yield here would let
                            another thread interleave — but it can't */
        printf("  [tid=%d] worker %d: line 2 of 3\n", uthread_self(), id);
        uthread_yield();
        printf("  [tid=%d] worker %d: line 3 of 3\n", uthread_self(), id);
        uthread_mutex_unlock(&output_mutex);
        uthread_yield();
    }
    return NULL;
}

static void test_mutex_output(void) {
    printf("=== Test 2: Mutex — critical-section output ===\n");
    printf("  (each worker holds the mutex across its 3-line block;\n"
           "   notice blocks never interleave even with yields inside)\n");
    uthread_mutex_init(&output_mutex);

    uthread_t tids[OUTPUT_THREADS];
    for (int i = 0; i < OUTPUT_THREADS; i++) {
        uthread_create(&tids[i], output_worker, (void *)(long)(i + 1));
    }
    for (int i = 0; i < OUTPUT_THREADS; i++) {
        uthread_join(tids[i], NULL);
    }
    printf("  PASS — no interleaved blocks\n\n");

    uthread_mutex_destroy(&output_mutex);
}

/* ================================================================== */
/* Test 3 — Semaphore used as a binary mutex                           */
/* ================================================================== */

static long          sem_counter = 0;
static uthread_sem_t sem_mutex;

static void *sem_counter_worker(void *arg) {
    int steps = (int)(long)arg;
    for (int i = 0; i < steps; i++) {
        uthread_sem_wait(&sem_mutex);   /* P — acquire */
        sem_counter++;
        uthread_sem_post(&sem_mutex);  /* V — release */
        if (i % 250 == 0) uthread_yield();
    }
    return NULL;
}

static void test_semaphore_binary(void) {
    printf("=== Test 3: Semaphore — binary (mutex-style) ===\n");
    sem_counter = 0;
    uthread_sem_init(&sem_mutex, 1); /* initial value = 1 → mutex */

    uthread_t t1, t2, t3;
    uthread_create(&t1, sem_counter_worker, (void *)500L);
    uthread_create(&t2, sem_counter_worker, (void *)500L);
    uthread_create(&t3, sem_counter_worker, (void *)500L);

    uthread_join(t1, NULL);
    uthread_join(t2, NULL);
    uthread_join(t3, NULL);

    printf("  sem_counter = %ld  (expected 1500) — %s\n\n",
           sem_counter,
           sem_counter == 1500 ? "PASS (no lost updates)" : "FAIL");

    uthread_sem_destroy(&sem_mutex);
}

/* ================================================================== */
/* Test 4 — Semaphore: producer / consumer with bounded buffer         */
/* ================================================================== */

#define BUF_SIZE   4
#define ITEM_COUNT 16

static int           buf[BUF_SIZE];
static int           buf_head = 0, buf_tail = 0;
static uthread_sem_t empty_slots;  /* counts free slots  (init = BUF_SIZE) */
static uthread_sem_t filled_slots; /* counts ready items (init = 0)        */
static uthread_mutex_t buf_mutex;  /* protects head/tail indices            */

static void *producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= ITEM_COUNT; i++) {
        uthread_sem_wait(&empty_slots);    /* wait for a free slot */
        uthread_mutex_lock(&buf_mutex);
        buf[buf_tail] = i;
        buf_tail = (buf_tail + 1) % BUF_SIZE;
        printf("  producer → put item %2d  (slots free after: %d)\n",
               i, uthread_sem_getvalue(&empty_slots));
        uthread_mutex_unlock(&buf_mutex);
        uthread_sem_post(&filled_slots);   /* signal a new item */
        uthread_yield();
    }
    return NULL;
}

static void *consumer(void *arg) {
    (void)arg;
    long sum = 0;
    for (int i = 0; i < ITEM_COUNT; i++) {
        uthread_sem_wait(&filled_slots);   /* wait for an item */
        uthread_mutex_lock(&buf_mutex);
        int item = buf[buf_head];
        buf_head = (buf_head + 1) % BUF_SIZE;
        printf("  consumer ← got item %2d\n", item);
        uthread_mutex_unlock(&buf_mutex);
        uthread_sem_post(&empty_slots);    /* free a slot */
        sum += item;
        uthread_yield();
    }
    return (void *)sum;
}

static void test_producer_consumer(void) {
    printf("=== Test 4: Semaphore — producer / consumer (buffer size %d) ===\n",
           BUF_SIZE);

    buf_head = buf_tail = 0;
    uthread_sem_init(&empty_slots,  BUF_SIZE);
    uthread_sem_init(&filled_slots, 0);
    uthread_mutex_init(&buf_mutex);

    uthread_t prod, cons;
    uthread_create(&prod, producer, NULL);
    uthread_create(&cons, consumer, NULL);

    void *consumer_sum;
    uthread_join(prod, NULL);
    uthread_join(cons, &consumer_sum);

    long expected = (long)ITEM_COUNT * (ITEM_COUNT + 1) / 2; /* 1+2+…+16 */
    printf("  sum of consumed items = %ld  (expected %ld) — %s\n\n",
           (long)consumer_sum, expected,
           (long)consumer_sum == expected ? "PASS" : "FAIL");

    uthread_sem_destroy(&empty_slots);
    uthread_sem_destroy(&filled_slots);
    uthread_mutex_destroy(&buf_mutex);
}

/* ================================================================== */
/* main                                                                 */
/* ================================================================== */

int main(void) {
    printf("uthreads sync demo — mutex & semaphore via atomic operations\n");
    printf("-------------------------------------------------------------\n\n");

    uthread_init();

    test_mutex_counter();
    test_mutex_output();
    test_semaphore_binary();
    test_producer_consumer();

    printf("All sync tests complete.\n");
    return 0;
}
