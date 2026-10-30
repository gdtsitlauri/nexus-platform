// Runs a program on the pipelined core with its I/O devices and prints what the console received.
//   iverilog -g2012 -o io.vvp nexus_mips_pipeline.v nexus_io_devices.v cpu_io_tb.v
//   vvp io.vvp +program=prog.hex +entry=N [+handler=H] [+limit=C]
// Prints: "CONSOLE <text>" (newlines escaped as \n) and
//         "EXIT <code> INSTRET <n> INTERRUPTS <n> POLLS <n> CYCLES <c>".
`timescale 1ns / 1ps
module cpu_io_tb;
  reg clk = 1'b0;
  reg reset = 1'b1;
  reg [31:0] entry = 32'd0;
  reg [31:0] handler = 32'd0;
  wire halted, console_sent;
  wire [7:0] console_byte;
  wire [31:0] exit_code, retired, cycles, interrupts, polls;

  nexus_io_system system (
      .clk(clk), .reset(reset), .entry_pc(entry), .irq_handler(handler), .halted(halted),
      .exit_code(exit_code), .retired(retired), .cycles(cycles), .interrupts(interrupts),
      .console_sent(console_sent), .console_byte(console_byte), .status_polls(polls));

  always #5 clk = ~clk;

  reg [8*256-1:0] text;
  integer received = 0;
  always @(posedge clk) begin
    if (console_sent) begin
      text = {text[8*255-1:0], console_byte};
      received = received + 1;
    end
  end

  reg [1023:0] program_path;
  integer index, limit, k;
  initial begin
    text = 0;
    for (index = 0; index < 4096; index = index + 1) system.cpu.imem[index] = 32'd0;
    for (index = 0; index < 16384; index = index + 1) system.cpu.dmem[index] = 32'd0;
    if (!$value$plusargs("program=%s", program_path)) begin
      $display("FAIL: missing +program=<file.hex>");
      $finish;
    end
    $readmemh(program_path, system.cpu.imem);
    if (!$value$plusargs("entry=%d", entry)) entry = 32'd0;
    if (!$value$plusargs("handler=%d", handler)) handler = 32'd0;
    if (!$value$plusargs("limit=%d", limit)) limit = 2000000;
    repeat (2) @(posedge clk);
    reset = 1'b0;
    while (!halted && cycles < limit) @(posedge clk);
    repeat (4) @(posedge clk);
    if (!halted) $display("FAIL: cycle limit reached (pc=%0d)", system.cpu.pc);
    $write("CONSOLE ");
    for (k = received - 1; k >= 0; k = k - 1) begin
      if (text[8*k +: 8] == 8'h0a) $write("\\n");
      else $write("%c", text[8*k +: 8]);
    end
    $display("");
    $display("EXIT %0d INSTRET %0d INTERRUPTS %0d POLLS %0d CYCLES %0d", $signed(exit_code), retired, interrupts,
             polls, cycles);
    $finish;
  end
endmodule
