# Operating Systems (EY517)

Library `src/os/` (`nexus_os`), command-line tool `nexus-os`, examples in `examples/os/`.
Tests: `nexus_os_test`, `nexus_os_cli`, `nexus_posix_lab`, `nexus_soc_levels`.

## Processes and scheduling

- Seven-state process model (new, ready, running, blocked, ready/suspend, blocked/suspend, exit) with
  only the legal transitions and a per-process history (`scheduling.cpp`).
- FCFS, RR, SPN, SRT, HRRN, static priority and multi-level feedback with Gantt charts, turnaround,
  normalised turnaround and waiting time. The textbook five-process example
  (`examples/os/stallings.jobs`) is reproduced, e.g. SRT mean turnaround 7.2.

## Concurrency

- **Model checker** (`concurrency.cpp`): small concurrent programs (`examples/os/*.conc`) in a
  load/store language with `tas`, `await`, semaphores `P`/`V` and critical-section markers. Every
  interleaving is explored; the checker reports mutual-exclusion violations, deadlocks and failed
  assertions with a counterexample trace. It finds the lost update of `count++`, proves Peterson's
  algorithm, breaks the naive flag protocols, and finds the dining-philosophers deadlock, which
  disappears when one philosopher takes the forks in the other order.
- **Primitives on real threads**: Peterson lock, counting semaphore, bounded buffer, readers-writers
  lock with writer preference, stress-tested with `std::thread`.
- **Deadlock**: Banker's algorithm (safety and requests, checked against exhaustive search over
  orderings), detection with outstanding requests, wait-for-graph cycles.

## Memory

- Partitioned allocation with first, next, best and worst fit, coalescing and compaction; the buddy
  system.
- Two-level paging with a TLB (LRU) and effective access time.
- Page replacement: FIFO, LRU, OPT and Clock; Belady's anomaly for FIFO (9 faults with 3 frames,
  10 with 4) and the stack property of LRU are checked; working-set sizes over a window.

## I/O and files

- Disk scheduling: FIFO, SSTF, SCAN, C-SCAN, LOOK, C-LOOK, compared with the exhaustive optimum on
  small queues.
- RAID 5 parity with reconstruction of every stripe after any single disk failure.
- A Unix-like file system on a block array: inodes with direct, single and double indirect blocks,
  directories, hard links, a free-block bitmap and an fsck that checks the structure after random operations.

## A kernel on the NEXUS CPU

`examples/os/rr_kernel.s` is a preemptive round-robin kernel for the MIPS core. Three tasks need
2, 4 and 3 time slices. The timer interrupt saves the running task's registers in its process control
block, charges it one slice and dispatches the next task that still needs time; every dispatch writes
the task number to the UART. On the C++ pipeline model and on the Verilog SoC (`src/hdl/soc/`) the
UART shows `012012121` after 9 interrupts. `examples/os/rr_kernel.jobs` describes the same three jobs
for the scheduling library, and `nexus-os schedule examples/os/rr_kernel.jobs --policy rr --quantum 1`
gives the Gantt chart `0 1 2 0 1 2 1 2 1`: the algorithm and the kernel on the hardware decide the same
way (`nexus_os_cli`, `nexus_full_flow`).

## Linux laboratory

`examples/os/posix/` has three C programs: `fork_pipe.c` (fork, pipe, wait), `exec_signal.c`
(exec, signal handlers, kill) and `threads.c` (POSIX threads with a mutex and a condition variable).
`nexus_posix_lab` builds and runs them only on a POSIX system; on Windows it is skipped. On Google
Colab (Linux, GCC) all three passed: `total 5050`, `child replaced by exec`, `counter 400000`.
