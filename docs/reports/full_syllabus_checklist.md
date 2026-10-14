# Full Syllabus Checklist (version 2.0.0)

Every topic of the ten course outlines in `docs/course_outlines/` mapped to the code that implements
it and the automated test that checks it. Test names are CTest names (`ctest -R <name>`).

## Status Legend

- **implemented and tested**: executable code plus an automated check in `ctest`
- **implemented, toolchain-dependent**: executable code whose test runs only when an external
  toolchain is installed (OpenMP compiler, MPI, CUDA, clang, Yosys + nextpnr, a POSIX system); the test is
  skipped otherwise
- **notes**: topics that are not programs (history, reading lists, taxonomies); covered in `docs/`

No topic of the ten outlines is left outside the project.

## 1. NEY221 Principles of Computer Operation

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| computer organization overview | implemented and tested | `src/sim/` (functional to pipelined models), `ARCHITECTURE.md` | `nexus_cross_model_differential` |
| MIPS assembly programming, registers, memory, stack, subroutines | implemented and tested | `src/mips/`, `src/sim/functional/`, `nexusc compile` | `nexus_mips_functional_test`, `nexus_phase6_golden` |
| accumulator ISA | implemented and tested | `src/compiler/isa_styles/` (`nexusc isa --style accumulator`) | `nexus_isa_styles` |
| stack ISA and Java bytecode (JVM-like) | implemented and tested | `src/compiler/isa_styles/` (`--style stack`: iload/istore, if_icmpXX, invokestatic, iaload) | `nexus_isa_styles` |
| general-purpose-register (load/store) ISA | implemented and tested | MIPS back end | `nexus_backend_mips_test` |
| IA-32 (register-memory, two-address) | implemented and tested | `src/compiler/isa_styles/` (`--style register-memory`: mov/add/imul/idiv/setcc, [ebp+d], [base+index*4], cdecl) | `nexus_isa_styles` |
| ISA trade-offs (code size, instruction count, memory traffic) | implemented and tested | `nexusc isa <file>` comparison table | `nexus_isa_styles` |
| signed/unsigned integers: sign-magnitude, one's and two's complement, excess-K, BCD | implemented and tested | `src/common/src/number_systems.cpp` (`mips-sim arith repr`) | `nexus_number_systems_test` |
| fixed point | implemented and tested | `src/common/src/fpu_lite.cpp` (Q8.8) | `nexus_fpu_lite_test` |
| floating point (IEEE-754 binary32) | implemented and tested | `soft_add/sub/mul/div`: round-to-nearest-even, subnormals, signed zero, Inf/NaN, bit-exact with hardware (`mips-sim arith float`) | `nexus_number_systems_test` (2M random pairs x 4 ops) |
| non-numeric data (characters, UTF-8) | implemented and tested | strict UTF-8 encoder/decoder (`mips-sim arith utf8`) | `nexus_number_systems_test` |
| adders (ripple carry, carry lookahead) | implemented and tested | `src/common/src/arithmetic.cpp`, `carry_lookahead_add`, `src/hdl/alu/nexus_adder.v` | `nexus_arithmetic_units_test`, `nexus_number_systems_test`, `nexus_hdl_adder` |
| multipliers (shift-add, Booth radix-2) | implemented and tested | `arithmetic.cpp`, `booth_multiply` (`mips-sim arith booth`), `nexus_iterative_multiplier.v` | `nexus_number_systems_test`, `nexus_hdl_multiplier` |
| dividers (restoring, non-restoring) | implemented and tested | `arithmetic.cpp`, `nonrestoring_divide` (`mips-sim arith divide`), `nexus_iterative_divider.v` | `nexus_number_systems_test`, `nexus_hdl_divider` |

