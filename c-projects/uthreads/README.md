# uthreads — Custom User-Level Threading & Concurrency Library

A cooperative user-space threading library written in C implementing CPU context-switching, custom Thread Control Blocks (TCBs), a Round-Robin scheduler, and synchronisation primitives (Mutex, Semaphore) using atomic operations.

**Tech stack:** C · Linux · POSIX API (`ucontext`) · C11 `stdatomic.h`

---

## Features

### Threading
- **Thread Control Blocks (TCBs)** — each thread has an ID, state, `ucontext_t`, private heap-allocated stack, return value, and join bookkeeping
- **CPU context switching** — `getcontext` / `makecontext` / `swapcontext` for zero-syscall cooperative switching
- **Round-Robin scheduler** — runs in its own dedicated `ucontext_t` / aligned stack; dispatches the next `RUNNABLE` thread on every yield, block, or exit
- **Join with return values** — `uthread_join()` blocks via the scheduler until the target finishes and collects its `void *` return value
- **Deadlock detection** — if no thread is ever runnable again the scheduler prints thread states and calls `abort()`

### Synchronisation (atomic-backed, starvation-free)
- **Mutex** — CAS-based lock with FIFO wait-queue; *direct handoff* in `unlock()` transfers ownership straight to the first waiter instead of releasing and re-racing
- **Semaphore** — counting semaphore with CAS-loop acquire and FIFO wait-queue; `post()` delivers the token directly to a blocked waiter to avoid a TOCTOU window
- Both primitives integrate with the scheduler — blocked threads are marked `UTHREAD_BLOCKED` and yield to other runnable threads rather than spinning

---

## API

### Threading

```c
void       uthread_init(void);
int        uthread_create(uthread_t *tid, void *(*func)(void *), void *arg);
void       uthread_yield(void);
int        uthread_join(uthread_t tid, void **retval);   // EDEADLK on self-join, EINVAL on double-join
void       uthread_exit(void *retval);                   // noreturn
uthread_t  uthread_self(void);
```

### Mutex

```c
void uthread_mutex_init(uthread_mutex_t *m);
void uthread_mutex_destroy(uthread_mutex_t *m);
void uthread_mutex_lock(uthread_mutex_t *m);        // blocks if held; FIFO wakeup
int  uthread_mutex_trylock(uthread_mutex_t *m);     // 0=ok, -1/EBUSY=contended
void uthread_mutex_unlock(uthread_mutex_t *m);      // direct handoff to first waiter
```

### Semaphore

```c
void uthread_sem_init(uthread_sem_t *s, unsigned int value);
void uthread_sem_destroy(uthread_sem_t *s);
void uthread_sem_wait(uthread_sem_t *s);            // P — blocks if count==0
int  uthread_sem_trywait(uthread_sem_t *s);         // 0=ok, -1/EAGAIN=would block
void uthread_sem_post(uthread_sem_t *s);            // V — direct handoff or count++
int  uthread_sem_getvalue(const uthread_sem_t *s);
```

---

## How it works

```
Thread yields / blocks / exits
         │
         │ swapcontext(&thread_ctx, &scheduler_ctx)
         ▼
  ┌─────────────────────────────────────────┐
  │           scheduler()                   │
  │  scan threads[] for next RUNNABLE       │
  │  if none: all done → wake main          │
  │           or deadlock → abort()         │
  └──────────────┬──────────────────────────┘
                 │ swapcontext(&scheduler_ctx, &next_ctx)
                 ▼
         Next thread runs
```

### Thread states

| State       | Meaning                                              |
|-------------|------------------------------------------------------|
| `RUNNABLE`  | Ready; waiting for the scheduler to pick it          |
| `RUNNING`   | Currently executing                                  |
| `BLOCKED`   | Waiting in `join()`, `mutex_lock()`, or `sem_wait()` |
| `FINISHED`  | Returned / called `uthread_exit()`                   |

### Mutex — direct handoff unlock

```
lock()   → CAS(locked 0→1)   fast path: acquired
         → else: enqueue self, BLOCK, yield (scheduler)
           on wakeup: ownership already transferred → return

unlock() → waitq non-empty: set owner=waiter, mark RUNNABLE
                             locked stays 1 (no release/re-race)
         → waitq empty:      owner=-1, locked=0
```

### Semaphore — direct handoff post

```
wait()   → CAS(count, c, c-1) while c>0   → acquired
         → count==0: enqueue self, BLOCK, yield
           on wakeup: token was handed by post() → return

post()   → waitq non-empty: mark waiter RUNNABLE (no count++)
         → waitq empty:     count++
```

---

## Project layout

```
uthreads/
├── include/
│   └── uthreads.h              ← full public API (threads + sync)
├── src/
│   ├── uthreads.c              ← TCBs, scheduler, context switching
│   ├── sync.c                  ← mutex + semaphore (atomic ops)
│   └── uthreads_internal.h     ← internal state shared by sync.c
├── demo/
│   ├── demo.c                  ← threading tests (yield, join, return value)
│   └── sync_demo.c             ← sync tests (counter, output, prod/consumer)
└── Makefile
```

---

## Build & run

```bash
cd c-projects/uthreads
make              # build both binaries
make run          # run threading demo
make run-sync     # run sync demo
make run-all      # run both demos
make clean        # remove object files and binaries
```

---

## Limitations

- **Cooperative only** — no preemption; threads must yield voluntarily
- **Maximum 64 threads** (configurable via `UTHREAD_MAX_THREADS` in `uthreads.h`)
- Thread IDs are never reused; a thread's TCB slot is retired after `uthread_join()`
- `uthread_mutex_t` does not support recursive locking
- No `pthread`-style condition variables (can be built on top of semaphores)
