# Hazards

Phase 6 implements bounded but real hazard handling for the current MIPS subset.

## Hazard Classes In Nexus

The pipeline currently handles:

- data hazards:
  - register RAW hazards
  - HI/LO producer-consumer hazards for `mult`/`div` followed by `mflo`/`mfhi`
  - load-use hazards
- control hazards:
  - conditional branch redirects
  - jump and `jr` redirects

The implementation is intentionally educational and does not yet cover:

- structural hazards from shared functional units
- cache miss latency hazards
- exception or interrupt restart hazards

## Forwarding Rules

The EX stage can forward operands from:

- `EX/MEM` when the producer already has a usable ALU result
- `MEM/WB` when the producer is in writeback

This resolves most RAW hazards without stalling, including:

- ALU-to-ALU dependencies
- ALU-to-branch compare dependencies
- ALU-to-store-data dependencies
- HI/LO-to-`mflo`/`mfhi` dependencies

Example from the tests:

```text
addiu $t0, $zero, 5
addiu $t1, $zero, 7
addu  $t2, $t0, $t1
addu  $v0, $t2, $t0
```

This retires with zero stalls and three forwarding events in the current model.

## Load-Use Hazards

Forwarding alone is not enough for an immediately dependent consumer of `lw`, because the load value
is not available until the MEM/WB path.

The pipeline therefore:

- freezes PC and `IF/ID`
- inserts a bubble into `ID/EX`
- records a stall event and a `ID*` timeline marker

Example:

```text
lw   $t1, 0($sp)
addu $v0, $t1, $zero
```

The Phase 6 test suite verifies that this inserts exactly one stall in the bounded model.

## Control Hazards And Flushes

Conditional branches are resolved in EX. When prediction is wrong, the simulator:

- redirects PC
- flushes the younger `IF/ID` instruction if it is on the wrong path
- flushes the same-cycle IF fetch if it is also on the wrong path

Timeline markers:

- `ID!` means a decode-stage instruction was killed by redirect
- `IF!` means a fetched instruction was killed by redirect

Jumps and `jr` also redirect control flow in EX and may trigger younger-instruction flushes.

## What Counts As A Stall Or Flush

The Phase 6 summaries report:

- `Stalls`: cycles where decode/fetch are frozen
- `Load-use stalls`: the subset caused specifically by `lw` consumers
- `Flushes`: the number of younger instructions invalidated by redirects

These counts are intentionally simple and deterministic so they are easy to compare in tests.

## Files To Inspect

- pipeline model: `src/sim/pipeline/src/model.cpp`
- pipeline docs: `docs/microarchitecture/pipeline.md`
- branch handling docs: `docs/microarchitecture/branch_prediction.md`