## 2. EY321 Computer Organization

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| history of computing | notes | `docs/reports/history_of_computing_evolution.md` | `nexus_report_docs` |
| MIPS machine code | implemented and tested | MIPS32 encoder (`mips-sim encode`) | `nexus_hdl_pipeline_cosim` |
| datapath and control, micro-operations | implemented and tested | `src/sim/single_cycle/`, `src/sim/multi_cycle/` | `nexus_single_cycle_model_test`, `nexus_multi_cycle_model_test` |
| hardwired control | implemented and tested | single/multi-cycle control, `src/hdl/control/` | `nexus_multi_cycle_model_test`, `nexus_hdl_control` |
| microprogrammed control | implemented and tested | multi-cycle `--control microcode` | `nexus_multi_cycle_model_test` |
| pipelining, hazards, forwarding, stalls | implemented and tested | `src/sim/pipeline/` | `nexus_pipeline_model_test`, `nexus_cross_model_differential` |
| branch prediction | implemented and tested | static predictors (pipeline), 2-bit (advanced) | `nexus_advanced_predictor_test` |
| memory hierarchy, L1 + L2 caches | implemented and tested | `src/sim/memory/` | `nexus_memory_cache_test`, `nexus_pipeline_memory_system_test` |
| I/O, buses, interrupts, DMA | implemented and tested | `src/sim/io/` | `nexus_io_system_test` |
| performance evaluation (CPI, IPC, speedup) | implemented and tested | `--stats` on every model, `scripts/run_benchmarks.py` | `nexus_phase7_benchmarks` |
| HDL laboratory: ALU, register file, control, pipeline registers | implemented and tested | `src/hdl/` | `nexus_hdl_*` |
| complete CPU in HDL | implemented and tested | `src/hdl/cpu_pipeline/nexus_mips_pipeline.v`: synthesisable 5-stage MIPS (forwarding, load-use interlock, branch flush, mult/div) | `nexus_hdl_pipeline_cosim` (RTL vs simulator, plus Yosys synthesis) |

## 3. NEY613 Compilers

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| grammars, languages, automata | implemented and tested | `src/compiler/formal/`: regex -> Thompson NFA -> subset DFA -> minimal DFA (`nexusc regex`) | `nexus_formal_test` (vs `std::regex`) |
| hand-written lexer | implemented and tested | `src/compiler/frontend/src/lexer.cpp` | `nexus_frontend_lexer_test` |
| Flex lexer | implemented and tested | `experimental_parallel_parsing/src/flex_bison_lexer.l` | `nexus_experimental_parse_test` |
| recursive-descent (LL) parser | implemented and tested | `src/compiler/frontend/src/parser.cpp` | `nexus_frontend_parser_test` |
| LL(1): FIRST/FOLLOW, parse table, predictive parsing, left-recursion elimination, left factoring | implemented and tested | `nexusc grammar <g> first-follow / ll1 / transform` | `nexus_formal_test` |
| LR parsing: LR(0), SLR(1), canonical LR(1), LALR(1) tables and parsing | implemented and tested | `nexusc grammar <g> lr0 / slr / lr1 / lalr` | `nexus_formal_test` |
| Bison LR parser | implemented and tested | `flex_bison_parser.y` (`experimental-parse --mode bison-lr`) | `nexus_experimental_parse_test` |
| AST construction | implemented and tested | `nexusc ast` | `nexus_frontend_parser_test` |
| semantic analysis, type checking, symbol tables | implemented and tested | `src/compiler/semantics/` | `nexus_semantics_test` |
| attribute grammars | implemented and tested | S-attributed actions `{ $$ = $1 + $3 }` evaluated on LL and LR parse trees | `nexus_formal_test` |
| intermediate code: AST, three-address code, quadruples | implemented and tested | `src/compiler/ir/` (`nexusc ir`, `nexusc quads`) | `nexus_ir_lowering_test`, `nexus_coverage_cli` |
| code generation, instruction selection, stack frames, calls | implemented and tested | `src/compiler/backend_mips/` | `nexus_backend_mips_test`, `nexus_cross_model_differential` |
| register allocation | implemented and tested | linear scan (`--regalloc linear-scan`, `nexusc analysis regalloc`) | `nexus_cross_model_differential`, `nexus_hdl_pipeline_cosim` |
| introductory optimisation | implemented and tested | folding, unrolling, SCCP | `nexus_unroll_pass_test`, `nexus_ssa_test` |
| complete compiler (source to running machine code) | implemented and tested | `nexusc` + `mips-sim` + RTL core | `nexus_phase10_cli`, `nexus_hdl_pipeline_cosim` |

