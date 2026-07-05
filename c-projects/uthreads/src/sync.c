/*
 * sync.c — Cooperative Mutex and Semaphore for the uthreads library.
 *
 * Design principles
 * -----------------
 *  • Atomic operations (C11 stdatomic) are used for the locked/count
 *    fields so that memory ordering is explicit and compiler reordering
 *    is prevented — even though the OS only runs one uthread at a time,
 *    atomics document intent and keep the code correct under any future
 *    preemptive extension.
 *
 *  • Both primitives use a FIFO wait-queue so threads are woken in the
 *    order they blocked — this prevents starvation.
 *
 *  • "Direct handoff" avoids a TOCTOU window: instead of releasing the
 *    lock/token and then making a waiter race to re-acquire it, we
 *    transfer ownership directly to the first waiter inside the
 *    unlock/post call.  The woken thread returns without retrying CAS.
 *
 * Mutex lifecycle
 * ---------------
 *   lock()   — CAS(locked 0→1); on failure: enqueue self, BLOCK, yield.
 *              On wakeup ownership was handed off by unlock() → return.
 *   trylock()— CAS attempt only; returns 0 on success or -1 immediately.
 *   unlock() — if waitq non-empty: hand ownership to head waiter, mark
 *              RUNNABLE; else: clear owner and atomically clear locked.
 *
 * Semaphore lifecycle
 * -------------------
 *   wait()   — CAS(count, c, c-1) while c>0; on failure: enqueue self,
 *              BLOCK, yield.  On wakeup the token was handed by post() →
 *              return (no decrement needed).
 *   trywait()— CAS loop only; returns 0 on success or -1 immediately.
 *   post()   — if waitq non-empty: hand the token directly to head waiter
 *              (no count increment), mark RUNNABLE; else: fetch_add(1).
 */

#include "uthreads_internal.h"
#include "uthreads.h"

#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal FIFO wait-queue helpers                                     */
/* ------------------------------------------------------------------ */

static void wq_init(waitq_t *wq) {
    wq->head  = 0;
    wq->tail  = 0;
    wq->count = 0;
}

static int wq_empty(const waitq_t *wq) {
    return wq->count == 0;
}

/* Push tid onto the tail of the queue. */
static void wq_push(waitq_t *wq, int tid) {
    assert(wq->count < UTHREAD_MAX_THREADS && "waitq overflow");
    wq->buf[wq->tail] = tid;
    wq->tail          = (wq->tail + 1) % UTHREAD_MAX_THREADS;
    wq->count++;
}

/* Pop and return the head tid, or -1 if empty. */
static int wq_pop(waitq_t *wq) {
    if (wq->count == 0) return -1;
    int tid  = wq->buf[wq->head];
    wq->head = (wq->head + 1) % UTHREAD_MAX_THREADS;
    wq->count--;
    return tid;
}

/* ================================================================== */
/* Mutex                                                                */
/* ================================================================== */

void uthread_mutex_init(uthread_mutex_t *m) {
    assert(m != NULL);
    atomic_init(&m->locked, 0);
    m->owner = -1;
    wq_init(&m->waitq);
}

void uthread_mutex_destroy(uthread_mutex_t *m) {
    assert(m != NULL);
    assert(atomic_load(&m->locked) == 0 &&
           "uthread_mutex_destroy: mutex still locked");
    assert(wq_empty(&m->waitq) &&
           "uthread_mutex_destroy: threads still waiting");
    (void)m;
}

void uthread_mutex_lock(uthread_mutex_t *m) {
    assert(m != NULL);

    /* Fast path — uncontended acquire. */
    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(
            &m->locked, &expected, 1,
            memory_order_acquire, memory_order_relaxed)) {
        m->owner = current_tid;
        return;
    }

    /*
     * Slow path — the mutex is held by another thread.
     * Add ourselves to the FIFO wait-queue, block, and yield to the
     * scheduler.  When unlock() hands ownership to us we wake here and
     * return directly — no retry CAS needed (handoff semantics).
     */
    wq_push(&m->waitq, current_tid);
    threads[current_tid].state = UTHREAD_BLOCKED;
    swapcontext(&threads[current_tid].ctx, &scheduler_ctx);

    /*
     * We have been woken by uthread_mutex_unlock().
     * Acquire load on our baton: this pairs with the release store in
     * unlock() and establishes happens-before, making all writes that
     * preceded the unlock() visible to us now.
     */
    while (!atomic_load_explicit(&threads[current_tid].baton,
                                  memory_order_acquire))
        ; /* cooperative scheduler guarantees baton==1 on first check */
    atomic_store_explicit(&threads[current_tid].baton, 0,
                           memory_order_relaxed);
    /* m->owner == current_tid was set by the unlocker. */
}

