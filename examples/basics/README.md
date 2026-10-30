# Small examples: start here

Run commands from the repository root. Build the two tools once using the [README](../../README.md).
On native Windows the tools are `build/bin/mips-sim.exe` and `build/bin/nexusc.exe`.
The Python demonstrations only need Python 3; OpenMP/MPI and the C pointer example have separate
compiler commands in their sources and in [NEY704](../../docs/courses/NEY704.md) / [NEY709](../../docs/courses/NEY709.md).

Each example shows one idea: run it and read the state changes in its trace. For the connections between subjects,
follow [one sum through all six courses](../../docs/FLOW.md).
The expected result below is the value printed by NEXUS as `Program exited with code ...`.
The simulator process itself returns 0 when simulation succeeds; those are different exit codes.

## MIPS, one idea at a time

```bash
build/bin/mips-sim run examples/basics/mips/01_add.s --mode functional --trace --stats
```

| File | Idea | Result |
| --- | --- | --- |
| [01_add.s](mips/01_add.s) | registers, immediate, addition | 12 |
| [02_memory.s](mips/02_memory.s) | store/load an addressed word | 13 |
| [03_if.s](mips/03_if.s) | comparison and conditional branch | 1 |
| [04_sum.s](mips/04_sum.s) | repeated updates and loop exit | 10 |
| [05_call.s](mips/05_call.s) | argument, return value and return address | 9 |
| [06_stack.s](mips/06_stack.s) | save/restore return address on stack | 9 |
| [07_forwarding.s](mips/07_forwarding.s) | bypass a result | 10 |
| [08_load_use.s](mips/08_load_use.s) | wait for a load | 10 |
| [09_flush.s](mips/09_flush.s) | discard the wrong branch path | 7 |
| [10_scheduling.s](mips/10_scheduling.s) | independent work around a multiply | 16 |
| [11_cache.s](mips/11_cache.s) | three reads, two cache lines | 0 |

The [NEY221 guide](../../docs/courses/NEY221.md) explains the first six; [EY321](../../docs/courses/EY321.md)
explains pipeline/cache examples; [NEY606](../../docs/courses/NEY606.md) explains scheduling.
NEXUS uses an aligned-word MIPS subset with no architectural delay slots. C++ trace PC values count
instructions; multiply by four for the corresponding instruction byte address.

## Compiler, same input at every stage

Start with [tiny.nx](../source_lang/tiny.nx) and follow the commands at the start of
[NEY613](../../docs/courses/NEY613.md). The result is 14; parenthesising `(2+3)*4` changes it to 20.

| Input | Use | Result or observation |
| --- | --- | --- |
| [addition.g](compiler/addition.g) | hand-build LL(1) FIRST/FOLLOW and table | `2+3` accepts, value 5; `2+` rejects |
| [diamond.nx](compiler/diamond.nx) | CFG, dominance, SSA, data flow, generated code | result 3; `choose` has one phi |
| [constant_branch.nx](compiler/constant_branch.nx) | SCCP within one function | result 3; else path proved unreachable |
| [sum.nx](compiler/sum.nx) | loop analysis and execution | result 10; argument 5 gives 15 |
| [reaching_definitions.py](compiler/reaching_definitions.py) | visible propagation rounds | d1 and d2 reach the diamond join |
| [alias.c](compiler/alias.c) | two pointers may name the same object | distinct objects: 1; same object: 2 |
| [points_to.py](compiler/points_to.py) | propagate addresses through pointer copies, loads and stores | fixed-point sets; conservative overlap for r and q |

## Predication: guarded effects

```bash
python examples/basics/predication.py
```

[predication.py](predication.py) implements a guarded register write. Only the active predicate
updates the destination and evaluates its expression. The maximum is 7 for both input orders;
a guarded division with zero denominator retains the old register value 99. The
[NEY606 explanation](../../docs/courses/NEY606.md#predication-select-a-result-without-a-control-flow-branch)
distinguishes these semantics from eager evaluation and from real CPU timing.

## Arithmetic and parallelism without a large setup

```bash
python examples/basics/full_adder.py 7 1
python examples/basics/parallel/race.py
python examples/basics/parallel/store_buffer.py
```

Read [full_adder.py](full_adder.py) to see carries, [race.py](parallel/race.py) to see a chosen
lost-update interleaving, and [store_buffer.py](parallel/store_buffer.py) to see chosen memory-order events.
They are small executable explanations, not full hardware/thread emulators.
Then run the complete [OpenMP sum](parallel/sum_openmp.c) and [two-rank MPI message](parallel/hello_mpi.c)
using the commands in [NEY704](../../docs/courses/NEY704.md#5-programming-shared-memory-openmp).

To check the reproducible core lessons after a build:

```bash
python tests/integration/teaching_examples_test.py build/bin/nexusc build/bin/mips-sim .
```

On native Windows use the two `.exe` paths. Optional real C/OpenMP/MPI examples have separate CTest
checks, reported as skipped if their compiler/runtime is absent. The simulations remain available.
