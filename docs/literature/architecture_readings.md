# Architecture Readings

These notes connect core architecture readings to the implemented Nexus models.

## 1. Patterson and Ditzel, *The Case for the Reduced Instruction Set Computer* (1980)

- Why it matters: explains why a regular load/store ISA can simplify datapaths and compilers.
- Nexus link: the bounded MIPS subset and explicit stack-frame lowering follow this same teaching goal.
- Useful question while reading: which ISA choices simplify Phase 4 code generation and Phase 5/6 datapaths?

## 2. Tomasulo, *An Efficient Algorithm for Exploiting Multiple Arithmetic Units* (1967)

- Why it matters: classical dynamic-scheduling paper.
- Nexus link: Phase 8 stops short of Tomasulo-lite, but the advanced-scheduling docs use it as the reference point for what the bounded width/VLIW experiments do not implement.

## 3. Lamport, *How to Make a Multiprocessor Computer That Correctly Executes Multiprocess Programs* (1979)

- Why it matters: the classic sequential-consistency baseline.
- Nexus link: Phase 10 compares `sc` against a weaker educational store-buffer model.

## 4. Nagarajan, Sorin, Hill, and Wood, *A Primer on Memory Consistency and Cache Coherence* (2nd ed.)

- Why it matters: clean explanation of coherence versus consistency.
- Nexus link: Phase 7 memory/cache and Phase 10 coherence-lite/consistency-lite experiments are easiest to interpret with this framing.

## 5. Hennessy and Patterson, *Computer Architecture: A Quantitative Approach*

- Why it matters: performance interpretation through CPI, bottlenecks, and Amdahl-style tradeoffs.
- Nexus link: the metric and report scripts deliberately follow this quantitative style.

## Suggested Nexus Reading Path

1. read Patterson and Ditzel before looking at `docs/architecture/mips_isa.md`
2. read Lamport before comparing `sc` and `weak-lite`
3. read the coherence primer before extending the Phase 7/10 memory-system code
4. treat Tomasulo as the “next step after Nexus” reference for advanced scheduling
