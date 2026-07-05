/*
 * uthreads.c — User-level threading library implementation.
 *
 * Scheduler: cooperative round-robin.
 * Context switching: POSIX ucontext_t (getcontext / makecontext / swapcontext).
 *
 * Thread lifecycle:
 *   uthread_create()  → RUNNABLE
 *   scheduler picks   → RUNNING
 *   uthread_yield()   → RUNNABLE   (back into the run queue)
 *   uthread_join()    → BLOCKED    (until the target finishes)
 *   uthread_exit()    → FINISHED   (wakes any joining thread)
 */

#include "uthreads.h"

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal state — exported via uthreads_internal.h for sync.c        */
/* ------------------------------------------------------------------ */

uthread_tcb_t threads[UTHREAD_MAX_THREADS];
int           thread_count = 0;
int           current_tid  = -1;

/*
 * The scheduler runs in its own context with its own stack.
 * Aligned to 16 bytes to satisfy the x86-64 ABI and other platforms
 * that mandate natural stack alignment for ucontext / signal frames.
 * scheduler_stack stays file-local (sync.c does not need it).
 */
ucontext_t              scheduler_ctx;
static alignas(16) char scheduler_stack[UTHREAD_STACK_SIZE];

/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */

static void scheduler(void);
static void thread_wrapper(void);

/* ------------------------------------------------------------------ */
/* uthread_init                                                         */
/* ------------------------------------------------------------------ */

void uthread_init(void) {
    memset(threads, 0, sizeof(threads));

    /* Register the calling (main) thread as thread 0. */
    threads[0].id      = 0;
    threads[0].state   = UTHREAD_RUNNING;
    threads[0].joining = -1;
    threads[0].joined  = 0;
    atomic_init(&threads[0].baton, 0);
    threads[0].stack   = NULL; /* main uses the OS-provided stack */
    getcontext(&threads[0].ctx);

    current_tid  = 0;
    thread_count = 1;

    /* Build the scheduler context. */
    getcontext(&scheduler_ctx);
    scheduler_ctx.uc_stack.ss_sp    = scheduler_stack;
    scheduler_ctx.uc_stack.ss_size  = sizeof(scheduler_stack);
    scheduler_ctx.uc_stack.ss_flags = 0;
    scheduler_ctx.uc_link           = NULL;
    makecontext(&scheduler_ctx, scheduler, 0);
}

/* ------------------------------------------------------------------ */
/* Scheduler                                                            */
/* ------------------------------------------------------------------ */

/*
 * find_next_runnable() — Linear scan starting after current_tid,
 * wrapping around, returning the first RUNNABLE thread id or -1.
 */
static int find_next_runnable(void) {
    for (int i = 1; i <= thread_count; i++) {
        int tid = (current_tid + i) % thread_count;
        if (threads[tid].state == UTHREAD_RUNNABLE) {
            return tid;
        }
    }
    return -1;
}

/*
 * all_workers_finished() — Return 1 if every non-main thread is FINISHED.
 */
static int all_workers_finished(void) {
    for (int i = 1; i < thread_count; i++) {
        if (threads[i].state != UTHREAD_FINISHED) {
            return 0;
        }
    }
    return 1;
}

/*
 * scheduler() — Central dispatch loop.
 *
 * Entered via swapcontext() from yield / join / mutex / semaphore / exit.
 * Picks the next RUNNABLE thread and switches into it.
 *
 * If no thread is runnable:
 *   • All workers finished → wake thread 0 (main) so it can drain its
 *     uthread_join() calls and exit cleanly.
 *   • Otherwise → true deadlock (every live thread is BLOCKED on something
 *     that can never be resolved). Report the state and abort.
 */
static void scheduler(void) {
    while (1) {
        int next = find_next_runnable();

        if (next != -1) {
            current_tid         = next;
            threads[next].state = UTHREAD_RUNNING;
            swapcontext(&scheduler_ctx, &threads[next].ctx);
            /* Resume here after the thread yields, exits, or blocks. */
            continue;
        }

        /* Case 1: all worker threads done — wake main. */
        if (all_workers_finished()) {
            if (threads[0].state == UTHREAD_BLOCKED ||
                threads[0].state == UTHREAD_RUNNABLE) {
                threads[0].state = UTHREAD_RUNNABLE;
                continue;
            }
            break; /* main also finished — done */
        }

        /* Case 2: deadlock — no thread can ever become runnable. */
        static const char * const state_names[] = {
            "RUNNABLE", "RUNNING", "BLOCKED", "FINISHED"
        };
        fprintf(stderr,
                "uthreads: deadlock — no runnable thread.\n"
                "          Thread states:\n");
        for (int i = 0; i < thread_count; i++) {
            fprintf(stderr, "            tid %d → %s\n",
                    i, state_names[(int)threads[i].state]);
        }
        abort();
    }
}

