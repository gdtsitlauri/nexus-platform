# Advanced Scheduling

## Purpose

Phase 8 adds a bounded advanced-architecture sandbox that sits beside the existing functional,
single-cycle, multi-cycle, and pipelined models. It is intentionally small: the goal is to make
scheduling and speculation ideas visible without replacing the earlier execution models.

## Implemented Advanced Features

The current sandbox provides real execution experiments for:

- width-1 and width-2 in-order issue
- VLIW-lite static bundling inside reorderable basic blocks
- scoreboard-based dynamic scheduling with issue width 1
- selectable branch predictors: `static-not-taken` and `2bit`
- bounded speculative flush-cycle accounting for conditional-branch mispredictions

## In-Order Width Experiments

The in-order sandbox groups adjacent dynamic instructions into issue packets.

- width 1: one instruction per packet
- width 2: up to two adjacent instructions per packet when no bounded pair hazard is present

The pair-hazard filter blocks combinations involving:

- control instructions
- memory-memory pairs
- HI/LO interactions
- obvious RAW/WAR/WAW register conflicts inside the packet

This keeps the model deterministic and easy to inspect while still exposing a measurable throughput
difference when independent instructions are present.

## VLIW-Lite Scheduling

`vliw-lite` partitions the executed dynamic trace into basic blocks, builds a small dependency
graph, and performs a deterministic list-scheduling pass inside each block.

The scheduler:

- preserves control boundaries
- refuses to reorder memory-memory pairs
- refuses to reorder control instructions
- schedules at most 2 instructions per bundle
- fills unused bundle slots with visible `nop` markers in trace output

Measured bounded result:

- in-order width 2: 5 cycles
- VLIW-lite: 4 cycles

## Scoreboard Scheduling

The final repository now includes an executable scoreboard experiment. It is intentionally bounded:

- issue width must remain 1
- no register renaming is performed
- no reorder buffer is present
- the scheduler tracks functional-unit availability and result readiness centrally

This provides real dynamic-scheduling behavior without claiming a full out-of-order machine. The
focused tests validate both correctness and trace output for the scoreboard mode.

## Speculation Boundary

The advanced sandbox still does not implement a reorder buffer, register renaming, or out-of-order
commit. Instead, it:

- executes the program correctly first to obtain a dynamic instruction trace
- applies predictor accounting to that trace
- charges bounded speculative flush cycles for wrong predictions

That makes speculation visible in the metrics without introducing a much larger architectural
framework.

## What Is Documented With Worked Examples

- Tomasulo / reservation-station concepts
- rename-like indirection
- wakeup/select behavior in a larger out-of-order machine

These remain documentation-backed rather than executable code paths.

## What Is Still Outside Bounded Scope

- Tomasulo-lite or reservation stations as code
- register renaming
- reorder-buffer-style recovery
- superscalar issue beyond width 2
- industrial out-of-order wakeup/select logic
