# 🚀 `uthreads` — User-Level Threading & Concurrency Engine

A lightweight, high-performance, user-space cooperative threading library implemented in **C11** using `ucontext` for context switching. The project features a custom **Round-Robin scheduler**, custom **Mutexes** and **Semaphores** built on C11 atomic primitives, and an optimized **Direct-Handoff architecture** designed to eliminate thread starvation and lock stealing.

---

## 🛠️ Architecture & Core Components

### 1. Thread Control Block (TCB) & Context Switching
* **User-Space State Control:** Uses native POSIX `getcontext`, `makecontext`, and `swapcontext` primitives to manage register states and execution stacks entirely in user space without kernel context switching overhead.
* **Stack Alignment & Allocation:** Dynamically allocates isolated $64 \text{ KB}$ execution stacks per thread with strict memory alignment.
* **Trampoline Execution Engine:** Wraps user worker functions in a dedicated exit trampoline to ensure non-returning threads trigger clean `uthread_exit` cycles without leaking memory or throwing CPU bus errors.

### 2. Round-Robin Scheduler
* **Non-Preemptive Scheduling:** Threads voluntarily yield execution via `uthread_yield()`, passing CPU control back to the scheduler.
* **Deadlock & Main Loop Tracking:** The scheduler captures the original OS process context as `Thread 0` (Main Thread) and handles cyclic linear scans across the thread table. It includes automatic detection for deadlocks when all remaining threads enter blocked states.

### 3. Starvation-Free Synchronization Primitives (`sync.c`)
* **Direct-Handoff Lock Transfers:** Eliminates traditional "thundering herd" and lock-stealing problems. When unlocking a mutex or posting to a semaphore with waiting threads, lock ownership transfers directly in-flight to the next waiting thread’s TCB (`baton` state). The lock is never momentarily opened, avoiding TOCTOU race conditions.
* **Zero Pthread Dependencies:** Implemented using standard C11 atomic operations (`stdatomic.h`) with explicit memory barriers (`memory_order_acquire` / `memory_order_release`) to enforce hardware happens-before ordering across thread context swaps.
* **FIFO Wait Queues:** Suspends waiting threads into `UTHREAD_BLOCKED` state inside explicit FIFO queues rather than spinning, yielding 0% waste on host CPU cycles during lock contention.

---

## 📊 Technical Capabilities Verified

| Feature | Design Strategy | Verification Test |
| :--- | :--- | :--- |
| **Context Switching** | `ucontext_t` register swapping | Interleaved execution across $N$ worker threads |
| **Return Propagation** | Generic `void *` pointer passing in TCB | Function return value recovery via `uthread_join` |
| **Nested Dependency** | State machine blocked queues | Thread $A$ waiting on Thread $B$ to exit before unblocking |
| **Mutual Exclusion** | Atomic direct lock handoff | Multi-threaded shared counter ($5,000$ increments, 0 lost updates) |
| **Critical Section Integrity** | Enforced lock ownership bounds | Multi-line log prints remaining atomic despite intermediate yields |
| **Resource Counting** | C11 atomic semaphore counters | Bounded Buffer ($K=4$) Producer-Consumer pipeline |

---

## 📁 Repository Structure

```text
uthreads/
├── include/
│   └── uthreads.h       # Public API (Threads, Mutexes, Semaphores)
├── src/
│   ├── uthreads.c       # TCB management, Round-Robin scheduler, context swaps
│   └── sync.c           # Atomic Mutexes & Semaphores with direct-handoff queues
├── demo/
│   ├── demo.c           # Basic threading tests (yield, join, nested joins)
│   └── sync_demo.c      # Synchronization tests (counter, critical section, producer-consumer)
├── Makefile             # Build automation suite
└── README.md            # Project documentation
## 🚀 Getting Started

### Prerequisites
* A C11-compliant compiler (`gcc` or `clang`)
* A POSIX-compliant Unix/Linux OS environment (or WSL / Replit container environment)
* GNU `make`

### Quick Start (Build & Run All Tests)

Run the following commands in your terminal to clone the project, compile the binaries, and execute the full test suite:

```bash
# 1. Clone your repository (replace with your actual GitHub repository URL)
git clone [https://github.com/YOUR_USERNAME/uthreads.git](https://github.com/YOUR_USERNAME/uthreads.git)

# 2. Navigate into the project directory
cd uthreads

# 3. Build and execute all test suites (Threading + Synchronization)
make run-all
