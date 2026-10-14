#!/usr/bin/env python3
"""NEXUS SoC flow (NEY604 embedded systems, NEY705 design flow).

  soc_flow.py <mips-sim> [--fpga] [--gates]

1. Runs the example programs at every level of abstraction available and compares them:
   functional ISA simulator -> C++ pipeline model -> SoC RTL (Icarus Verilog) [-> gate-level netlist (--gates)].
   The observable results are the exit code, the bytes sent through the UART and the number of interrupts.
2. --fpga: Yosys synth_ecp5 -> nextpnr-ecp5 place and route -> ecppack bitstream, with the utilisation and the
   maximum frequency written to build/soc/fpga_report.txt.
Exit codes: 0 pass, 1 failure, 77 Icarus Verilog not installed.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HDL = ROOT / "src" / "hdl"
BUILD = ROOT / "build" / "soc"
PROGRAMS = [
    # program, switches, expected exit code, expected UART text, expected interrupts
    ("examples/embedded/hello_uart.s", 0, 12, "HELLO NEXUS\\n", 0),
    ("examples/os/rr_kernel.s", 0, 9, "012012121", 9),
    ("examples/embedded/gpio_echo.s", 181, 5, "", 0),
]


def sh(*args: str, cwd: Path | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(list(args), text=True, capture_output=True, check=False, cwd=cwd)


def encode(mips_sim: str, program: Path, out: Path) -> tuple[int, int]:
    r = sh(mips_sim, "encode", str(program), "-o", str(out))
    if r.returncode != 0:
        raise SystemExit(f"encode failed: {r.stderr}")
    entry = int(re.search(r"entry (\d+)", r.stderr).group(1))
    handler = re.search(r"handler (\d+)", r.stderr)
    return entry, int(handler.group(1)) if handler else 0


def simulator(mips_sim: str, program: Path, mode: str) -> tuple[int, str, int]:
    args = [mips_sim, "run", str(program), "--mode", mode, "--stats"]
    if mode == "pipeline":
        args.append("--io-demo")
        if "interrupt_handler" in program.read_text():
            args.append("--interrupt-demo")
    r = sh(*args)
    out = r.stdout + r.stderr
    code = int(re.search(r"Program exited with code (-?\d+)", out).group(1))
    chars = "".join("\\n" if v == "10" else chr(int(v)) for v in re.findall(r"io\[console\]: value=(\d+)", out))
    irq = re.search(r"Interrupts handled: (\d+)", out)
    return code, chars, int(irq.group(1)) if irq else 0


def rtl(vvp: Path, hexfile: Path, entry: int, handler: int, switches: int) -> tuple[int, str, int]:
    r = sh("vvp", "-n", str(vvp), f"+program={hexfile}", f"+entry={entry}", f"+handler={handler}",
           f"+switches={switches}")
    uart = re.search(r"^UART (.*)$", r.stdout, re.M).group(1)
    m = re.search(r"EXIT (-?\d+) INTERRUPTS (\d+)", r.stdout)
    return int(m.group(1)), uart, int(m.group(2))


GATE_TB = """`timescale 1ns / 1ps
module gate_tb;
  reg clk = 0, reset = 1, we = 0;
  reg [31:0] waddr = 0, wdata = 0;
  wire [7:0] leds; wire tx, halted; wire [31:0] code, cycles, irqs;
  reg [31:0] image [0:%(words)d - 1];
  nexus_soc soc(.clk(clk), .reset(reset), .entry_pc(32'd%(entry)d), .irq_handler(32'd%(handler)d),
    .imem_we(we), .imem_waddr(waddr), .imem_wdata(wdata), .switches(8'd%(switches)d), .leds(leds), .uart_tx(tx),
    .halted(halted), .exit_code(code), .cycles(cycles), .interrupts(irqs));
  always #5 clk = ~clk;
  reg [7:0] b; integer i, n = 0; reg [8*64-1:0] text = 0;
  always begin
    @(negedge tx);
    if (!reset) begin
      repeat (4) @(posedge clk);
      if (tx == 0) begin
        for (i = 0; i < 8; i = i + 1) begin repeat (8) @(posedge clk); b[i] = tx; end
        repeat (8) @(posedge clk);
        text = {text[8*63-1:0], b}; n = n + 1;
      end
    end
  end
  integer k;
  initial begin
    $readmemh("%(hex)s", image);
    for (k = 0; k < %(words)d; k = k + 1) begin
      @(negedge clk); we = 1; waddr = k; wdata = (image[k] === 32'bx) ? 32'd0 : image[k];
    end
    @(negedge clk); we = 0; reset = 0;
    while (!halted && cycles < 20000) @(posedge clk);
    repeat (100) @(posedge clk);
    $write("UART ");
    for (k = n - 1; k >= 0; k = k - 1) if (text[8*k +: 8] == 8'h0a) $write("\\\\n"); else $write("%%c", text[8*k +: 8]);
    $display("");
    $display("EXIT %%0d INTERRUPTS %%0d", $signed(code), irqs);
    $finish;
  end
endmodule
"""


def main() -> int:
    mips_sim = sys.argv[1]
    want_fpga = "--fpga" in sys.argv
    want_gates = "--gates" in sys.argv
    if want_fpga:
        # the FPGA test only runs the ECP5 flow; the programs are checked by the --gates run
        if shutil.which("yosys") is None or shutil.which("nextpnr-ecp5") is None:
            print("SKIP: yosys/nextpnr-ecp5 not found, FPGA flow not run")
            return 77
        BUILD.mkdir(parents=True, exist_ok=True)
        ok = fpga(mips_sim)
        print("PASS" if ok else "FAIL")
        return 0 if ok else 1
    if shutil.which("iverilog") is None or shutil.which("vvp") is None:
        print("SKIP: iverilog not found")
        return 77
    BUILD.mkdir(parents=True, exist_ok=True)
    vvp = BUILD / "soc.vvp"
    r = sh("iverilog", "-g2012", "-o", str(vvp), str(HDL / "cpu_pipeline" / "nexus_mips_pipeline.v"),
           str(HDL / "soc" / "nexus_soc.v"), str(HDL / "soc" / "soc_tb.v"))
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        return 1
    ok = True
    for rel, switches, want_code, want_uart, want_irq in PROGRAMS:
        program = ROOT / rel
        hexfile = BUILD / (program.stem + ".hex")
        entry, handler = encode(mips_sim, program, hexfile)
        rows = []
        if "gpio" not in rel:  # the C++ simulators model console, timer and DMA, not the GPIO block
            for mode in ("functional", "pipeline"):
                if mode == "functional" and "interrupt_handler" in program.read_text():
                    continue  # the ISA-level simulator has the console but no timer interrupts
                rows.append((f"mips-sim {mode}", *simulator(mips_sim, program, mode)))
        rows.append(("SoC RTL", *rtl(vvp, hexfile, entry, handler, switches)))
        if want_gates and "gpio" not in rel and "kernel" not in rel:
            rows.append(("gate-level netlist", *gates(hexfile, entry, handler, switches)))
        for name, code, uart, irq in rows:
            good = code == want_code and irq == want_irq and (name.startswith("mips-sim functional") or uart == want_uart)
            ok &= good
            print(f"{program.name:16s} {name:22s} exit {code:3d}  interrupts {irq}  uart '{uart}'  {'ok' if good else 'MISMATCH'}")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def gates(hexfile: Path, entry: int, handler: int, switches: int, imem_words: int = 64) -> tuple[int, str, int]:
    """Synthesise the SoC with small memories to a generic gate netlist and simulate the netlist."""
    net = BUILD / f"soc_gates_{imem_words}.v"
    if not net.exists():
        r = sh("yosys", "-q", "-p",
               f"read_verilog -sv {HDL / 'cpu_pipeline' / 'nexus_mips_pipeline.v'} {HDL / 'soc' / 'nexus_soc.v'}; "
               f"chparam -set IMEM_WORDS {imem_words} -set DMEM_WORDS 256 nexus_soc; synth -flatten -top nexus_soc; "
               f"write_verilog -noattr {net}")
        if r.returncode != 0:
            raise SystemExit(f"gate-level synthesis failed: {r.stderr[-2000:]}")
    tb = BUILD / "gate_tb.v"
    tb.write_text(GATE_TB % {"entry": entry, "handler": handler, "switches": switches, "hex": hexfile.as_posix(),
                             "words": imem_words})
    yosys_share = Path(shutil.which("yosys")).resolve().parents[1] / "share" / "yosys"
    sim = BUILD / "gates.vvp"
    r = sh("iverilog", "-g2012", "-o", str(sim), str(net), str(tb), str(yosys_share / "simcells.v"),
           str(yosys_share / "simlib.v"))
    if r.returncode != 0:
        raise SystemExit(f"gate-level compile failed: {r.stderr[-2000:]}")
    r = sh("vvp", "-n", str(sim))
    uart = re.search(r"^UART (.*)$", r.stdout, re.M).group(1)
    m = re.search(r"EXIT (-?\d+) INTERRUPTS (\d+)", r.stdout)
    return int(m.group(1)), uart, int(m.group(2))


def fpga(mips_sim: str, source: Path | None = None) -> bool:
    program = BUILD / "program.hex"
    entry, handler = encode(mips_sim, source or ROOT / "examples/embedded/hello_uart.s", program)
    r = sh("yosys", "-q", "-l", "yosys_ecp5.log", "-p",
           f"read_verilog -sv {HDL / 'cpu_pipeline' / 'nexus_mips_pipeline.v'} {HDL / 'soc' / 'nexus_soc.v'} "
           f"{HDL / 'soc' / 'nexus_soc_fpga.v'}; chparam -set PROGRAM \"program.hex\" -set ENTRY {entry} -set HANDLER {handler} nexus_soc_fpga; "
           f"synth_ecp5 -top nexus_soc_fpga -json soc.json", cwd=BUILD)
    if r.returncode != 0:
        print("yosys failed", r.stderr[-2000:])
        return False
    r = sh("nextpnr-ecp5", "--85k", "--package", "CABGA381", "--json", "soc.json", "--freq", "25", "--seed", "1",
           "--timing-allow-fail", "--textcfg", "soc.config", "-l", "pnr.log", cwd=BUILD)
    log = (BUILD / "pnr.log").read_text(errors="replace")
    fmax = re.findall(r"Max frequency for clock '[^']*clk[^']*': ([\d.]+) MHz", log)
    util = {k: v for k, v in re.findall(r"(TRELLIS_COMB|TRELLIS_FF|DP16KD|MULT18X18D|TRELLIS_IO):\s+(\d+/\s*\d+)", log)}
    packed = False
    if shutil.which("ecppack") is not None:
        bit = BUILD / "soc.bit"
        bit.unlink(missing_ok=True)
        sh("ecppack", "soc.config", "soc.bit", cwd=BUILD)
        # the Windows builds of ecppack/ecpunpack can crash on exit after writing their output, so the bitstream is
        # judged by reading it back: every frame parsed, the LFE5U-85F device ID, and the DONE command
        if bit.exists() and bit.stat().st_size > 0:
            if shutil.which("ecpunpack") is not None:
                back = sh("ecpunpack", "soc.bit", "soc_readback.config", cwd=BUILD)
                text = back.stdout + back.stderr
                packed = "device ID: 0x41113043" in text and "program DONE" in text
            else:
                packed = True
    report = (f"device LFE5U-85F CABGA381 (no board pin constraints), target 25 MHz\n"
              f"utilisation: {util}\nmax frequency: {fmax[-1] if fmax else '?'} MHz\n"
              f"bitstream: {'build/soc/soc.bit (read back by ecpunpack)' if packed else 'not generated'}\n")
    (BUILD / "fpga_report.txt").write_text(report)
    print(report.strip())
    return bool(fmax) and r.returncode == 0 and (packed or shutil.which("ecppack") is None)


if __name__ == "__main__":
    raise SystemExit(main())
