# Pipeline

Phase 6 implements a real educational 5-stage pipeline on top of the existing MIPS subset and
loader.

## Implemented Stage Structure

The pipeline uses the classic stages:

1. `IF`: fetch an instruction using the current predicted PC
2. `ID`: hold the fetched instruction and read source operands
3. `EX`: run ALU work, address generation, branch comparison, jump target resolution, and
   forwarding selection
4. `MEM`: perform `lw`/`sw` memory access
5. `WB`: retire the instruction and update architectural registers or HI/LO

The implementation lives in `src/sim/pipeline/src/model.cpp`.

## Pipeline Registers

Phase 6 models four explicit pipeline registers:

- `IF/ID`
- `ID/EX`
- `EX/MEM`
- `MEM/WB`

Each register carries:

- the decoded instruction
- the originating PC
- a dynamic instruction sequence number used in traces and timeline output
- stage-local values such as decoded operands, ALU results, memory data, and destination-register
  metadata

Invalid entries act as bubbles.

## Per-Cycle Behavior

The simulator advances one cycle at a time and performs:

- WB commit from `MEM/WB`
- hazard inspection for a possible load-use stall
- MEM processing from `EX/MEM`
- EX execution from `ID/EX`
- IF fetch using the current predicted PC
- stage freezing, bubble insertion, or flushes as required

This is distinct from the earlier modes:

- `functional` has no pipeline state
- `single-cycle` completes one whole instruction per cycle
- `multi-cycle` sequences one instruction through internal control states
- `pipeline` overlaps multiple instructions at once

## Timeline Output

`mips-sim run <file> --mode pipeline --timeline` prints a deterministic table with:

- one row per dynamic instruction
- cycle columns
- stage occupancy markers such as `IF`, `ID`, `EX`, `MEM`, `WB`
- `ID*` for a stalled decode-stage instruction
- `ID!` or `IF!` for flushed younger instructions

Example excerpt:

```text
timeline[pipeline]:
instruction  | 1    | 2    | 3    | 4    | 5    | 6    | 7    | 8    | 9
I0@pc0:addiu | IF   | ID   | EX   | MEM  | WB   | .    | .    | .    | .
I1@pc1:beq   | .    | IF   | ID   | EX   | MEM  | WB   | .    | .    | .
I2@pc2:addiu | .    | .    | IF   | ID!  | .    | .    | .    | .    | .
```

## Trace Output

`mips-sim run <file> --mode pipeline --trace` prints one line per cycle with:

- cycle number
- instruction identity in each stage
- event annotations for forwarding, stalls, redirects, mispredictions, or halting

Example:

```text
trace[pipeline]: cycle=4 IF=I3@pc3:addiu ID=I2@pc2:addiu EX=I1@pc1:beq MEM=I0@pc0:addiu WB=- events=forward(rs<-EX/MEM(I0@pc0:addiu)); forward(rt<-EX/MEM(I0@pc0:addiu)); mispredict(beq -> pc=3); flush(ID:I2@pc2:addiu)
```

## Current Scope And Limitations

Implemented in Phase 6:

- 5-stage overlap
- pipeline-register modeling
- EX-stage forwarding
- load-use stall insertion
- control flushes
- bounded static branch prediction

Still deferred:

- caches or memory hierarchy effects
- DMA, interrupts, or I/O timing
- superscalar issue, scoreboard, Tomasulo, or out-of-order behavior
- advanced branch prediction experiments beyond bounded static strategies

## Deeper Pipelining Note

Worked example:

```text
IF -> ID -> EXa -> EXb -> MEM -> WB
```

Splitting execute into two stages can shorten a cycle time but increases branch penalty and
forwarding complexity. Nexus does not implement this deeper pipeline in code, but the example is
included here so the checklist can distinguish implemented 5-stage work from documented deeper
pipelining concepts.
