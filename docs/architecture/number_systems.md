# Number Systems, Computer Arithmetic and Character Data

`src/common/src/number_systems.cpp`, exposed through `mips-sim arith`.

## IEEE-754 binary32 in integer arithmetic

`soft_add`, `soft_sub`, `soft_mul` and `soft_div` implement single precision with:
- round to nearest, ties to even, using an exact significand plus a sticky bit;
- gradual underflow (subnormal inputs and results);
- signed zeros (`+0 + -0 = +0`, `-0 + -0 = -0`), infinities and quiet NaNs (`inf - inf`, `0 * inf`,
  `0 / 0`).

```bash
./build/bin/mips-sim arith float 0.1 add 0.2
a        = 0 01111011 10011001100110011001101  (normal, 0.100000001)
b        = 0 01111100 10011001100110011001101  (normal, 0.200000003)
software = 0 01111101 00110011001100110011010  (normal, 0.300000012)
hardware = 0 01111101 00110011001100110011010  (normal, 0.300000012)
bit-exact match
```

`nexus_number_systems_test` compares all four operations with the host FPU on 2,000,000 random
operand pairs (all bit patterns, including NaN, infinities and subnormals) plus 18x18 special values,
and requires bit-identical results (NaN payloads aside).

## Integer representations

`mips-sim arith repr <value> [bits]` prints sign-magnitude, one's complement, two's complement,
excess-(2^(n-1)-1) (the IEEE exponent bias) and packed BCD, and reports values that do not fit (e.g.
-128 fits 8-bit two's complement but not sign-magnitude).

## Arithmetic algorithms

| algorithm | command | checked against |
| --- | --- | --- |
| Booth radix-2 multiplication (registers A, Q, Q-1, step trace) | `mips-sim arith booth -3 5 4` | native multiply, 20,000 random 32-bit pairs |
| non-restoring division (step trace) | `mips-sim arith divide 13 3 4` | native divide and remainder |
| carry-lookahead adder (4-bit groups, second lookahead level, gate depth vs ripple) | `mips-sim arith cla a b` | native add with carry in/out |
| shift-add multiplier, restoring divider, ripple-carry adder | `src/common/src/arithmetic.cpp` | `nexus_arithmetic_units_test` |
| HDL adder, ALU, iterative multiplier and divider | `src/hdl/alu/` | `nexus_hdl_*` testbenches |

## Characters

`utf8_encode` / `utf8_decode` implement UTF-8 strictly: overlong forms, encoded surrogates, values
above U+10FFFF and truncated sequences are rejected. The test round-trips code points across the
whole Unicode range. (On Windows, command-line arguments arrive in the ANSI code page, so
`mips-sim arith utf8` accepts only text that the code page can represent.)
