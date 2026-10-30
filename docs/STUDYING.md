# Studying with NEXUS

Start with [the connected flow](FLOW.md): one computation, several representations and machines.
Then choose a course in the [study guides](courses/README.md). The aim is to understand why a program
behaves as it does: **theory → meaning → small executable example → implementation**.

## Connect the idea to the actual code

Five questions organise the explanation of any mechanism:

1. **Why does this mechanism exist?** For example, forwarding avoids waiting for register write-back.
2. **What state does it keep?** Registers, pipeline entries, a cache line or a compiler fact set.
3. **What changes that state?** One instruction, a bus event, a parser reduction or a transfer function.
4. **What must stay true?** The correct program result, one valid owner, or a conservative analysis fact.
5. **What does it cost?** Cycles, memory traffic, communication, code size or compile time.

When opening a large source file, begin at the function named in the guide. Identify its inputs,
state update and result; follow a helper only when it is needed for that explanation.
For example, `decode_control` maps `lw` to control signals; `Dfa::matches` follows transitions;
`solve_data_flow` propagates sets until stable. Reading an entire simulator at once hides these small mechanisms.

Keep the levels distinct: an algorithm explains a method, an ISA defines visible operations,
a microarchitecture determines how they are executed, and a model approximates selected behaviour.
A simulator's instruction index, cycle count or memory service may differ from a real machine's interface.
Each course guide states the relevant assumptions near the example.

## The reasoning behind a number

A result is understood when the reasoning that produces it is clear. For a 16-byte page, address
36 is page 2 plus offset 4. If page 2 maps to frame 7, the physical address is 116. This connects
the page-table idea to the integer division and remainder operations implementing it.
For performance, the clock period matters as much as the cycle count, and model cycles compare designs
within the model only. For concurrency, correctness comes from the ownership and synchronisation rule,
not from one successful run: a different legal event order may give a different result.

## An example: more cores are not always faster

Question: **does adding cores always make the fixed sum 1..8 faster?** The same
[parallel implementation](../examples/flow/sum_parallel.s), snooping policy, memory settings and eight
inputs are used throughout. All runs return 36. The baseline is that parallel program on one core.

| Cores | Model cycles | Baseline cycles / current cycles |
| --- | --- | --- |
| 1 | 58 | 1.00 |
| 2 | 51 | 1.14 |
| 4 | 59 | 0.98 |
| 8 | 90 | 0.64 |

The loop has little work; every worker still needs the shared-result lock and barrier. The trace
and counters expose contention and waiting, so more cores do not improve every case.
These figures are one simulator's result for one tiny algorithm. A different decomposition,
larger input or different memory cost can change the balance. This is a model comparison, not a
measurement of physical processors.

## The original papers

The [architecture](literature/architecture_readings.md), [compiler](literature/compiler_readings.md)
and [parallel-system](literature/parallel_readings.md) lists connect classic papers to the implemented topics.
For each paper, the useful things to identify are its question, its mechanism, its assumptions and its limits;
the NEXUS example shows the intuition of the mechanism, not the paper's full setup.

The [study guides](courses/README.md) explain each topic and the relevant implementation limits.
