#!/usr/bin/env python3
"""Integration test of nexus-eda (EY121 logic design, NEY705 VLSI/EDA).

1. Command-line results against textbook answers.
2. FSM -> Verilog: the behavioural and the synthesised structural Verilog of each FSM, for every encoding and
   flip-flop type, are simulated with Icarus Verilog on a random input sequence and compared with nexus-eda's own
   simulation of the state table (skipped without iverilog).
3. Technology mapping checked by an independent tool: Yosys proves the mapped Verilog equivalent to the source
   BLIF with its own SAT solver (skipped without yosys).
Usage: eda_cli_test.py <nexus-eda> <project root>
"""
from __future__ import annotations

import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

EDA = sys.argv[1]
ROOT = Path(sys.argv[2])
EX = ROOT / "examples" / "eda"
failures = 0


def run(*args: str, ok_codes=(0,)) -> str:
    r = subprocess.run([EDA, *args], text=True, capture_output=True, check=False)
    if r.returncode not in ok_codes:
        raise SystemExit(f"nexus-eda {' '.join(args)} exited {r.returncode}\n{r.stdout}{r.stderr}")
    return r.stdout


def expect(cond: bool, what: str) -> None:
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)


# 1. textbook answers -------------------------------------------------------------------------------------------
out = run("minimize", "-n", "4", "-m", "4,8,10,11,12,15", "-d", "9,14")
expect("terms: 3, literals: 7" in out, "QM textbook example cost")
out = run("truth", "a b' + c")
expect("m(1,3,4,5,7)" in out, "minterms of a b' + c")
out = run("equiv", "a b + a' c + b c", "a b + a' c")
expect(out.count("equivalent") >= 2 and "same node" in out, "consensus theorem by truth table, BDD and SAT")
out = run("equiv", "a ^ b", "a + b", ok_codes=(0,))
expect("counterexample" in out, "counterexample for a different pair")
out = run("cmos", "a b + c")
expect("a=2 b=2 c=1.66667" in out and "DRC clean" in out, "AOI21 logical effort and clean layout")
out = run("sram")
expect("write 1 (WL=1, BL=1, BLB=0): Q=1 QB=0" in out and "write 0: Q=0 QB=1" in out, "6T SRAM at switch level")
out = run("effort", "inv", "--cin", "1", "--cout", "64")
expect("best number of stages (adding inverters): 3 with delay 15" in out, "inverter chain driving 64")
out = run("fsm", "minimize", str(EX / "traffic.fsm"))
expect("states: 5 -> 4" in out, "traffic light minimisation")
out = run("analyze", str(EX / "mano_d.seq"))
expect(" S01 | S00/1 | S11/0" in out, "analysis of a D flip-flop circuit")
out = run("synth", str(EX / "c17.blif"))
expect("PROVED" in out and "area 24 transistors" in out, "c17 mapped to 24 transistors and proved")
out = run("channel", "1,1,2,0,3,4,0,5", "0,2,3,3,0,5,4,0")
expect("tracks" in out, "channel routing")
out = run("steiner", "0,0", "4,0", "2,3", "2,-3")
expect("MST: 14" in out and "exact RSMT: 10" in out, "Steiner tree of a cross: MST 14, optimum 10")
out = run("sat", str(EX / "php54.cnf"), ok_codes=(20,))
expect("UNSATISFIABLE" in out, "pigeonhole formula is UNSAT")
out = run("pla", "-n", "3", "-m", "0,1,2,4;0,5,6,7", "--rom")
expect("PLA with output inversion: 4 product terms" in out and "without output inversion: 6" in out
       and "every output correct" in out and "ROM: 8 x 2 bits" in out, "PLA example: 4 terms with inversion, 6 without")

