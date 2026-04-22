# History of Computing Evolution

## Purpose

This report connects the educational subsystems implemented in Nexus to major shifts in computer
organization history.

## From Sequential Machines To Structured ISAs

Early stored-program machines made instruction sequencing and memory layout the central organizing
ideas. Nexus reflects that heritage first through its functional MIPS interpreter and then through
its progressively richer organization models. The repository keeps the ISA small and explicit so
that registers, stack frames, control flow, and memory references stay visible instead of hidden
behind a large runtime.

## Control Evolution

The project mirrors the historical shift from simple execution to explicit control organization:

- Phase 4 introduced correctness-first ISA interpretation
- Phase 5 added single-cycle and multi-cycle CPU models, showing the contrast between hardwired and microprogrammed control
- Phase 6 added a 5-stage pipeline with forwarding, stalls, flushes, and bounded static branch prediction

This progression matches how real architectures evolved from simple sequential datapaths toward more
overlapped execution while still balancing clarity, cost, and control complexity.

## Memory Hierarchy Evolution

As processor datapaths became faster, memory latency became a more visible bottleneck. Nexus teaches
that tension in bounded form:

- flat memory remains the correctness baseline
- a direct-mapped cache shows simple locality wins and conflict misses
- a bounded associative cache shows how more flexible placement can reduce those conflicts
- the final repository now extends this to a bounded L1+L2 contrast with explicit hit/miss accounting

The model stays intentionally small, but it now reaches far enough to demonstrate the historical
idea that hierarchy design changes observed performance, not just raw instruction semantics.

## I/O, Interrupts, And DMA

Computer evolution also depended on moving peripheral work away from pure polling loops. Nexus adds
small educational versions of those ideas:

- a memory-mapped console device for visible I/O effects
- a timer source that raises a bounded interrupt
- a DMA controller that performs deterministic background copies

These demos are intentionally small, but they expose the same structural themes that motivated real
interrupt-driven I/O and DMA-capable systems.

## Advanced Scheduling And Speculation

Later architectures pursued more instruction-level parallelism through wider issue, dynamic
scheduling, and better prediction. Nexus now mirrors that story with bounded experiments:

- width-2 issue illustrates why architects pursued wider machines
- VLIW-lite bundling shows one static-scheduling path
- a bounded scoreboard scheduler shows centralized dynamic readiness tracking
- predictor experiments and speculative flush accounting expose the cost of wrong-path work

This remains a teaching sandbox, not a full industrial out-of-order core, but it now goes beyond
purely documented scoreboard ideas.

## Relationship To The Repository

Nexus now follows a historically grounded arc:

1. high-level language frontend and semantic checking
2. IR, CFG, dominators, and analysis infrastructure
3. ISA-level code generation and execution
4. single-cycle, multi-cycle, and pipelined machine organization
5. memory/cache/I/O/performance extensions, including bounded L1+L2 work
6. advanced scheduling, prediction, and Amdahl-style exploration
7. multicore/coherence/consistency experiments
8. HDL correlation and optional heterogeneous-computing demonstrations

That structure is deliberate. It lets the repository teach not only what each subsystem does, but
also why later architectural layers emerged as answers to bottlenecks in earlier ones.

## HDL And Heterogeneous Perspective

Phase 11 closes the loop by adding bounded Verilog modules for datapath and control building
blocks, plus a bounded HDL CPU slice. This mirrors a common historical transition in
computer-engineering education: once the machine model is understood at the ISA and
microarchitecture levels, students revisit the same ideas in HDL form.

The optional CUDA demo is deliberately tiny, but it still reflects the historical move toward
heterogeneous acceleration. Nexus keeps that path explicit while preserving CPU-only validity as the
required baseline.

## Current Boundaries

- Tomasulo-style reservation stations and reorder-buffer execution are still outside bounded scope
- no large manycore or full-coherence research framework is attempted
- no OS or privilege-architecture deep dive is attempted
