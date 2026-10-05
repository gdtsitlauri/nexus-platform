# Register Allocation (Linear Scan)

`src/compiler/backend_mips/src/regalloc.cpp` implements linear-scan allocation (Poletto and Sarkar,
TOPLAS 1999) for the MIPS back end.

## Algorithm

1. **Candidates**: every IR value plus every scalar local whose address is never passed to a call.
2. **Live intervals**: block-level liveness is solved backwards to a fixed point; an interval is
   the hull of all definition/use positions and of every block boundary where the candidate is live,
   so a value live around a loop back edge covers the whole loop.
3. **Call crossing**: an interval that contains a `jal` may only use callee-saved registers
   (`$s0-$s7`, saved and restored by the prologue and epilogue). Other intervals prefer the
   caller-saved pool `$t4-$t9`. `$t0-$t3` stay reserved as code-generation scratch registers.
4. **Scan**: intervals are visited by start point, expired intervals free their register, and when no
   register is free the interval that ends last is spilled (it keeps its stack slot).

## Usage

```bash
./build/bin/nexusc compile prog.nx -S --regalloc linear-scan -o prog.s
./build/bin/nexusc analysis regalloc tests/programs/register_pressure.nx   # intervals and assignment
```

`register_pressure.nx` keeps twenty scalars live at once, more than the 14 allocatable registers, so
it exercises spilling (10 of 181 intervals spill).

## Effect (dynamic instruction count, functional simulator)

| program | stack-only | linear scan | reduction |
| --- | --- | --- | --- |
| factorial | 203 | 145 | 28.6% |
| ackermann | 2532 | 1935 | 23.6% |
| gcd_and_primes | 11061 | 5432 | 50.9% |
| matrix_multiply | 3417 | 2042 | 40.2% |
| register_pressure | 1894 | 801 | 57.7% |

## Verification

- `nexus_cross_model_differential`: every test program, compiled both ways, must exit with the same
  code on 12 CPU models as on the functional interpreter, and linear scan may never execute more
  instructions;
- `nexus_hdl_pipeline_cosim`: the allocated code also runs unchanged on the Verilog CPU.
