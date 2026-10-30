// Runs a MIPS program image on the multi-cycle core:
//   iverilog -g2012 -P mips_multicycle_tb.MICROCODE=1 -o tb nexus_mips_multicycle.v mips_multicycle_tb.v
//   vvp tb +program=prog.hex +entry=N
// Prints "EXIT <code> INSTRET <n> CYCLES <c>".
`timescale 1ns / 1ps
module mips_multicycle_tb;
  parameter MICROCODE = 0;
  reg clk = 1'b0;
  reg reset = 1'b1;
  reg [31:0] entry = 32'd0;
  wire halted;
  wire [31:0] exit_code, retired, cycles;

  nexus_mips_multicycle #(.MICROCODE(MICROCODE)) dut (
      .clk(clk), .reset(reset), .entry_pc(entry), .halted(halted), .exit_code(exit_code), .retired(retired),
      .cycles(cycles));

  always #5 clk = ~clk;

  reg [1023:0] program_path;
  integer index, limit;
  initial begin
    for (index = 0; index < 4096; index = index + 1) dut.imem[index] = 32'd0;
    for (index = 0; index < 16384; index = index + 1) dut.dmem[index] = 32'd0;
    if (!$value$plusargs("program=%s", program_path)) begin
      $display("FAIL: missing +program=<file.hex>");
      $finish;
    end
    $readmemh(program_path, dut.imem);
    if (!$value$plusargs("entry=%d", entry)) entry = 32'd0;
    if (!$value$plusargs("limit=%d", limit)) limit = 20000000;
    repeat (2) @(posedge clk);
    reset = 1'b0;
    while (!halted && cycles < limit) @(posedge clk);
    #1;
    if (!halted) $display("FAIL: cycle limit reached (pc=%0d)", dut.pc);
    else $display("EXIT %0d INSTRET %0d CYCLES %0d", $signed(exit_code), retired, cycles);
    $finish;
  end
endmodule