## 4. NEY606 Computer Architecture

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| performance, benchmarks, Amdahl's law | implemented and tested | `scripts/generate_amdahl_report.py`, `scripts/run_benchmarks.py` | `nexus_phase8_amdahl`, `nexus_phase7_benchmarks` |
| deeper pipelines | implemented and tested | `--pipeline-depth N` (redirect penalty grows with depth) | `nexus_tomasulo_test`, `nexus_coverage_cli` |
| superscalar issue | implemented and tested | `--issue-width 2` (in order), `--issue-width 1-4` (Tomasulo) | `nexus_advanced_model_test`, `nexus_tomasulo_test` |
| dynamic scheduling: scoreboard | implemented and tested | `--scheduler scoreboard` | `nexus_advanced_model_test` |
| dynamic scheduling: Tomasulo, reservation stations, register renaming, CDB | implemented and tested | `--scheduler tomasulo` | `nexus_tomasulo_test` |
| static scheduling, VLIW | implemented and tested | `--scheduler vliw-lite` | `nexus_advanced_model_test`, `nexus_phase8_golden` |
| branch prediction, speculation, reorder buffer, return-address stack | implemented and tested | 2-bit predictor, ROB with in-order commit, RAS | `nexus_tomasulo_test` |
| memory and peripherals | implemented and tested | caches, I/O, DMA | `nexus_pipeline_memory_system_test`, `nexus_io_system_test` |
| multiprocessors: coherence, consistency, synchronisation | implemented and tested | `src/sim/parallel/` | `nexus_coherence_test`, `nexus_consistency_test`, `nexus_parallel_model_test` |
| literature study | notes | `docs/literature/architecture_readings.md` | `nexus_literature_docs` |

## 5. NEY709 Advanced Compiler Topics

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| generalized parsing (ambiguous grammars) | implemented and tested | Earley parser with parse-tree counting (`nexusc grammar <g> earley`) | `nexus_formal_test` |
| parallel parsing | implemented and tested | `experimental-parse --mode parallel` | `nexus_experimental_parse_test` |
| type systems | implemented and tested | Hindley-Milner (Algorithm W) with let-polymorphism and occurs check (`nexusc infer`) | `nexus_type_inference_test` |
| intermediate representations, SSA | implemented and tested | `src/compiler/analysis/src/ssa.cpp`: dominance frontiers, phi placement, renaming (`analysis ssa`) | `nexus_ssa_test` |
| CFG, basic blocks, dominators | implemented and tested | `cfg.cpp`, `dominators.cpp` | `nexus_cfg_analysis_test` |
| lattices and convergence | implemented and tested | SCCP over the constant lattice (`analysis sccp`) | `nexus_ssa_test` |
| iterative data-flow analysis | implemented and tested | `data_flow.cpp`, `liveness.cpp` | `nexus_cfg_analysis_test` |
| region-based data-flow analysis | implemented and tested | `region_flow.cpp` | `nexus_phase10_cli` |
| expression optimisation, symbolic analysis | implemented and tested | `symbolic.cpp` | `nexus_symbolic_analysis_test` |
| loop unrolling | implemented and tested | `loop_unroll.cpp` | `nexus_unroll_pass_test` |
| polyhedral model, linear inequalities | implemented and tested | `src/compiler/polyhedral/`: Fourier-Motzkin dependence tests, distance/direction vectors, unimodular transforms, FM loop-bound generation (`nexusc poly`) | `nexus_polyhedral_test` |
| affine analysis and strip-mining | implemented and tested | `affine_analysis.cpp`, `affine_stripmine.cpp` | `nexus_affine_analysis_test` |
| pointer and alias analysis | implemented and tested | `alias_analysis.cpp` | `nexus_alias_analysis_test` |
| interprocedural analysis | implemented and tested | `interprocedural.cpp`, `interprocedural_pass.cpp` | `nexus_interproc_test` |
| literature study | notes | `docs/literature/compiler_readings.md` | `nexus_literature_docs` |

