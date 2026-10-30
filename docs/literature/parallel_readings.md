# Parallel systems readings (NEY704)

Classic papers behind the topics of the parallel-systems guide. Read the paper, then run the matching example.

| paper | topic | in NEXUS |
| --- | --- | --- |
| M. J. Flynn, *Some Computer Organizations and Their Effectiveness*, IEEE Trans. Computers, 1972 | SISD / SIMD / MIMD | [NEY704 §1](../courses/NEY704.md#1-kinds-of-parallel-machines-flynn) |
| D. M. Tullsen, S. J. Eggers, H. M. Levy, *Simultaneous Multithreading: Maximizing On-Chip Parallelism*, ISCA, 1995 | SMT | [NEY704 §2](../courses/NEY704.md#2-multithreading-hide-stalls-with-other-threads) |
| L. M. Censier, P. Feautrier, *A New Solution to Coherence Problems in Multicache Systems*, IEEE Trans. Computers, 1978 | directory coherence | [NEY704 §3](../courses/NEY704.md#3-shared-memory-cache-coherence) |
| M. S. Papamarcos, J. H. Patel, *A Low-Overhead Coherence Solution for Multiprocessors with Private Cache Memories*, ISCA, 1984 | snooping (MESI) | [NEY704 §3](../courses/NEY704.md#3-shared-memory-cache-coherence) |
| L. Lamport, *How to Make a Multiprocessor Computer That Correctly Executes Multiprocess Programs*, IEEE Trans. Computers, 1979 | sequential consistency | [NEY704 §4](../courses/NEY704.md#4-memory-consistency-and-synchronisation) |
| S. V. Adve, K. Gharachorloo, *Shared Memory Consistency Models: A Tutorial*, IEEE Computer, 1996 | relaxed consistency | [NEY704 §4](../courses/NEY704.md#4-memory-consistency-and-synchronisation) |
| R. M. Russell, *The CRAY-1 Computer System*, CACM, 1978 | vector processors, chaining | [NEY704 §7](../courses/NEY704.md#7-vector-computers-and-simd) |
| E. Lindholm et al., *NVIDIA Tesla: A Unified Graphics and Computing Architecture*, IEEE Micro, 2008 | GPUs, SIMT | [NEY704 §8](../courses/NEY704.md#8-gpus-and-their-programming) |
| W. J. Dally, C. L. Seitz, *Deadlock-Free Message Routing in Multiprocessor Interconnection Networks*, IEEE Trans. Computers, 1987 | routing in meshes and tori | [NEY704 §9](../courses/NEY704.md#9-interconnection-networks-bus-switch-network-on-chip) |

Also: the OpenMP and MPI specifications (openmp.org, mpi-forum.org); V. Nagarajan, D. J. Sorin, M. D. Hill,
D. A. Wood, *A Primer on Memory Consistency and Cache Coherence* (2nd ed.); W. J. Dally, B. Towles, *Principles
and Practices of Interconnection Networks*.