int uthread_mutex_trylock(uthread_mutex_t *m) {
    assert(m != NULL);
    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(
            &m->locked, &expected, 1,
            memory_order_acquire, memory_order_relaxed)) {
        m->owner = current_tid;
        return 0;
    }
    errno = EBUSY;
    return -1;
}

void uthread_mutex_unlock(uthread_mutex_t *m) {
    assert(m != NULL);
    assert(m->owner == current_tid &&
           "uthread_mutex_unlock: called by non-owner");

    if (!wq_empty(&m->waitq)) {
        /*
         * Direct handoff: transfer ownership to the first waiter without
         * clearing 'locked'.  The waiter inherits the lock and will
         * return from uthread_mutex_lock() without re-acquiring.
         */
        int next = wq_pop(&m->waitq);
        m->owner = next;
        /* locked stays 1 — still held, now by 'next'.
         * Release store: all writes before unlock() are now visible to
         * the acquiring thread once it reads the baton with acquire.  */
        atomic_store_explicit(&threads[next].baton, 1, memory_order_release);
        threads[next].state = UTHREAD_RUNNABLE;
    } else {
        /* No waiters — release the lock. */
        m->owner = -1;
        atomic_store_explicit(&m->locked, 0, memory_order_release);
    }
}

/* ================================================================== */
/* Semaphore                                                            */
/* ================================================================== */

void uthread_sem_init(uthread_sem_t *s, unsigned int value) {
    assert(s != NULL);
    atomic_init(&s->count, (int)value);
    wq_init(&s->waitq);
}

void uthread_sem_destroy(uthread_sem_t *s) {
    assert(s != NULL);
    assert(wq_empty(&s->waitq) &&
           "uthread_sem_destroy: threads still waiting");
    (void)s;
}

void uthread_sem_wait(uthread_sem_t *s) {
    assert(s != NULL);

    /*
     * Fast path — try to atomically decrement a positive count.
     * Use a CAS loop to handle concurrent post() calls gracefully.
     */
    while (1) {
        int c = atomic_load_explicit(&s->count, memory_order_acquire);
        if (c > 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &s->count, &c, c - 1,
                    memory_order_acquire, memory_order_relaxed)) {
                return; /* token acquired */
            }
            /* CAS lost; retry */
            continue;
        }

        /*
         * Slow path — count is 0.  Block until post() hands us a token
         * directly.  On wakeup we return immediately; post() already
         * "consumed" the increment on our behalf (no decrement needed).
         */
        wq_push(&s->waitq, current_tid);
        threads[current_tid].state = UTHREAD_BLOCKED;
        swapcontext(&threads[current_tid].ctx, &scheduler_ctx);
        /*
         * Acquire load: pairs with the release store in uthread_sem_post()
         * to establish happens-before, making all writes before post()
         * visible to us.
         */
        while (!atomic_load_explicit(&threads[current_tid].baton,
                                      memory_order_acquire))
            ; /* cooperative scheduler guarantees baton==1 on first check */
        atomic_store_explicit(&threads[current_tid].baton, 0,
                               memory_order_relaxed);
        return; /* token was handed directly by post() */
    }
}

int uthread_sem_trywait(uthread_sem_t *s) {
    assert(s != NULL);
    int c;
    do {
        c = atomic_load_explicit(&s->count, memory_order_acquire);
        if (c <= 0) {
            errno = EAGAIN;
            return -1;
        }
    } while (!atomic_compare_exchange_weak_explicit(
                 &s->count, &c, c - 1,
                 memory_order_acquire, memory_order_relaxed));
    return 0;
}

void uthread_sem_post(uthread_sem_t *s) {
    assert(s != NULL);

    if (!wq_empty(&s->waitq)) {
        /*
         * Direct handoff: give the token straight to the first waiter
         * instead of incrementing count.  The waiter will return from
         * uthread_sem_wait() as if it successfully decremented.
         */
        int next = wq_pop(&s->waitq);
        /* count is NOT incremented — token flows directly to 'next'.
         * Release store: all writes before post() are visible to the
         * waiter once it reads the baton with acquire.               */
        atomic_store_explicit(&threads[next].baton, 1, memory_order_release);
        threads[next].state = UTHREAD_RUNNABLE;
    } else {
        /* No waiters — deposit the token into count. */
        atomic_fetch_add_explicit(&s->count, 1, memory_order_release);
    }
}

int uthread_sem_getvalue(const uthread_sem_t *s) {
    assert(s != NULL);
    return atomic_load_explicit(&s->count, memory_order_relaxed);
}
