/*
 * uthreads_internal.h — Internal shared state exposed to sync.c.
 *
 * NOT part of the public API.  Do not include from user code.
 */

#ifndef UTHREADS_INTERNAL_H
#define UTHREADS_INTERNAL_H

#include "uthreads.h"
#include <ucontext.h>

/* Global thread table (defined in uthreads.c) */
extern uthread_tcb_t threads[];
extern int           thread_count;
extern int           current_tid;
extern ucontext_t    scheduler_ctx;

#endif /* UTHREADS_INTERNAL_H */
