# MIPS ISA

Phases 4 through 6 implement a bounded educational MIPS-like subset that is sufficient for the
current NexusLang frontend and middle-end examples. The emphasis is correctness, determinism, clear
stack-frame behavior, and explicit organization-level execution models rather than binary encoding
fidelity.

## Implemented Scope Through Phase 6

Implemented instruction families:

- arithmetic and logic:
  - `add`, `addu`, `sub`
  - `and`, `or`, `xor`
  - `slt`, `sltu`
- immediate and shift support:
  - `addiu`, `ori`, `xori`, `sltiu`
  - `sll`, `lui`
- memory:
  - `lw`, `sw`
- control flow:
  - `beq`, `bne`
  - `j`, `jal`, `jr`
- multiply and divide support:
  - `mult`, `div`
  - `mflo`, `mfhi`

The current backend and simulators intentionally do not model:

- branch delay slots
- binary encodings
- exceptions or overflow traps
- byte loads/stores
- floating-point instructions

## Register Naming

The implemented named registers are:

| Register | Purpose in current phases |
| --- | --- |
| `$zero` | hard-wired zero |
| `$v0` | return value register |
| `$a0` - `$a3` | first four argument registers |
| `$t0` - `$t7` | scratch temporaries used by backend lowering |
| `$sp` | stack pointer |
| `$fp` | frame pointer |
| `$ra` | return address |

The backend uses a simple fixed-register discipline:

- all IR locals and values live in stack slots
- `$t0` - `$t3` are used as short-lived scratch registers within emitted instruction sequences
- no global register allocation is attempted yet

## Calling Convention Assumptions

The current implementation uses a small, explicit convention:

- up to 4 arguments are passed in `$a0` - `$a3`
- return values are passed in `$v0`
- arrays are passed by address
- the backend treats `$t0` - `$t7` as caller-saved scratch registers
- `jal` writes the return target to `$ra`
- callee epilogues return with `jr $ra`

If a function requires more than 4 arguments, the current backend reports a diagnostic
instead of silently emitting incorrect code.

## Stack Frame Layout

After the prologue, `$fp` is set equal to the post-allocation stack pointer. Offsets are positive
from `$fp`.

Frame layout used by the current backend:

```text
low addresses
  local scalar slots
  local array storage
  parameter save slots
  IR temporary/value slots
  saved $fp
  saved $ra
high addresses
```

Representative prologue:

```text
addiu $sp, $sp, -56
sw    $ra, 52($sp)
sw    $fp, 48($sp)
or    $fp, $sp, $zero
```

Representative epilogue:

```text
lw    $fp, 48($sp)
lw    $ra, 52($sp)
addiu $sp, $sp, 56
jr    $ra
```

## Memory Model Assumptions

The functional, single-cycle, multi-cycle, and pipelined simulators use a simple in-memory word array:

- addresses are 32-bit signed values in the simulator implementation
- only word-aligned accesses are supported
- `lw` and `sw` operate on 4-byte words
- the stack grows downward from the top of simulator memory
- `main` is the entry label
- returning from `main` via `jr $ra` halts when `$ra` contains the simulator sentinel value

There is no heap, MMIO, interrupt handling, or cache behavior through Phase 6.

## Phase 6 Validation Modes

The same loaded MIPS subset can now be validated through four views:

- `--mode functional`: correctness-first reference execution
- `--mode single-cycle`: one architectural instruction per educational cycle
- `--mode multi-cycle`: multi-state execution with either hardwired or microcode control framing
- `--mode pipeline`: overlapping IF/ID/EX/MEM/WB execution with hazards, forwarding, and static prediction

This keeps architectural results aligned while making control/datapath structure visible.

## Backend Mapping Notes

The Phase 4 backend lowers the current IR as follows:

- IR constants become `addiu` or `lui`/`ori`
- scalar locals and temporaries are spilled to stack slots
- comparisons become `slt`, `xori`, `sltiu`, or `sltu` combinations
- multiplication and division use `mult`/`div` followed by `mflo`/`mfhi`
- conditionals lower to `bne` plus explicit `j` fallthrough edges
- returns jump to a shared function epilogue

## Example

Excerpt from `nexusc compile examples/source_lang/factorial.nx -S`:

```text
factorial:
  addiu $sp, $sp, -56  # allocate 56-byte frame
  sw $ra, 52($sp)
  sw $fp, 48($sp)
  or $fp, $sp, $zero
  sw $a0, 0($fp)  # save parameter n
  ...
  jal factorial
  ...
  jr $ra
```

## Limitations

- no delay slots
- no caller stack-argument area for more than 4 parameters
- no byte/halfword operations
- no floating-point state
- no external assembler compatibility guarantee beyond this internal educational subset