## 6. NEY704 Parallel Systems and Parallel Programming

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| taxonomy of parallel architectures (Flynn) | notes | `docs/parallel/overview.md` | `nexus_report_docs` |
| multithreading, simultaneous multithreading | implemented and tested | 2-way SMT on the Tomasulo core (`--smt <thread1.s>`, ICOUNT fetch) | `nexus_tomasulo_test` |
| shared-memory multiprocessors | implemented and tested | `--mode parallel`, SPMD `worker` entry for any core count | `nexus_parallel_model_test`, `nexus_manycore` |
| distributed memory, message passing (MPI) | implemented, toolchain-dependent | `benchmarks/mpi/mpi_bench.cpp` | `nexus_mpi_benchmark` (needs `mpicxx`; passed on Colab, `results/colab/`) |
| symmetric and asymmetric multiprocessors | implemented and tested | `--core-cpi 1,1,4,4` (big/little cores) | `nexus_manycore` |
| snooping and directory coherence | implemented and tested | `--coherence snoop / directory-lite` | `nexus_coherence_test` |
| memory consistency, synchronisation | implemented and tested | `--consistency sc / weak-lite`, locks, barriers, atomics | `nexus_consistency_test`, `nexus_phase10_golden` |
| OpenMP | implemented, toolchain-dependent | `benchmarks/openmp/openmp_bench.cpp` | `nexus_openmp_benchmark` (needs an OpenMP compiler; passed on Colab) |
| vector and SIMD programming | implemented and tested | `benchmarks/simd/simd_bench.cpp` | `nexus_simd_benchmark` |
| GPUs and SIMT execution | implemented and tested | `src/sim/simt/`: warps, reconvergence stack, SIMD efficiency, memory coalescing (`--mode simt`) | `nexus_simt_test` |
| GPU programming on a device (CUDA) | implemented, toolchain-dependent | `benchmarks/gpu_optional/gpu_optional_bench.cu` | `nexus_gpu_optional` (needs `nvcc`; passed on a Colab Tesla T4, GPU checksums equal the CPU) |
| interconnects: bus, switch, ring, 2-D mesh NoC | implemented and tested | `--interconnect bus / switch / ring / mesh / noc-lite` | `nexus_interconnect_test`, `nexus_manycore` |
| manycore (up to 64 cores) | implemented and tested | SPMD reduction on 1-64 cores | `nexus_manycore` |
| literature study | notes | `docs/literature/parallel_readings.md` | `nexus_literature_docs` |

