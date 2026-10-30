# Course Mapping

How Nexus 3.1.0 covers the six course outlines in `docs/course_outlines/`. The topic-by-topic table
with code paths and test names is `docs/reports/full_syllabus_checklist.md`.

| course | coverage | main entry points |
| --- | --- | --- |
| NEY221 Principles of Computer Operation | complete | MIPS toolchain; `nexusc isa` (stack/JVM-like, accumulator, IA-32-like, MIPS); `mips-sim arith` (IEEE-754, integer encodings, Booth, non-restoring, CLA, UTF-8); HDL arithmetic units |
| EY321 Computer Organization | complete | single-cycle and multi-cycle models with hardwired and microprogrammed control; pipeline with hazards, forwarding and prediction; L1/L2, I/O, interrupts, DMA; Verilog modules and the pipelined Verilog CPU with co-simulation, a polled console and a timer interrupt on its I/O bus; virtual memory (`mips-sim vm`); the multi-cycle Verilog CPU with hardwired and microprogrammed control; branch prediction in the Verilog pipeline |
| NEY613 Compilers | complete | `nexusc` end to end; `nexusc graph/tree/hash` (data structures, traversal and closure algorithms); `nexusc attr` (inherited attributes, dependency graphs); `nexusc regex` and `nexusc grammar` (automata, LL(1), LR(0)/SLR/LR(1)/LALR, attributes); Flex/Bison path; `nexusc quads`; linear-scan register allocation |
| NEY606 Computer Architecture | complete | Amdahl and benchmark scripts; superscalar, VLIW-lite, scoreboard and Tomasulo with ROB, speculation and RAS; software pipelining (`nexusc modsched`); non-blocking caches and prefetching (`mips-sim cache`); `--pipeline-depth`; caches and peripherals; multiprocessor coherence and consistency |
| NEY709 Advanced Compiler Topics | complete | Earley and parallel parsing; Hindley-Milner (`nexusc infer`); SSA and SCCP; data flow (iterative and region based); symbolic analysis; unrolling, including symbolic unrolling with a remainder loop (`--pass unroll-remainder`); polyhedral transformations (`nexusc poly`) and hyperplane schedules by integer linear programming (`nexusc poly <file> schedule`); alias and interprocedural analysis |
| NEY704 Parallel Systems and Programming | complete (OpenMP, MPI and CUDA demos run on Colab, `results/colab/`) | SMT on the Tomasulo core; coarse-grained, fine-grained and simultaneous multithreading (`mips-sim mt`); vector processor (`mips-sim vector`); message passing and collectives (`mips-sim msg`); GPU thread blocks and shared memory (`--block`); multicore simulator with snooping/directory coherence, SC/weak consistency, locks, barriers, atomics; bus/switch/ring/mesh networks; 1-64 cores; asymmetric cores; SIMT GPU model; OpenMP, MPI, SIMD and CUDA programs |

Theory-only parts of the outlines (history of computing, Flynn's taxonomy, reading lists) are covered
in `docs/reports/history_of_computing_evolution.md`, `docs/parallel/overview.md` and `docs/literature/`.