# 2. FSM Verilog co-simulation -------------------------------------------------------------------------------------
have_iverilog = shutil.which("iverilog") is not None and shutil.which("vvp") is not None
if have_iverilog:
    rnd = random.Random(121)
    for fsm in ("det1011.fsm", "traffic.fsm"):
        inputs = [rnd.randint(0, 1) for _ in range(64)]
        want = run("fsm", "simulate", str(EX / fsm), "--inputs", ",".join(map(str, inputs))).strip()
        for enc, ff in (("binary", "D"), ("gray", "T"), ("binary", "JK"), ("onehot", "D")):
            verilog = run("fsm", "verilog", str(EX / fsm), "--encoding", enc, "--ff", ff)
            name = verilog.split("module ")[1].split("_behav")[0]
            with tempfile.TemporaryDirectory() as tmp:
                t = Path(tmp)
                (t / "fsm.v").write_text(verilog)
                stim = "\n".join(f"    x = {v}; #1; $write(\"%0d,%0d \", b.z, s.z); @(posedge clk); #1;" for v in inputs)
                (t / "tb.v").write_text(f"""`timescale 1ns/1ps
module tb;
  reg clk = 0, rst = 1; reg [0:0] x = 0;
  wire [{2 if fsm == 'traffic.fsm' else 1}-1:0] zb, zs;
  {name}_behav b(.clk(clk), .rst(rst), .x(x), .z(zb));
  {name}_min_struct s(.clk(clk), .rst(rst), .x(x), .z(zs));
  always #5 clk = ~clk;
  initial begin
    @(posedge clk); #1 rst = 0;
{stim}
    $finish;
  end
endmodule
""")
                comp = subprocess.run(["iverilog", "-g2012", "-o", str(t / "sim"), str(t / "fsm.v"), str(t / "tb.v")],
                                      text=True, capture_output=True, check=False)
                if comp.returncode != 0:
                    expect(False, f"{fsm} {enc}/{ff}: Verilog does not compile\n{comp.stderr}")
                    continue
                sim = subprocess.run(["vvp", "-n", str(t / "sim")], text=True, capture_output=True, check=False)
                pairs = [p.split(",") for p in sim.stdout.split() if "," in p]
                behav = ",".join(p[0] for p in pairs)
                struct = ",".join(p[1] for p in pairs)
                # the behavioural model is the original machine, the structural one is synthesised from the minimised FSM
                expect(behav == want, f"{fsm} {enc}/{ff}: behavioural Verilog equals the state-table simulation")
                expect(struct == want, f"{fsm} {enc}/{ff}: synthesised structural Verilog equals the state table")
                if behav == want and struct == want:
                    print(f"iverilog: {fsm} {enc}/{ff} matches the state table on 64 random inputs")
else:
    print("SKIP: iverilog not found, FSM co-simulation not run")

# 3. independent equivalence check of the technology mapper by Yosys ------------------------------------------------
if shutil.which("yosys") is not None:
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        designs = {"c17": EX / "c17.blif"}
        src = ROOT / "src" / "hdl"
        r = subprocess.run(["yosys", "-q", "-p", f"read_verilog {src / 'control' / 'nexus_control_unit.v'}; "
                            f"synth -flatten -top nexus_control_unit; abc -lut 4; opt_clean; write_blif {t / 'ctl.blif'}"],
                           text=True, capture_output=True, check=False)
        if r.returncode == 0:
            designs["control"] = t / "ctl.blif"
        for name, blif in designs.items():
            mapped = t / f"{name}_mapped.v"
            run("synth", str(blif), "--buffer", "4", "--verilog", str(mapped), "--module", "gate")
            model = next(line.split()[1] for line in blif.read_text().splitlines() if line.startswith(".model"))
            script = (f"read_blif {blif}; rename {model} gold; read_verilog {mapped}; "
                      f"miter -equiv -flatten -make_assert gold gate miter; hierarchy -top miter; "
                      f"sat -verify -prove-asserts miter")
            eq = subprocess.run(["yosys", "-q", "-p", script], text=True, capture_output=True, check=False)
            expect(eq.returncode == 0, f"Yosys proves the nexus-eda mapping of {name} equivalent\n{eq.stdout[-2000:]}{eq.stderr[-2000:]}")
            if eq.returncode == 0:
                print(f"Yosys SAT: nexus-eda mapping of {name} proved equivalent to the source")
else:
    print("SKIP: yosys not found, independent equivalence check not run")

if failures:
    print(f"{failures} failure(s)")
    sys.exit(1)
print("eda CLI integration test passed")
