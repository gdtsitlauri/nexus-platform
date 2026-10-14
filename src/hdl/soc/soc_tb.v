// Runs a program on the NEXUS SoC and decodes the UART serial line like a terminal would.
//   vvp soc.vvp +program=prog.hex +entry=N +handler=H [+switches=S] [+limit=C]
// Prints: "UART <text>" (every received byte, escaped), "LEDS <hex>", "EXIT <code> INTERRUPTS <n> CYCLES <c>".
`timescale 1ns / 1ps
module soc_tb;
  localparam CLKS_PER_BIT = 8;
  reg clk = 1'b0;
  reg reset = 1'b1;
  reg [31:0] entry = 32'd0;
  reg [31:0] handler = 32'd0;
  reg [7:0] switches = 8'd0;
  wire [7:0] leds;
  wire uart_tx, halted;
  wire [31:0] exit_code, cycles, interrupts;

  nexus_soc #(.CLKS_PER_BIT(CLKS_PER_BIT)) soc (
      .clk(clk), .reset(reset), .entry_pc(entry), .irq_handler(handler),
      .imem_we(1'b0), .imem_waddr(32'd0), .imem_wdata(32'd0), .switches(switches),
      .leds(leds), .uart_tx(uart_tx), .halted(halted), .exit_code(exit_code), .cycles(cycles),
      .interrupts(interrupts));

  always #5 clk = ~clk;

  // UART receiver model: sample in the middle of every bit
  reg [7:0] rx_byte;
  integer bit_index;
  reg [8*256-1:0] text;
  integer received = 0;
  always begin
    @(negedge uart_tx);
    if (!reset) begin
      repeat (CLKS_PER_BIT / 2) @(posedge clk);
      if (uart_tx == 1'b0) begin  // a real start bit
        for (bit_index = 0; bit_index < 8; bit_index = bit_index + 1) begin
          repeat (CLKS_PER_BIT) @(posedge clk);
          rx_byte[bit_index] = uart_tx;
        end
        repeat (CLKS_PER_BIT) @(posedge clk);  // stop bit
        if (uart_tx !== 1'b1) $display("FAIL: framing error");
        text = {text[8*255-1:0], rx_byte};
        received = received + 1;
      end
    end
  end

  reg [1023:0] program_path;
  integer index, limit, k;
  initial begin
    text = 0;
    for (index = 0; index < 4096; index = index + 1) soc.cpu.imem[index] = 32'd0;
    for (index = 0; index < 16384; index = index + 1) soc.cpu.dmem[index] = 32'd0;
    if (!$value$plusargs("program=%s", program_path)) begin
      $display("FAIL: missing +program=<file.hex>");
      $finish;
    end
    $readmemh(program_path, soc.cpu.imem);
    if (!$value$plusargs("entry=%d", entry)) entry = 32'd0;
    if (!$value$plusargs("handler=%d", handler)) handler = 32'd0;
    if (!$value$plusargs("switches=%d", switches)) switches = 8'd0;
    if (!$value$plusargs("limit=%d", limit)) limit = 2000000;
    repeat (2) @(posedge clk);
    reset = 1'b0;
    while (!halted && cycles < limit) @(posedge clk);
    repeat (12 * CLKS_PER_BIT) @(posedge clk);  // let the last UART frame finish
    if (!halted) $display("FAIL: cycle limit reached (pc=%0d)", soc.cpu.pc);
    $write("UART ");
    for (k = received - 1; k >= 0; k = k - 1) begin
      if (text[8*k +: 8] == 8'h0a) $write("\\n");
      else $write("%c", text[8*k +: 8]);
    end
    $display("");
    $display("LEDS %02h", leds);
    $display("EXIT %0d INTERRUPTS %0d CYCLES %0d", $signed(exit_code), interrupts, cycles);
    $finish;
  end
endmodule
