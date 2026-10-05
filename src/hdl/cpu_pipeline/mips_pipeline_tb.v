// Runs a MIPS program image (one 32-bit hex word per line, from `mips-sim encode`) on the
// pipelined core and prints the architectural outcome for co-simulation:
//   iverilog -g2012 -o tb nexus_mips_pipeline.v mips_pipeline_tb.v
//   vvp tb +program=prog.hex
`timescale 1ns / 1ps
module mips_pipeline_tb;
  reg clk = 1'b0;
  reg reset = 1'b1;
  wire halted;
  wire [31:0] exit_code;
  wire [31:0] retired;
  wire [31:0] cycles;
  wire [31:0] stalls;
  wire [31:0] flushes;
  reg [31:0] entry = 32'd0;

  nexus_mips_pipeline dut(
      .clk(clk),
      .reset(reset),
      .entry_pc(entry),
      .imem_we(1'b0),
      .imem_waddr(32'd0),
      .imem_wdata(32'd0),
      .halted(halted),
      .exit_code(exit_code),
      .retired(retired),
      .cycles(cycles),
      .stalls(stalls),
      .flushes(flushes));

  always #5 clk = ~clk;

  reg [1023:0] program_path;
  integer index;
  integer limit;
  initial begin
    for (index = 0; index < 4096; index = index + 1) begin
      dut.imem[index] = 32'd0;  // sll $zero, $zero, 0 (nop)
    end
    for (index = 0; index < 16384; index = index + 1) begin
      dut.dmem[index] = 32'd0;
    end
    if (!$value$plusargs("program=%s", program_path)) begin
      $display("FAIL: missing +program=<file.hex>");
      $finish;
    end
    $readmemh(program_path, dut.imem);
    if (!$value$plusargs("entry=%d", entry)) begin
      entry = 32'd0;
    end
    if (!$value$plusargs("limit=%d", limit)) begin
      limit = 5000000;
    end
    repeat (2) @(posedge clk);
    reset = 1'b0;
    while (!halted && cycles < limit) begin
      @(posedge clk);
    end
    #1;
    if (!halted) begin
      $display("FAIL: cycle limit reached (pc=%0d)", dut.pc);
    end else begin
      $display("EXIT %0d INSTRET %0d CYCLES %0d STALLS %0d FLUSHES %0d", $signed(exit_code), retired, cycles, stalls,
               flushes);
      $display("PASS: mips_pipeline_tb");
    end
    $finish;
  end
endmodule
