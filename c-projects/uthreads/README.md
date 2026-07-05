# uthreads — User-Level Threading Library

A cooperative user-level threading library written in C, built on top of the POSIX `ucontext` API.

## Features

- **Thread creation** — `uthread_create()` spawns threads with an independent stack
- **Cooperative yield** — `uthread_yield()` hands the CPU to the next runnable thread
- **Join & return values** — `uthread_join()` blocks until a thread finishes and retrieves its return value
- **Clean exit** — `uthread_exit()` terminates a thread and wakes any joiner
- **Round-robin scheduler** running in its own dedicated stack

## How it works

```
┌──────────────┐   swapcontext    ┌─────────────────┐
│  Thread A    │ ───────────────► │   scheduler()   │
│  (RUNNING)   │                  │  (picks next)   │
└──────────────┘                  └────────┬────────┘
                                           │ swapcontext
                                           ▼
                                  ┌──────────────────┐
                                  │   Thread B       │
                                  │   (RUNNABLE→     │
                                  │    RUNNING)      │
                                  └──────────────────┘
```

Each thread has its own `ucontext_t` and a heap-allocated 64 KB stack. The scheduler runs in a separate context and performs a linear scan to pick the next `RUNNABLE` thread on every dispatch.

### Thread states

| State       | Meaning                                      |
|-------------|----------------------------------------------|
| `RUNNABLE`  | Ready to run; waiting for the scheduler      |
| `RUNNING`   | Currently executing on the CPU               |
| `BLOCKED`   | Waiting inside `uthread_join()`              |
| `FINISHED`  | Completed; return value is available         |

## API

```c
void       uthread_init(void);
int        uthread_create(uthread_t *tid, void *(*func)(void *), void *arg);
void       uthread_yield(void);
int        uthread_join(uthread_t tid, void **retval);
void       uthread_exit(void *retval);   /* noreturn */
uthread_t  uthread_self(void);
```

## Build & run

```bash
cd c-projects/uthreads
make          # compile
make run      # compile + run demo
make clean    # remove build artifacts
```

## Project layout

```
uthreads/
├── include/
│   └── uthreads.h      ← public API
├── src/
│   └── uthreads.c      ← scheduler + thread primitives
├── demo/
│   └── demo.c          ← three demo tests
└── Makefile
```

## Limitations

- **Cooperative only** — threads must yield voluntarily; there is no preemption
- **No mutex/semaphore** — synchronisation beyond join is left to the user
- **Maximum 64 threads** (configurable via `UTHREAD_MAX_THREADS`)
- Thread IDs are not reused after a thread finishes
