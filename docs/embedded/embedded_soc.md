# Embedded Systems and the NEXUS SoC (NEY604)

Verilog in `src/hdl/soc/`, flow script `scripts/soc_flow.py`, models in `src/embedded/`
(`nexus-embedded`). Tests: `nexus_soc_levels`, `nexus_soc_fpga`, `nexus_embedded_test`,
`nexus_embedded_rt_cli`.

## System on chip

`nexus_soc.v` connects the 5-stage pipelined MIPS core (`src/hdl/cpu_pipeline/`, built with
`ENABLE_IO=1`) to memory-mapped peripherals:

| address | device |
| --- | --- |
| 0x10000 | UART transmitter, 8N1; status bit 0 = ready |
| 0x10100 | timer: period, enable, interrupt acknowledge |
| 0x10300 / 0x10304 | GPIO: LEDs out / switches in |

Addresses at 0x10000 and above leave the data memory and go to the bus. On a timer interrupt the
core drains its pipeline, saves the return PC in `$ra` and jumps to the `interrupt_handler` label;
`jr $ra` in the handler returns and restores the saved `$ra`.

## The same program at four levels

`scripts/soc_flow.py` runs the examples on up to four levels:
1. the ISA-level simulator (`mips-sim --mode functional`), which has the UART of the memory map
   but no timer interrupts;
2. the cycle-level C++ pipeline model, which models console output and the timer but not GPIO;
3. the Verilog SoC in Icarus Verilog, with a testbench that decodes the UART serial line;
4. the gate-level netlist that Yosys synthesises from the same RTL (`--gates`).

| program | levels | result (identical wherever it runs) |
| --- | --- | --- |
| `examples/embedded/hello_uart.s` | ISA level, C++ pipeline, RTL, gate level | UART prints `HELLO NEXUS`, exit 12 |
| `examples/flow/flow_demo.nx` (compiled) | 10 CPU models, RTL, gate level, FPGA | UART prints `S=5913`, exit 25 |
| `examples/embedded/gpio_echo.s` | RTL | reads the switches, writes the LEDs, exit 5 |
| `examples/os/rr_kernel.s` | C++ pipeline, RTL | UART prints the dispatch order `012012121`, 9 timer interrupts |

## FPGA

`nexus_soc_fpga.v` adds a boot ROM that copies the program into instruction memory after reset.
`soc_flow.py --fpga` runs Yosys `synth_ecp5`, nextpnr-ecp5 (Lattice ECP5-85F) and ecppack, and reads
the bitstream back with ecpunpack:

| item | value |
| --- | --- |
| logic cells / flip-flops | 5983 / 1592 (of 83640) |
| block RAM, DSP | 3 DP16KD, 4 MULT18X18D |
| maximum frequency | 6.8 MHz |

The low clock comes from the single-cycle combinational divider of the core, which is the critical
path; the iterative divider in `src/hdl/alu/` would remove it. No board pin constraints are given
and the bitstream has not been run on a board.

## Real-time and models

- **Real-time scheduling** (`models.cpp`): Liu-Layland and hyperbolic bounds, exact response-time
  analysis, EDF with the processor-demand test, rate-monotonic, deadline-monotonic and EDF simulation
  over the hyperperiod. On 400 random task sets the exact tests agree with simulation and the bounds
  are never violated. `examples/embedded/tasks.rt` has U = 0.83 above the Liu-Layland bound and is
  still RM-schedulable (response times 1, 3, 10).
- **Priority inversion**: with a medium-priority task of 5 units, the high-priority task finishes at
  9 without a protocol and at 4 with priority inheritance (`nexus-embedded inversion`).
- **Synchronous dataflow**: repetition vector, a single-processor schedule, buffer sizes, detection
  of inconsistent rates and of deadlocked cycles (`nexus-embedded sdf`).
- **Petri nets**: reachability, boundedness and deadlock markings (producer-consumer, mutual
  exclusion, two locks taken in opposite order).
- Finite-state machines are covered by the EY121 tools (`docs/logic/logic_design.md`).
