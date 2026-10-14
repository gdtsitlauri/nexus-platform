# Logic Design Tools (EY121)

Library `src/logic/` (`nexus_logic`), command-line tool `nexus-eda`, Verilog in `src/hdl/logic/`.
Tests: `nexus_logic_test`, `nexus_eda_cli`, `nexus_hdl_logic_comb`, `nexus_hdl_logic_seq`.

## Combinational logic

- **Expressions and truth tables** (`boolean.cpp`): `a b' + c`, `~`, `^`, parentheses; truth table,
  minterms, maxterms, canonical SOP and POS, NAND-only and NOR-only rewriting.
- **Two-level minimisation** (`minimize.cpp`): Quine-McCluskey tabulation with don't cares, prime
  implicant chart, essential primes, Petrick's method and an exact branch-and-bound cover, minimal POS
  through the complement. Karnaugh maps of 2-4 variables are printed with the chosen groups. The test
  compares the cost with exhaustive search on random functions.
- **Other representations**: reduced ordered BDDs with a chosen variable order (the size depends on
  it: a1 b1 + a2 b2 + a3 b3 has 6 nodes with interleaved and 14 with separated variables); Tseitin CNF in DIMACS;
  a CDCL SAT solver (watched literals, first-UIP learning, VSIDS, restarts). `nexus-eda equiv` decides
  equivalence three ways (truth table, same BDD node, SAT on a miter).
- **Codes** (`codes.cpp`): Gray code, parity, Hamming single-error correction.
- **Programmable logic** (`pla.cpp`, `nexus-eda pla`): the PLA programming table with product terms
  shared between outputs and a choice of true or complemented output (the XOR at each output). The
  fewest AND-array rows are found by branch and bound over the primes of every product of outputs;
  on 3-input functions the row count equals a brute-force search over all cubes. The tool also gives
  the PAL alternative (each output with its own terms) and the ROM table. In the textbook example
  F1 = sum(0,1,2,4), F2 = sum(0,5,6,7) the PLA needs 4 rows with F1 complemented, 6 without, and a PAL
  3 + 3 terms.
- **Building blocks** in Verilog (`nexus_combinational.v`): multiplexer, decoder, priority encoder,
  comparator, BCD to 7-segment, binary/Gray converters, each with an exhaustive testbench.

```bash
nexus-eda minimize -n 4 -m 4,8,10,11,12,15 -d 9,14     # terms: 3, literals: 7
nexus-eda equiv "a b + a' c + b c" "a b + a' c"        # consensus theorem
nexus-eda sat examples/eda/php54.cnf                   # pigeonhole 5 -> 4: UNSATISFIABLE
nexus-eda pla -n 3 -m "0,1,2,4;0,5,6,7" --rom          # PLA 4 rows, PAL 6 terms, ROM 8 x 2
```

## Sequential logic

- **Latches and flip-flops at gate level** (`gatesim.cpp`): an event-driven simulator with gate
  delays shows the SR NOR latch (including the forbidden input and oscillation when both inputs are released
  together with equal gate delays), the gated D latch and the master-slave flip-flop (`nexus-eda latch sr|d|msff`).
- **Registers and counters** in Verilog (`nexus_sequential.v`): D, JK and T flip-flops, a universal
  shift register, a synchronous modulo-N up/down counter and a ripple counter.
- **State machines** (`fsm.cpp`, file format in `examples/eda/*.fsm`): Mealy and Moore machines,
  state tables, minimisation by partition refinement (the traffic-light example goes from 5 to 4
  states), equivalence of two machines by a product-machine search.
- **Analysis**: flip-flop input and output equations (`examples/eda/mano_d.seq`) give the state
  table (`nexus-eda analyze`).
- **Synthesis**: state assignment (binary, Gray, one-hot), excitation tables for D, T and JK
  flip-flops, minimised next-state and output equations, and structural Verilog
  (`nexus-eda fsm synth|verilog`). `nexus_eda_cli` co-simulates the behavioural and the synthesised
  Verilog with Icarus against the state-table simulation for 2 machines x 4 encoding/flip-flop
  combinations on random inputs.

## Limits

Truth-table methods (Quine-McCluskey, K-maps) are exponential in the number of variables; the SAT and
BDD paths are the ones that scale.
The gate simulator uses unit or given delays, not electrical timing (that is the VLSI part).
