# VLSI Design and EDA (NEY705)

Library `src/vlsi/` (`nexus_vlsi`), commands under `nexus-eda` (`src/vlsi/src/cli.cpp`).
Tests: `nexus_vlsi_test`, `nexus_eda_cli`.

## Transistor and cell level

- **Switch-level simulation** (`cmos.cpp`): nMOS/pMOS switches, signal strengths (supply, driven,
  charged) and an X value resolved by a two-bound settle in the style of MOSSIM, so that charge
  storage and fights between drivers come out right. `nexus-eda sram` writes and reads a 6T SRAM
  cell with precharged bit lines.
- **Complex static CMOS gates** from a pull-down expression (`nexus-eda cmos "a b + c"` = AOI21):
  series/parallel networks, the dual pull-up, transistor sizing for equal drive, a check that pull-up
  and pull-down are complementary over all inputs.
- **Layout**: Euler paths for a single diffusion strip, stick diagram, lambda-rule cell layout and a
  design-rule checker (width, spacing, poly extension, contact enclosure).
- **Logical effort** (`logical_effort.cpp`): g and p derived from the sizing; path effort F = GBH,
  best number of stages and stage sizes, checked against a numerical optimum
  (`nexus-eda effort inv --cin 1 --cout 64`: 3 stages, delay 15).

## Logic synthesis and verification

- **And-inverter graphs** (`aig.cpp`) with structural hashing and balancing; BLIF input (for example
  from Yosys) and output.
- **Technology mapping** (`netlist.cpp`): cut enumeration and matching to a small static-CMOS
  library (INV, NAND2/3, NOR2/3, AOI21, OAI21), area or delay goal, fanout buffering; output as
  structural Verilog.
- **Equivalence**: random simulation, then a SAT miter (using the EY121 solver). `nexus_eda_cli` also
  has Yosys prove the mapped Verilog equivalent to the source BLIF (c17 and the CPU control unit).
- **Timing and power** (`analysis.cpp`): static timing with arrival, required time, slack and the
  critical path, compared with brute-force path enumeration; dynamic power from simulated switching
  activity and from signal-probability propagation (the gap between the two comes from reconvergent
  fanout).
- **Testing**: single stuck-at faults, 64-bit parallel fault simulation with random patterns, then
  SAT-based test generation; faults the SAT solver cannot excite are proved redundant (for
  `a b + a' c + b c` one fault of the consensus term is redundant, coverage 15/16).

## Physical design

- **Partitioning** (`partition.cpp`): Kernighan-Lin and Fiduccia-Mattheyses on the netlist hypergraph.
- **Floorplanning** (`floorplan.cpp`): slicing floorplans in normalised Polish expressions, Wong-Liu
  simulated annealing, compared with the exact optimum by dynamic programming over all slicing trees.
- **Placement** (`placement.cpp`): quadratic placement solved by conjugate gradients, legalisation to a
  slot grid, min-cut placement, simulated annealing.
- **Routing** (`routing.cpp`): Lee wave expansion and A* on a two-layer grid with vias, PathFinder
  negotiated congestion for global routing, left-edge channel routing with vertical constraints,
  rectilinear MST, iterated 1-Steiner and exact Steiner trees for small nets.

## The flow on a real block

The CPU control unit of `src/hdl/control/`, synthesised by Yosys to BLIF and run through
`nexus-eda flow`:

| step | result |
| --- | --- |
| AIG | 12 inputs, 15 outputs, 303 AND nodes, depth 41 |
| mapping | 263 cells, 1276 transistors; proved equivalent by SAT |
| bisection cut | random 158, Kernighan-Lin 37, Fiduccia-Mattheyses 20 |
| wirelength (HPWL) | random 5120, quadratic + legalisation 2439, min-cut 4888, quadratic + annealing 1584 |
| routing | 264 nets, overflow 0 after 4 negotiation iterations, 367 vias |
| critical delay | 246 tau with the wire-load model, 350 tau with routed wire capacitance |

## Limits

These are teaching-scale tools. The router is a global router on a coarse grid (no detailed routing,
no GDSII), and the flow skips routing above 1500 cells and Kernighan-Lin above 400. Delays are in
units of tau from logical effort, not from a characterised process library.
