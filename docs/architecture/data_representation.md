# Data Representation

Phase 5 ties data-representation coverage directly to the current Nexus machine model instead of
leaving it as generic notes.

## Machine Context Used By Nexus

The implemented compiler and simulators currently assume:

- 32-bit integer registers
- 32-bit word-addressed data memory with word alignment for `lw` and `sw`
- two's-complement signed arithmetic
- booleans represented as integer `0` or `1`
- stack-based storage for locals, temporaries, saved registers, and array objects

This matters because the same representation choices are now exercised by:

- the Phase 4 functional interpreter
- the Phase 5 single-cycle model
- the Phase 5 multi-cycle model
- arithmetic helpers in `src/common/src/arithmetic.cpp`

## Signed And Unsigned Integers

### Signed Integers

Signed values are represented in two's-complement form. That is the behavior used by:

- `add`, `addu`, `sub`
- `slt`
- signed multiply/divide support through `mult`, `div`, `mflo`, and `mfhi`

Overflow is tracked inside the Phase 5 arithmetic helpers even though the current educational
machine model does not raise traps.

Worked example:

```text
0xFFFFFFFF  -> signed: -1
0x00000007  -> signed: 7
```

### Unsigned Integers

Unsigned interpretation uses the same 32-bit bit pattern but changes comparison meaning. The
current repository demonstrates this with:

- `sltu`
- `sltiu`
- unsigned adder carry tracking inside `AdderResult`

Worked example:

```text
0xFFFFFFFF  -> unsigned: 4294967295
0x00000007  -> unsigned: 7
sltu(-1, 7) -> false because 0xFFFFFFFF is larger than 7 as an unsigned value
```

## Overflow, Carry, And Sign

Phase 5 separates three ideas that are often confused in introductory architecture work:

- sign bit: the high bit of a 32-bit word in two's-complement interpretation
- carry out: useful for unsigned addition/subtraction reasoning
- signed overflow: useful for signed arithmetic reasoning

The `AdderResult` and `ALUResult` structures expose these explicitly so tests can reason about
them without pretending the machine already implements exception handling.

## Fixed-Point Concepts

Nexus does not yet execute fixed-point instructions, but the repository now documents the concept
because later architecture and compiler work may want bounded numeric formats.

Educational framing:

- a fixed-point number is an integer bit pattern plus an implied scaling factor
- the existing integer datapath can often be reused for fixed-point add/subtract
- multiply/divide need scaling correction and overflow care

Example:

```text
Q16.16 value 1.5  -> stored as 0x00018000
```

This is documentation-only in Phase 5.

## Floating-Point Concepts

The current MIPS subset and simulators do not implement floating-point registers or IEEE-754
operations yet. Phase 5 documents the concept because the course coverage requires it.

Key distinctions from the current integer datapath:

- sign, exponent, and significand fields
- normalization and rounding
- NaN / infinity handling
- different exception behavior from integer overflow

This is documentation-only in Phase 5.

## Booleans

Booleans are currently lowered and executed as 32-bit integer values:

- `0` means false
- nonzero values are treated as true in conditional branching
- the frontend still type-checks booleans separately from integers

This split is important:

- semantic analysis preserves source-language type intent
- backend and machine models execute a compact integer representation

## Characters And Strings

The current frontend does not yet expose full string literals or byte-oriented execution support,
but the course coverage requires the conceptual model.

Phase 5 documentation stance:

- characters are typically stored as integer code points or bytes
- strings are typically contiguous character arrays plus a length or terminator convention
- byte loads/stores are intentionally deferred beyond the current MIPS subset

This area remains documentation-only in Phase 5.

## Arrays

Arrays are the main structured data object already exercised by the compiler/backend path.

Current machine-facing representation:

- arrays live in stack-resident contiguous word storage
- indexing is lowered to base-address plus scaled word offset
- the backend computes addresses explicitly before `lw`/`sw`

Conceptual layout for `var values: int[4]`:

```text
base + 0   -> values[0]
base + 4   -> values[1]
base + 8   -> values[2]
base + 12  -> values[3]
```

Because the current memory model is word-based, array elements are currently easiest to support
for 32-bit integer/boolean storage.

## Records / Struct-Like Layout

The Phase 2 frontend intentionally kept structs optional, so full struct lowering is still future
work. Phase 5 still documents the layout model because it is important for later compiler and
machine work.

Expected educational layout strategy:

- fields appear at fixed offsets from a base address
- alignment rules determine padding
- field loads/stores become offset(base) memory operations

Example conceptual layout:

```text
record Pair { first: int; second: int; }

base + 0 -> first
base + 4 -> second
```

This remains documentation-only until the language surface grows.

## Memory Layout Implications

The current stack frame organization matters for representation:

```text
low addresses
  local scalar slots
  local array storage
  parameter save slots
  temporary spill slots
  saved $fp
  saved $ra
high addresses
```

Consequences:

- locals and arrays have deterministic offsets from `$fp`
- call frames are easy to inspect in emitted MIPS
- all three execution modes see the same architectural layout
- representation decisions directly affect `lw`/`sw` address calculation

## Where To Look In Code

- arithmetic helpers: `src/common/src/arithmetic.cpp`
- backend frame and storage lowering: `src/compiler/backend_mips/src/codegen.cpp`
- functional reference: `src/sim/functional/src/interpreter.cpp`
- single-cycle execution: `src/sim/single_cycle/src/model.cpp`
- multi-cycle execution: `src/sim/multi_cycle/src/model.cpp`

## Phase 5 Limitations

- no floating-point execution support
- no byte/halfword memory operations
- no heap allocator or string runtime
- no struct lowering yet
- no trap/exception behavior for arithmetic overflow
