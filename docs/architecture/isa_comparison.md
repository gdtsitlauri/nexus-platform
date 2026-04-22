# ISA Comparison

Phases 4 and 5 provide both a real general-purpose-register execution path in the repository and
worked comparative examples for accumulator, stack, and GPR styles.

## Worked Example 1: `x = (a + b) * c`

### Accumulator Style

```text
LOAD a
ADD  b
MUL  c
STORE x
```

Characteristics:

- compact state model
- implicit accumulator register
- many instructions overwrite the only working register
- compilers often need extra stores when multiple values must remain live

### Stack Style

```text
PUSH a
PUSH b
ADD
PUSH c
MUL
POP  x
```

Characteristics:

- operands are implicit on the stack top
- easy mapping from expression trees or postfix notation
- frequent stack traffic
- close to Java bytecode execution style

### General-Purpose-Register Style

```text
ADD r1, ra, rb
MUL rx, r1, rc
```

Characteristics:

- explicit data flow between named registers
- fewer memory operations when values stay in registers
- more flexible instruction scheduling and calling conventions
- this is the style used by the implemented Phase 4 MIPS backend

## Worked Example 2: Conditional Branch

High-level fragment:

```text
if (x != 0) y = 1 else y = 2
```

Accumulator sketch:

```text
LOAD x
JZ   else
LOADI 1
STORE y
JMP  done
else:
LOADI 2
STORE y
done:
```

Stack-machine sketch:

```text
PUSH x
JZ   else
PUSH 1
POP  y
JMP  done
else:
PUSH 2
POP  y
done:
```

MIPS-style GPR sketch:

```text
beq  $t0, $zero, else
addiu $t1, $zero, 1
sw   $t1, 0($fp)
j    done
else:
addiu $t1, $zero, 2
sw   $t1, 0($fp)
done:
```

## IA-32 And Java Bytecode Concepts

### IA-32 Concepts

- rich addressing modes can fold memory access and arithmetic together
- partial-register and flag behavior make code generation more stateful
- compared with the current MIPS subset, IA-32 typically has denser encodings but more implicit
  machine state

### Java Bytecode Concepts

- stack-based operand model
- explicit local-variable slots separate from operand stack
- natural fit for portable virtual-machine execution
- compared with MIPS, bytecode is easier to emit from expression trees but usually requires later
  lowering or JIT compilation for efficient native execution

## Code Density And Compiler Implications

| Style | Typical code density | Compiler impact | Execution model impact |
| --- | --- | --- | --- |
| Accumulator | high for simple straight-line code | simple instruction selection, more spills | heavy dependence on one implicit register |
| Stack | moderate to high | direct expression lowering, more stack shuffling | easy interpretation, harder local optimization |
| GPR / MIPS | lower textual density but clearer data flow | better optimization hooks and calling-convention control | explicit operands, easier CFG and data-flow reasoning |

## Repository Status

- implemented: general-purpose-register MIPS subset with backend emission plus functional,
  single-cycle, and multi-cycle execution
- documented with worked examples: accumulator, stack, IA-32 concept sketches, Java bytecode concept sketches
- pending: later comparative measurements of code size and performance across richer backends
