# Version 2.0.0 on Google Colab (Linux, GCC), 2026-10-14

Setup: Colab CPU runtime; `apt-get install ninja-build flex bison iverilog yosys`; OpenMPI and OpenMP
preinstalled; no nextpnr; `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`. The build finished
without errors.

## The full flow (`python3 scripts/nexus_flow.py build/bin --gates`)

```
1. compiler: NexusLang -> MIPS
  ok   nexusc compile --regalloc none                             flow_none.s
  ok   nexusc compile --regalloc linear-scan                      flow_linear-scan.s
  ok   SSA form of the same program                               phi nodes present
2. CPU models: the same binary everywhere                         (20 runs, all ok)
3. hardware: MIPS32 machine code on the Verilog SoC
  ok   SoC RTL (Icarus Verilog)                                   uart 'S=5913\n' exit 25
  ok   gate-level netlist (Yosys)                                 uart 'S=5913\n' exit 25
4. chip design: the CPU's control unit through nexus-eda
  ok   Yosys: control unit -> BLIF                                control.blif
  ok   mapping proved equivalent (SAT)                            260 cells
  ok   placement and routing                                      overflow 0
5. FPGA
  skip (run with --fpga)
6. OS: the kernel on the SoC against the scheduling library
  ok   kernel dispatch order = RR Gantt chart                     SoC '012012121', nexus-os '012012121'
PASS
```

## POSIX lab (`ctest -R nexus_posix_lab -V`, earlier run of the same day)

```
77: nexus_posix_fork_pipe: ok total 5050
77: nexus_posix_exec_signal: ok child replaced by exec
77: nexus_posix_threads: ok counter 400000
```

## Full suite (`ctest -j2`, tail of the output)

```
73/81 Test #65: nexus_cross_model_differential ......   Passed   50.10 sec
75/81 Test #78: nexus_posix_lab .....................   Passed    0.11 sec
79/81 Test #74: nexus_soc_levels ....................   Passed  373.22 sec
80/81 Test #75: nexus_soc_fpga ......................***Skipped   0.06 sec
81/81 Test #76: nexus_full_flow .....................   Passed  198.14 sec

100% tests passed, 0 tests failed out of 81

Total Test time (real) = 589.46 sec

The following tests did not run:
	 56 - nexus_toolchain_compare (Skipped)
	 75 - nexus_soc_fpga (Skipped)
```
