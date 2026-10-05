# Instruction-Set Styles: Stack, Accumulator, Register-Memory and Load/Store

`src/compiler/isa_styles/` compiles the same NexusLang IR to three classic instruction-set styles,
each with its own interpreter, and compares them with the MIPS (load/store, general-purpose
register) back end.

| style | model | operands | representative instructions |
| --- | --- | --- | --- |
| stack | JVM-like bytecode | zero-address, operand stack | `iload`, `istore`, `iadd`, `if_icmplt`, `invokestatic`, `iaload`, `ireturn` |
| accumulator | one-address machine | one implicit accumulator + one memory operand | `load`, `store`, `add m`, `clt m`, `jnz`, `param`, `call` |
| register-memory | IA-32-like | two-address, `[ebp+disp]` and `[base+index*4]` operands | `mov`, `add r,[m]`, `imul`, `cdq`/`idiv`, `cmp`/`setcc`/`movzx`, `push`, `call`, `ret` |
| load-store | MIPS | three-address registers, only `lw`/`sw` touch memory | the Nexus MIPS subset |

Design notes:
- the stack style uses the javac pattern for comparisons (`if_icmp<not cond> L; iconst_1; goto E;
  L: iconst_0; E:`) because the JVM has no "compare and push" instruction;
- the register-memory style follows cdecl: arguments pushed right to left, `push ebp; mov ebp, esp;
  sub esp, N`, result in `eax`, caller pops arguments;
- encoded sizes follow the real encodings (JVM: `iload_0..3` 1 byte, `bipush` 2 bytes; IA-32: ModRM
  with 8- or 32-bit displacement; MIPS: 4 bytes per instruction).

## Usage

```bash
./build/bin/nexusc isa tests/programs/matrix_multiply.nx              # comparison table
./build/bin/nexusc isa examples/source_lang/factorial.nx --style stack --listing
```

## Example: 3x3 matrix multiplication (`tests/programs/matrix_multiply.nx`)

| style | static instructions | code bytes | dynamic instructions | data reads | data writes |
| --- | --- | --- | --- | --- | --- |
| stack (JVM-like) | 297 | 513 | 2557 | 966 | 625 |
| accumulator | 271 | 789 | 2250 | 1082 | 741 |
| register-memory (IA-32-like) | 246 | 1150 | 2105 | 970 | 628 |
| load-store (MIPS, every value in the frame) | 403 | 1612 | 3417 | 1301 | 960 |
| load-store (MIPS + linear-scan allocation) | 266 | 1064 | 2042 | 131 | 39 |

The textbook trade-offs appear directly: the stack code is the densest (about 1.7 bytes per
instruction), register-memory instructions do the most work each, and a load/store machine is only
competitive once a register allocator keeps values out of memory (memory traffic drops about 13x).

## Verification

`tests/integration/isa_styles_test.py` (`nexus_isa_styles`) runs every test program on all five
configurations and requires identical results, stack code denser than MIPS, and the lowest memory
traffic for MIPS with register allocation.