## 7. EY121 Logic Design

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| number systems and codes | implemented and tested | `src/common/src/number_systems.cpp` (bases, complements, BCD); `src/logic/src/codes.cpp` (Gray, parity, Hamming SEC; `nexus-eda gray`, `nexus-eda hamming`) | `nexus_number_systems_test`, `nexus_logic_test` |
| Boolean algebra | implemented and tested | `src/logic/src/boolean.cpp`: expressions, evaluation, identities checked by truth table, BDD and SAT (`nexus-eda equiv`) | `nexus_logic_test`, `nexus_eda_cli` |
| basic logic gates, universal gates | implemented and tested | gate-level simulator (`src/logic/src/gatesim.cpp`); NAND-only and NOR-only rewriting (`nexus-eda nand`, `nexus-eda nor`) | `nexus_logic_test` |
| truth tables | implemented and tested | `nexus-eda truth`: table, minterms, maxterms | `nexus_logic_test`, `nexus_eda_cli` |
| Karnaugh-map simplification | implemented and tested | `karnaugh_map` (2-4 variables, Gray order, groups of the minimal cover; printed by `nexus-eda minimize`) | `nexus_logic_test`, `nexus_eda_cli` |
| Quine-McCluskey simplification | implemented and tested | tabulation, prime implicants, essential primes, Petrick's method and an exact branch-and-bound cover, don't cares, minimal POS (`nexus-eda minimize`); checked against exhaustive search | `nexus_logic_test`, `nexus_eda_cli` |
| canonical forms and other representations (BDD, CNF) | implemented and tested | canonical SOP/POS; reduced ordered BDDs with variable orders; Tseitin CNF and a CDCL SAT solver (`nexus-eda bdd`, `cnf`, `sat`) | `nexus_logic_test`, `nexus_eda_cli` |
| combinational building blocks | implemented and tested | `src/hdl/logic/nexus_combinational.v`: multiplexer, decoder, priority encoder, comparator, BCD to 7-segment, Gray converter; adders in `src/hdl/alu/` | `nexus_hdl_logic_comb`, `nexus_hdl_adder` |
| combinational circuit design | implemented and tested | specification -> minimised two-level cover -> NAND/NOR network or mapped standard cells (`nexus-eda synth`) | `nexus_logic_test`, `nexus_vlsi_test` |
| programmable combinational circuits (ROM, PLA, PAL) | implemented and tested | `src/logic/src/pla.cpp` (`nexus-eda pla`): multi-output minimisation with shared product terms and output inversion for a PLA (minimum row count checked against brute force), PAL terms per output, ROM table; look-up tables in the FPGA flow | `nexus_logic_test`, `nexus_eda_cli`, `nexus_soc_fpga` |
| sequential elements (latches, flip-flops) | implemented and tested | SR NOR latch, gated D latch and master-slave flip-flop at gate level with delays, races and oscillation (`nexus-eda latch`); D, JK and T flip-flops in Verilog | `nexus_logic_test`, `nexus_hdl_logic_seq` |
| sequential circuits | implemented and tested | universal shift register, synchronous modulo-N up/down counter, ripple counter (`src/hdl/logic/nexus_sequential.v`) | `nexus_hdl_logic_seq` |
| state diagrams and tables, state minimisation | implemented and tested | `src/logic/src/fsm.cpp`: Mealy and Moore machines, state tables, partition-refinement minimisation, equivalence by product machine (`nexus-eda fsm table`, `fsm minimize`) | `nexus_logic_test`, `nexus_eda_cli` |
| analysis of sequential circuits | implemented and tested | flip-flop and output equations -> state table (`nexus-eda analyze`) | `nexus_logic_test`, `nexus_eda_cli` |
| synthesis of sequential circuits | implemented and tested | state assignment (binary, Gray, one-hot), excitation tables for D, T and JK, minimised equations, structural Verilog co-simulated with Icarus against the behavioural machine (`nexus-eda fsm synth`, `fsm verilog`) | `nexus_logic_test`, `nexus_eda_cli` |

## 8. EY517 Operating Systems

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| computer system overview | implemented and tested | CPU, memory hierarchy, I/O, interrupt and DMA models of the earlier courses; the SoC with timer interrupts (`src/hdl/soc/`) | `nexus_io_system_test`, `nexus_soc_levels` |
| operating system overview | implemented and tested | a preemptive round-robin kernel for the NEXUS CPU: timer interrupts, process control blocks, context switches (`examples/os/rr_kernel.s`); on the C++ model and the SoC RTL its dispatch order equals the RR Gantt chart of the scheduling library | `nexus_os_cli`, `nexus_soc_levels`, `nexus_full_flow` |
| process description and control | implemented and tested | seven-state process model with legal transitions and PCB history (`src/os/src/scheduling.cpp`) | `nexus_os_test` |
| threads | implemented and tested | `std::thread` workloads on the synchronisation primitives | `nexus_os_test` |
| concurrency: mutual exclusion and synchronisation | implemented and tested | explicit-state model checker over all interleavings (race, Peterson, broken flag protocols, test-and-set, semaphores, bounded buffer; `nexus-os check`); Peterson lock, semaphores, bounded buffer and readers-writers on real threads | `nexus_os_test`, `nexus_os_cli` |
| concurrency: deadlock and starvation | implemented and tested | Banker's algorithm (safety and requests, checked against exhaustive search), detection with requests, wait-for cycles, dining-philosophers deadlock and its prevention by resource ordering; readers-writers lock with writer preference (`nexus-os banker`, `nexus-os check`) | `nexus_os_test`, `nexus_os_cli` |
| memory management | implemented and tested | first/next/best/worst fit with coalescing and compaction, buddy system, paging (`src/os/src/resources.cpp`; `nexus-os alloc`) | `nexus_os_test` |
| virtual memory | implemented and tested | two-level page tables with an LRU TLB and effective access time; FIFO, LRU, OPT and Clock replacement, Belady's anomaly, stack property, working sets (`nexus-os pages`) | `nexus_os_test`, `nexus_os_cli` |
| uniprocessor scheduling | implemented and tested | FCFS, RR, SPN, SRT, HRRN, priority and feedback with Gantt charts and metrics; the textbook five-process example reproduced (`nexus-os schedule`) | `nexus_os_test`, `nexus_os_cli` |
| I/O management and disk scheduling | implemented and tested | FIFO, SSTF, SCAN, C-SCAN, LOOK, C-LOOK against the exhaustive optimum; RAID 5 with reconstruction (`nexus-os disk`); polled and interrupt-driven I/O in the SoC | `nexus_os_test`, `nexus_os_cli`, `nexus_soc_levels` |
| file management | implemented and tested | Unix-like file system: inodes with direct, single and double indirect blocks, directories, hard links, free-block bitmap, fsck (`nexus-os fs`) | `nexus_os_test` |
| Linux usage (laboratory) | implemented, toolchain-dependent | `examples/os/posix/`: fork/pipe/wait, exec and signals, POSIX threads with mutex and condition variable | `nexus_posix_lab` (Linux or macOS only; passed on Colab Linux/GCC) |

