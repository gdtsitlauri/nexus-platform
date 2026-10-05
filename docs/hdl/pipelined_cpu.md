# Synthesisable 5-Stage Pipelined MIPS Core

`src/hdl/cpu_pipeline/nexus_mips_pipeline.v` (testbench `mips_pipeline_tb.v`).

## Design

- stages IF, ID, EX, MEM, WB with the real MIPS32 encodings of the Nexus subset:
  `add addu sub and or xor slt sltu sll jr mult div mfhi mflo addiu ori xori sltiu lui lw sw beq bne j jal`;
- data hazards: EX/MEM -> EX and MEM/WB -> EX forwarding, a write-before-read register file and a
  one-cycle load-use interlock;
- control hazards: branches and jumps resolve in EX and flush the two younger instructions;
- HI/LO: `mult`/`div` update HI/LO in EX, so a following `mfhi`/`mflo` always sees the new value;
- the PC counts instruction words (as the simulators do); `jr $ra` to the reset value 0x80000000 of
  `$ra` halts after the pipeline drains, with the exit code in `$v0`;
- a program-load port (`imem_we`) writes instruction memory, so the core can be used without a
  simulator-only `$readmemh`.

## Toolchain flow

```bash
./build/bin/nexusc compile prog.nx -S --regalloc linear-scan -o prog.s
./build/bin/mips-sim encode prog.s -o prog.hex          # prints "entry N words M"
iverilog -g2012 -o cpu.vvp src/hdl/cpu_pipeline/nexus_mips_pipeline.v src/hdl/cpu_pipeline/mips_pipeline_tb.v
vvp cpu.vvp +program=prog.hex +entry=N
EXIT 192 INSTRET 2042 CYCLES 2385 STALLS 59 FLUSHES 282
```

## Verification (`nexus_hdl_pipeline_cosim`)

Every program in `tests/programs/` plus `factorial.nx`, compiled with and without register
allocation (16 runs), must give the same exit code **and the same retired-instruction count** on the
RTL core as on the functional simulator. Cycle counts are within a few percent of the C++ pipeline
model. When Yosys is installed the test also synthesises the core (`synth -top nexus_mips_pipeline`).
A full generic synthesis with 256-word memories maps to about 60,000 cells, most of them memory
flip-flops.

## Limits

Multiply and divide are single-cycle combinational blocks (the iterative units in `src/hdl/alu/`
show the multi-cycle alternative). Memories are plain arrays, not vendor block RAM. The core has not
been placed and routed on an FPGA.
