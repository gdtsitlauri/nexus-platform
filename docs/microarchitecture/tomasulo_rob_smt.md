# Tomasulo, Reorder Buffer, Speculation and SMT

`--mode advanced --scheduler tomasulo` in `src/sim/advanced/src/model.cpp`.

## Model

A trace-driven timing model: the functional trace supplies values, branch outcomes and memory
addresses, and the model decides when each instruction dispatches, executes, broadcasts and commits.

| stage | behaviour |
| --- | --- |
| dispatch | in order, up to `--issue-width` (1-4) per cycle, into a reorder-buffer slot (`--rob N`) and a reservation station of its class (`--rs N` per class); sources are renamed through a register alias table to the ROB tag of their latest producer (HI and LO are renamed too) |
| execute | oldest ready entry first on a free unit: 2 ALUs, 1 memory port, 1 branch unit, a pipelined multiplier (3 cycles), a non-pipelined divider (4 cycles) |
| memory | loads wait until every older store has its address (conservative disambiguation); a load matching an older in-flight store gets the value by store-to-load forwarding in one cycle |
| write-back | results compete for the common data bus (`--cdb N` per cycle); dependants start the cycle after the broadcast (Hennessy and Patterson timing) |
| commit | in order from the ROB head, up to the issue width per cycle |
| control | 2-bit or static predictor for conditional branches, an 8-entry return-address stack for `jr`; on a misprediction the thread fetches down the wrong path until the branch resolves, then resumes after the redirect penalty |
| deeper pipelines | `--pipeline-depth D` sets the redirect penalty to D - 3 (5 stages give the classic 2 cycles) |
| SMT | `--smt thread1.s`: two hardware threads share ROB, reservation stations, units and CDB; each keeps its own RAT, predictor and RAS; dispatch uses ICOUNT (fewest in-flight first) |

## Example: matrix multiplication (linear-scan code, 2042 instructions)

| configuration | cycles | IPC |
| --- | --- | --- |
| in-order, width 1 (ideal latency model) | 2144 | 0.95 |
| scoreboard | 3386 | 0.60 |
| Tomasulo, width 1, ROB 16, 1 CDB | 2553 | 0.80 |
| Tomasulo, width 4, ROB 32, 8 RS, 4 CDB, static predictor | 1406 | 1.45 |
| same with 2-bit predictor | 1160 | 1.76 |

The in-order model charges no operand latency, so it is not a fair baseline for width 1; the wider
Tomasulo machine is the one that overlaps independent work.

## Verification (`nexus_tomasulo_test`)

- results and architectural registers equal the functional model;
- a 16-entry ROB beats a 2-entry ROB on independent work after a divide, and the small ROB reports
  ROB-full stalls;
- WAW/WAR on the same register are removed by renaming;
- a load right after a store to the same address is satisfied by forwarding;
- every return hits in the return-address stack;
- a larger redirect penalty costs cycles, wider issue gains them;
- two SMT threads keep their own results, commit every instruction and finish sooner than back to back.

`nexus_cross_model_differential` also runs every test program on the Tomasulo core.