/* ------------------------------------------------------------------ */
/* Thread entry trampoline                                              */
/* ------------------------------------------------------------------ */

/*
 * thread_wrapper() — Entered when a newly created thread first runs.
 * Calls the user function then calls uthread_exit() so the thread is
 * properly cleaned up even if the user function returns without calling it.
 */
static void thread_wrapper(void) {
    uthread_tcb_t *self   = &threads[current_tid];
    void          *retval = self->func(self->arg);
    uthread_exit(retval);
    __builtin_unreachable();
}

/* ------------------------------------------------------------------ */
/* uthread_create                                                       */
/* ------------------------------------------------------------------ */

int uthread_create(uthread_t *tid, void *(*func)(void *), void *arg) {
    if (!tid || !func)                        return -1;
    if (thread_count >= UTHREAD_MAX_THREADS)  return -1;

    int            new_tid = thread_count++;
    uthread_tcb_t *t       = &threads[new_tid];

    t->id      = new_tid;
    t->state   = UTHREAD_RUNNABLE;
    t->joining = -1;
    t->joined  = 0;
    atomic_init(&t->baton, 0);
    t->func    = func;
    t->arg     = arg;
    t->retval  = NULL;

    t->stack = malloc(UTHREAD_STACK_SIZE);
    if (!t->stack) {
        thread_count--;
        return -1;
    }

    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp    = t->stack;
    t->ctx.uc_stack.ss_size  = UTHREAD_STACK_SIZE;
    t->ctx.uc_stack.ss_flags = 0;
    t->ctx.uc_link           = NULL;
    makecontext(&t->ctx, thread_wrapper, 0);

    *tid = new_tid;
    return 0;
}

/* ------------------------------------------------------------------ */
/* uthread_yield                                                        */
/* ------------------------------------------------------------------ */

void uthread_yield(void) {
    assert(current_tid >= 0);
    threads[current_tid].state = UTHREAD_RUNNABLE;
    swapcontext(&threads[current_tid].ctx, &scheduler_ctx);
}

/* ------------------------------------------------------------------ */
/* uthread_exit                                                         */
/* ------------------------------------------------------------------ */

void uthread_exit(void *retval) {
    assert(current_tid >= 0);
    uthread_tcb_t *self = &threads[current_tid];

    self->retval = retval;
    self->state  = UTHREAD_FINISHED;

    /* Wake any thread blocked on uthread_join(self->id). */
    if (self->joining != -1) {
        threads[self->joining].state = UTHREAD_RUNNABLE;
        self->joining = -1;
    }

    /*
     * Jump to the scheduler.  We cannot free self->stack here because
     * we are still executing on it.  uthread_join() frees it once the
     * thread is confirmed FINISHED.
     */
    swapcontext(&self->ctx, &scheduler_ctx);

    assert(0 && "uthread_exit: scheduler resumed a finished thread");
    __builtin_unreachable();
}

/* ------------------------------------------------------------------ */
/* uthread_join                                                         */
/* ------------------------------------------------------------------ */

int uthread_join(uthread_t tid, void **retval) {
    if (tid < 0 || tid >= thread_count) { errno = EINVAL;   return -1; }
    if (tid == current_tid)             { errno = EDEADLK;  return -1; }

    uthread_tcb_t *target = &threads[tid];

    /* Reject double-join and concurrent-join. */
    if (target->joined ||
        (target->joining != -1 && target->joining != current_tid)) {
        errno = EINVAL;
        return -1;
    }

    /* Block until the target finishes; its exit() wakes us. */
    while (target->state != UTHREAD_FINISHED) {
        target->joining            = current_tid;
        threads[current_tid].state = UTHREAD_BLOCKED;
        swapcontext(&threads[current_tid].ctx, &scheduler_ctx);
    }

    target->joined = 1;

    if (retval) {
        *retval = target->retval;
    }

    /* Safe to free now — target is no longer running on its stack. */
    if (target->stack) {
        free(target->stack);
        target->stack = NULL;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* uthread_self                                                         */
/* ------------------------------------------------------------------ */

uthread_t uthread_self(void) {
    return current_tid;
}
