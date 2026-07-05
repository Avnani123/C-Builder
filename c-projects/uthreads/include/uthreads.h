#ifndef UTHREADS_H
#define UTHREADS_H

/*
 * uthreads.h — Public API for the user-level threading library.
 *
 * Built on top of POSIX ucontext_t.  All scheduling is cooperative:
 * threads run until they call uthread_yield(), uthread_join(), or
 * uthread_exit().
 *
 * Synchronisation primitives (Mutex, Semaphore) are provided in this
 * header and implemented in src/sync.c using C11 atomic operations.
 * Both use FIFO wait-queues with direct handoff to prevent starvation.
 */

#include <stdatomic.h>
#include <ucontext.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#define UTHREAD_MAX_THREADS  64
#define UTHREAD_STACK_SIZE   (64 * 1024)   /* 64 KB per thread */

/* ------------------------------------------------------------------ */
/* Thread types                                                         */
/* ------------------------------------------------------------------ */

typedef int uthread_t;

typedef enum {
    UTHREAD_RUNNABLE = 0, /* ready to run, not currently running      */
    UTHREAD_RUNNING,      /* currently on the CPU                     */
    UTHREAD_BLOCKED,      /* waiting on join / mutex / semaphore      */
    UTHREAD_FINISHED,     /* completed; retval is available           */
} uthread_state_t;

typedef struct {
    uthread_t        id;
    uthread_state_t  state;
    ucontext_t       ctx;
    char            *stack;      /* heap-allocated; NULL for thread 0  */
    void            *retval;     /* set by uthread_exit()              */
    uthread_t        joining;    /* tid waiting on us via join; -1=none */
    int              joined;     /* 1 after uthread_join() claimed it  */
    _Atomic int      baton;      /* handoff permit: 1=token delivered  */
    void            *(*func)(void *);
    void            *arg;
} uthread_tcb_t;

/* ------------------------------------------------------------------ */
/* Synchronisation support types                                        */
/* ------------------------------------------------------------------ */

/*
 * waitq_t — FIFO queue of blocked thread IDs shared by mutex/semaphore.
 * Defined here so mutex/semaphore structs are self-contained and can be
 * stack-allocated by the user.
 */
typedef struct {
    int buf[UTHREAD_MAX_THREADS];
    int head;
    int tail;
    int count;
} waitq_t;

/* ------------------------------------------------------------------ */
/* Mutex                                                                */
/* ------------------------------------------------------------------ */

/*
 * uthread_mutex_t — Cooperative mutex using an atomic lock flag and a
 * FIFO wait-queue.  Direct-handoff unlock prevents lock-stealing and
 * ensures threads are woken in the order they blocked.
 *
 * Usage:
 *   uthread_mutex_t m;
 *   uthread_mutex_init(&m);
 *   uthread_mutex_lock(&m);
 *   // critical section
 *   uthread_mutex_unlock(&m);
 *   uthread_mutex_destroy(&m);
 */
typedef struct {
    _Atomic int  locked;   /* 0 = free, 1 = held                      */
    int          owner;    /* tid of current holder; -1 if free        */
    waitq_t      waitq;    /* FIFO queue of blocked threads            */
} uthread_mutex_t;

/* Initialise a mutex to the unlocked state. */
void uthread_mutex_init(uthread_mutex_t *m);

/* Assert the mutex is unlocked with no waiters, then release resources. */
void uthread_mutex_destroy(uthread_mutex_t *m);

/*
 * Acquire the mutex.  If it is held, the calling thread is enqueued and
 * blocked until ownership is handed to it directly by uthread_mutex_unlock().
 */
void uthread_mutex_lock(uthread_mutex_t *m);

/*
 * Try to acquire the mutex without blocking.
 * Returns 0 on success, -1 (errno=EBUSY) if the mutex is already held.
 */
int uthread_mutex_trylock(uthread_mutex_t *m);

/*
 * Release the mutex.  Must be called by the owning thread.
 * If there are waiters, ownership is transferred directly to the first one.
 */
void uthread_mutex_unlock(uthread_mutex_t *m);

/* ------------------------------------------------------------------ */
/* Semaphore                                                            */
/* ------------------------------------------------------------------ */

/*
 * uthread_sem_t — Counting semaphore using an atomic counter and a FIFO
 * wait-queue.  uthread_sem_post() hands the token directly to a blocked
 * waiter (if any) instead of incrementing count, avoiding races.
 *
 * Usage:
 *   uthread_sem_t s;
 *   uthread_sem_init(&s, 1);   // binary semaphore
 *   uthread_sem_wait(&s);
 *   // critical section / consume resource
 *   uthread_sem_post(&s);
 *   uthread_sem_destroy(&s);
 */
typedef struct {
    _Atomic int  count;    /* current resource count (>= 0)            */
    waitq_t      waitq;    /* FIFO queue of blocked threads            */
} uthread_sem_t;

/* Initialise a semaphore with the given initial value. */
void uthread_sem_init(uthread_sem_t *s, unsigned int value);

/* Assert no threads are waiting, then release resources. */
void uthread_sem_destroy(uthread_sem_t *s);

/*
 * Decrement the semaphore.  If count is 0, the calling thread blocks
 * until uthread_sem_post() hands it a token directly.
 */
void uthread_sem_wait(uthread_sem_t *s);

/*
 * Try to decrement without blocking.
 * Returns 0 on success, -1 (errno=EAGAIN) if count is 0.
 */
int uthread_sem_trywait(uthread_sem_t *s);

/*
 * Increment the semaphore.  If there are waiters, hands the token to
 * the first one (no actual count increment); otherwise increments count.
 */
void uthread_sem_post(uthread_sem_t *s);

/* Return the current semaphore count (informational; not synchronised). */
int uthread_sem_getvalue(const uthread_sem_t *s);

/* ------------------------------------------------------------------ */
/* Thread API                                                           */
/* ------------------------------------------------------------------ */

/*
 * uthread_init() — Must be called once before any other uthread call.
 * Registers the calling (main) thread as thread 0 and sets up the
 * internal scheduler context.
 */
void uthread_init(void);

/*
 * uthread_create() — Spawn a new thread that will call func(arg).
 * Writes the new thread's ID into *tid.
 * Returns 0 on success, -1 on failure.
 */
int uthread_create(uthread_t *tid, void *(*func)(void *), void *arg);

/*
 * uthread_yield() — Voluntarily give up the CPU.
 * The current thread becomes RUNNABLE and the scheduler picks the
 * next RUNNABLE thread.
 */
void uthread_yield(void);

/*
 * uthread_join() — Wait for thread tid to finish.
 * If retval is non-NULL, the thread's return value is stored there.
 * Returns 0 on success, -1 on bad tid, self-join (EDEADLK), or
 * double-join / concurrent-join (EINVAL).
 */
int uthread_join(uthread_t tid, void **retval);

/*
 * uthread_exit() — Terminate the calling thread, setting its return
 * value to retval.  Never returns.
 */
void uthread_exit(void *retval) __attribute__((noreturn));

/*
 * uthread_self() — Return the calling thread's ID.
 */
uthread_t uthread_self(void);

#endif /* UTHREADS_H */
