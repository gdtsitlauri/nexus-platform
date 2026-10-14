# Course Mapping

How Nexus 2.0.0 covers the ten course outlines in `docs/course_outlines/`. The topic-by-topic table
with code paths and test names is `docs/reports/full_syllabus_checklist.md`.

| course | coverage | main entry points |
| --- | --- | --- |
| NEY221 Principles of Computer Operation | complete | MIPS toolchain; `nexusc isa` (stack/JVM-like, accumulator, IA-32-like, MIPS); `mips-sim arith` (IEEE-754, integer encodings, Booth, non-restoring, CLA, UTF-8); HDL arithmetic units |
| EY321 Computer Organization | complete | single-cycle and multi-cycle models with hardwired and microprogrammed control; pipeline with hazards, forwarding and prediction; L1/L2, I/O, interrupts, DMA; Verilog modules and the pipelined Verilog CPU with co-simulation |
| NEY613 Compilers | complete | `nexusc` end to end; `nexusc regex` and `nexusc grammar` (automata, LL(1), LR(0)/SLR/LR(1)/LALR, attributes); Flex/Bison path; `nexusc quads`; linear-scan register allocation |
| NEY606 Computer Architecture | complete | Amdahl and benchmark scripts; superscalar, VLIW-lite, scoreboard and Tomasulo with ROB, speculation and RAS; `--pipeline-depth`; caches and peripherals; multiprocessor coherence and consistency |
| NEY709 Advanced Compiler Topics | complete | Earley and parallel parsing; Hindley-Milner (`nexusc infer`); SSA and SCCP; data flow (iterative and region based); symbolic analysis; unrolling; polyhedral transformations (`nexusc poly`); alias and interprocedural analysis |
| NEY704 Parallel Systems and Programming | complete (OpenMP, MPI and CUDA demos run on Colab, `results/colab/`) | SMT on the Tomasulo core; multicore simulator with snooping/directory coherence, SC/weak consistency, locks, barriers, atomics; bus/switch/ring/mesh networks; 1-64 cores; asymmetric cores; SIMT GPU model; OpenMP, MPI, SIMD and CUDA programs |
| EY121 Logic Design | complete | `nexus-eda truth / minimize / pla / bdd / equiv / sat / fsm / analyze / latch`; PLA/PAL/ROM; Quine-McCluskey with Petrick, K-maps, BDDs, CDCL SAT; FSM minimisation and D/T/JK synthesis to Verilog; combinational and sequential Verilog blocks (`docs/logic/logic_design.md`) |
| EY517 Operating Systems | complete (POSIX lab programs run only on Linux/macOS) | `nexus-os schedule / check / banker / pages / disk / alloc / fs`; model checker for concurrent programs; round-robin kernel on the NEXUS CPU; POSIX lab programs (`docs/os/operating_systems.md`) |
| NEY604 Embedded Systems and Microprocessors | complete (FPGA flow needs Yosys and nextpnr; no board test) | SoC in Verilog with UART, timer interrupts and GPIO; the same programs on the simulators, RTL and gate-level netlist; ECP5 bitstream; `nexus-embedded` for real-time scheduling, SDF and Petri nets (`docs/embedded/embedded_soc.md`) |
| NEY705 VLSI Design | complete (no GDSII) | `nexus-eda cmos / sram / effort / synth / place / flow / floorplan / channel / steiner / faults`; switch-level simulation, layout and DRC, logical effort, AIG mapping with SAT equivalence, STA and power, partitioning, placement, routing, ATPG (`docs/vlsi/vlsi_flow.md`) |

Theory-only parts of the outlines (history of computing, Flynn's taxonomy, reading lists) are covered
in `docs/reports/history_of_computing_evolution.md`, `docs/parallel/overview.md` and `docs/literature/`.
