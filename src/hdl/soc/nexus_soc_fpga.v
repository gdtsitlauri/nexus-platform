// FPGA top level of the NEXUS SoC (NEY604): a boot ROM, initialised from a hex image at synthesis time, is copied
// into the core's instruction memory through its program-load port while reset is held; then the core runs.
// Board pins (clock, LEDs, switches, UART) are bound by a constraint file; the design was placed and routed for a
// Lattice ECP5 but has not been tested on a board.
module nexus_soc_fpga #(
    parameter PROGRAM = "program.hex",
    parameter ROM_WORDS = 256,
    parameter ENTRY = 0,
    parameter HANDLER = 0,
    parameter CLKS_PER_BIT = 217  // 25 MHz / 115200 baud
) (
    input  wire       clk,
    input  wire       btn_reset,
    input  wire [7:0] sw,
    output wire [7:0] led,
    output wire       uart_txd);
  reg [31:0] rom [0:ROM_WORDS-1];
  initial $readmemh(PROGRAM, rom);

  // power-on reset and boot copy
  reg [3:0]  por = 4'd0;
  reg [31:0] copy_addr = 32'd0;
  reg        booting = 1'b1;
  wire       por_done = &por;
  always @(posedge clk) begin
    if (!por_done) por <= por + 4'd1;
    if (btn_reset || !por_done) begin
      copy_addr <= 32'd0;
      booting <= 1'b1;
    end else if (booting) begin
      copy_addr <= copy_addr + 32'd1;
      if (copy_addr == ROM_WORDS - 1) booting <= 1'b0;
    end
  end
  wire core_reset = booting || btn_reset || !por_done;

  wire halted;
  wire [31:0] exit_code, cycles, interrupts;
  wire [7:0] leds;
  nexus_soc #(.IMEM_WORDS(ROM_WORDS), .DMEM_WORDS(1024), .CLKS_PER_BIT(CLKS_PER_BIT)) soc (
      .clk(clk), .reset(core_reset), .entry_pc(ENTRY), .irq_handler(HANDLER),
      .imem_we(booting && por_done), .imem_waddr(copy_addr), .imem_wdata(rom[copy_addr[7:0]]),
      .switches(sw), .leds(leds), .uart_tx(uart_txd), .halted(halted), .exit_code(exit_code),
      .cycles(cycles), .interrupts(interrupts));
  // LEDs show the program's GPIO output; after the program halts they show the low byte of the exit code
  assign led = halted ? exit_code[7:0] : leds;
endmodule