## 9. NEY604 Embedded Systems and Microprocessors

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| microprocessor structure | implemented and tested | single-cycle, multi-cycle and pipelined MIPS models; the synthesisable 5-stage Verilog core | `nexus_hdl_pipeline_cosim`, `nexus_cross_model_differential` |
| registers | implemented and tested | register file in Verilog; architectural registers in every model | `nexus_hdl_register_file`, `nexus_mips_functional_test` |
| microprocessor arithmetic | implemented and tested | ALU, iterative multiplier and divider (Verilog); integer and floating-point arithmetic | `nexus_hdl_alu`, `nexus_arithmetic_units_test` |
| memory | implemented and tested | caches and memory hierarchy; the SoC memory map (instruction and data memory, memory-mapped devices) | `nexus_memory_cache_test`, `nexus_soc_levels` |
| communication with external devices | implemented and tested | memory-mapped I/O bus, UART transmitter (8N1 serial line decoded by the testbench), GPIO, timer with interrupts; polling and interrupt-driven programs (`src/hdl/soc/`, `examples/embedded/`) | `nexus_soc_levels` |
| definition, characteristics and constraints of embedded systems | implemented and tested | real-time scheduling: Liu-Layland and hyperbolic bounds, exact response-time analysis, EDF, rate- and deadline-monotonic simulation, priority inversion with priority inheritance (`src/embedded/`; `nexus-embedded rt`) | `nexus_embedded_test`, `nexus_embedded_rt_cli` |
| modelling of embedded systems | implemented and tested | finite-state machines (EY121 tools), synchronous dataflow (repetition vector, schedule, buffers, deadlock), Petri nets (reachability, boundedness, deadlock) | `nexus_embedded_test`, `nexus_logic_test` |
| embedded-system architectures | implemented and tested | SoC: CPU, bus decoder, peripherals and interrupt line; the round-robin kernel on it | `nexus_soc_levels` |
| ASIC | implemented and tested | RTL -> Yosys -> standard-cell mapping, timing, power, placement and routing in `nexus-eda` (NEY705 section) | `nexus_eda_cli`, `nexus_vlsi_test` |
| FPGA | implemented, toolchain-dependent | `src/hdl/soc/nexus_soc_fpga.v` with a boot ROM; Yosys synth_ecp5, nextpnr-ecp5 placement and routing, ecppack bitstream (no board test) | `nexus_soc_fpga` (needs Yosys and nextpnr) |
| system on chip | implemented and tested | `src/hdl/soc/nexus_soc.v` | `nexus_soc_levels` |
| levels of abstraction | implemented and tested | compute programs agree between the ISA-level simulator and the RTL core (exit code and instruction count); I/O and interrupt programs agree between the CPU models, the Verilog SoC and its synthesised gate-level netlist; one compiled program goes through every level (`scripts/nexus_flow.py`) | `nexus_hdl_pipeline_cosim`, `nexus_soc_levels`, `nexus_full_flow` |
| hardware description languages | implemented and tested | Verilog throughout `src/hdl/` with self-checking testbenches | `nexus_hdl_all`, `nexus_soc_levels` |
| design and implementation on FPGA | implemented, toolchain-dependent | utilisation and maximum frequency reported by `scripts/soc_flow.py --fpga` | `nexus_soc_fpga` (needs Yosys and nextpnr) |

