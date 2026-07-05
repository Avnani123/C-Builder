#ifndef UTHREADS_H
#define UTHREADS_H

/*
 * uthreads.h — Public API for the user-level threading library.
 *
 * Built on top of POSIX ucontext_t. All scheduling is cooperative:
 * threads run until they call uthread_yield(), uthread_join(), or
 * uthread_exit().
 */

#include <ucontext.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#define UTHREAD_MAX_THREADS  64
#define UTHREAD_STACK_SIZE   (64 * 1024)   /* 64 KB per thread */

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

typedef int uthread_t;

typedef enum {
    UTHREAD_RUNNABLE = 0, /* ready to run, not currently running      */
    UTHREAD_RUNNING,      /* currently on the CPU                     */
    UTHREAD_BLOCKED,      /* waiting on uthread_join()                */
    UTHREAD_FINISHED,     /* completed; retval is available           */
} uthread_state_t;

typedef struct {
    uthread_t        id;
    uthread_state_t  state;
    ucontext_t       ctx;
    char            *stack;      /* heap-allocated; NULL for thread 0  */
    void            *retval;     /* set by uthread_exit()              */
    uthread_t        joining;    /* tid of thread waiting on us; -1 if none */
    int              joined;     /* 1 after uthread_join() has claimed it  */
    void            *(*func)(void *);
    void            *arg;
} uthread_tcb_t;

/* ------------------------------------------------------------------ */
/* API                                                                 */
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
 * Returns 0 on success, -1 on bad tid.
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