## 10. NEY705 VLSI Design

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| CMOS logic, basic gates and memory elements | implemented and tested | switch-level simulator with strengths and X resolution (`src/vlsi/src/cmos.cpp`); static CMOS complex gates from a pull-down expression; 6T SRAM write and read (`nexus-eda cmos`, `nexus-eda sram`) | `nexus_vlsi_test`, `nexus_eda_cli` |
| layout-level design | implemented and tested | Euler-path transistor ordering, stick diagrams, lambda-rule cell layout and a design-rule checker | `nexus_vlsi_test`, `nexus_eda_cli` |
| delay estimation (logical effort) | implemented and tested | logical effort and parasitic delay from transistor sizing; path analysis (F = GBH, best stage count, sizing) checked against numerical optimisation; fanout buffering (`nexus-eda effort`) | `nexus_vlsi_test`, `nexus_eda_cli` |
| hardware description languages | implemented and tested | Verilog designs and testbenches (`src/hdl/`) | `nexus_hdl_all` |
| integrated-circuit design flows | implemented and tested | RTL -> Yosys -> BLIF -> `nexus-eda` (technology mapping, STA, partitioning, placement, routing with routed-wire timing); RTL -> FPGA bitstream. No GDSII is produced | `nexus_eda_cli`, `nexus_soc_fpga` |
| VLSI design with EDA/CAD tools | implemented and tested | Yosys, Icarus Verilog and nextpnr together with the project's own tools | `nexus_eda_cli`, `nexus_soc_levels` |
| behavioural simulation | implemented and tested | Icarus Verilog testbenches for every module, the CPU and the SoC | `nexus_hdl_all`, `nexus_soc_levels` |
| synthesis and equivalence checking | implemented and tested | and-inverter graphs with structural hashing, BLIF input, cut-based technology mapping; equivalence by random simulation plus a SAT miter (expressions also by BDDs, `nexus-eda equiv`); the mapped netlists are also proved equivalent by Yosys | `nexus_vlsi_test`, `nexus_eda_cli` |
| synthesis algorithms | implemented and tested | Quine-McCluskey/Petrick two-level minimisation, AIG balancing, cut mapping, FSM state assignment | `nexus_logic_test`, `nexus_vlsi_test` |
| power and timing analysis | implemented and tested | static timing (arrival, required, slack, critical path) checked against path enumeration; dynamic power from simulated and probabilistic switching activity | `nexus_vlsi_test`, `nexus_eda_cli` |
| circuit testing | implemented and tested | single stuck-at fault simulation and SAT-based test generation; redundant faults proved untestable (`nexus-eda faults`) | `nexus_vlsi_test` |
| floorplanning and placement | implemented and tested | slicing floorplans (`nexus-eda floorplan`), standard-cell placement with legalisation (`nexus-eda place`) | `nexus_vlsi_test`, `nexus_eda_cli` |
| placement algorithms | implemented and tested | Kernighan-Lin and Fiduccia-Mattheyses partitioning, quadratic placement with conjugate gradients, min-cut placement, simulated annealing; Wong-Liu floorplan annealing against the exact slicing optimum | `nexus_vlsi_test` |
| routing | implemented and tested | two-layer global routing with vias and negotiated congestion (PathFinder) of placed designs (`nexus-eda flow`) | `nexus_vlsi_test`, `nexus_eda_cli` |
| routing algorithms | implemented and tested | Lee wave expansion, A*, left-edge channel routing with vertical constraints, rectilinear MST, iterated 1-Steiner and exact Steiner trees (`nexus-eda channel`, `nexus-eda steiner`) | `nexus_vlsi_test`, `nexus_eda_cli` |
