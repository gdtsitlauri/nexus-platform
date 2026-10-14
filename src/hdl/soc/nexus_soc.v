// NEXUS system-on-chip (NEY604): the pipelined MIPS core with a memory-mapped I/O bus and three peripherals.
// The memory map matches the C++ simulators (src/sim/io), so the same program runs on mips-sim and on this RTL:
//   0x10000  UART data (write: transmit the low byte)      0x10004  UART status (bit 0: ready to accept a byte)
//   0x10100  timer compare (cycles until the interrupt)    0x10104  timer control (bit 0: enable)
//   0x10108  timer status (bit 0: interrupt pending; write clears)
//   0x10300  GPIO output register (LEDs)                   0x10304  GPIO input (switches)
// The UART sends 8N1 frames (start bit, 8 data bits LSB first, stop bit) with CLKS_PER_BIT clocks per bit.

module nexus_uart_tx #(parameter CLKS_PER_BIT = 8) (
    input  wire       clk,
    input  wire       reset,
    input  wire       start,
    input  wire [7:0] data,
    output reg        tx,
    output wire       ready);
  reg [9:0]  shift;
  reg [3:0]  bits_left;
  reg [15:0] count;
  assign ready = bits_left == 4'd0;
  always @(posedge clk) begin
    if (reset) begin
      tx <= 1'b1;
      bits_left <= 4'd0;
      count <= 16'd0;
      shift <= 10'h3ff;
    end else if (bits_left == 4'd0) begin
      tx <= 1'b1;
      if (start) begin
        shift <= {1'b1, data, 1'b0};  // stop, data, start (sent LSB first)
        bits_left <= 4'd10;
        count <= 16'd0;
      end
    end else begin
      tx <= shift[0];
      if (count == CLKS_PER_BIT - 1) begin
        count <= 16'd0;
        shift <= {1'b1, shift[9:1]};
        bits_left <= bits_left - 4'd1;
      end else begin
        count <= count + 16'd1;
      end
    end
  end
endmodule

// countdown timer with the semantics of the simulator's timer device
module nexus_timer (
    input  wire        clk,
    input  wire        reset,
    input  wire        we,
    input  wire [31:0] addr,
    input  wire [31:0] wdata,
    input  wire        ack,
    output reg  [31:0] compare,
    output reg         enabled,
    output reg         pending);
  always @(posedge clk) begin
    if (reset) begin
      compare <= 32'd0;
      enabled <= 1'b0;
      pending <= 1'b0;
    end else begin
      if (enabled && compare != 32'd0) begin
        compare <= compare - 32'd1;
        if (compare == 32'd1) begin
          enabled <= 1'b0;
          pending <= 1'b1;
        end
      end
      if (ack) begin
        pending <= 1'b0;  // acknowledged when the core dispatches the interrupt
      end
      if (we) begin
        case (addr)
          32'h00010100: compare <= wdata;
          32'h00010104: begin enabled <= wdata[0]; if (wdata[0]) pending <= 1'b0; end
          32'h00010108: pending <= 1'b0;
          default: ;
        endcase
      end
    end
  end
endmodule

module nexus_soc #(
    parameter IMEM_WORDS = 4096,
    parameter DMEM_WORDS = 16384,
    parameter CLKS_PER_BIT = 8
) (
    input  wire        clk,
    input  wire        reset,
    input  wire [31:0] entry_pc,
    input  wire [31:0] irq_handler,
    input  wire        imem_we,
    input  wire [31:0] imem_waddr,
    input  wire [31:0] imem_wdata,
    input  wire [7:0]  switches,
    output reg  [7:0]  leds,
    output wire        uart_tx,
    output wire        halted,
    output wire [31:0] exit_code,
    output wire [31:0] cycles,
    output wire [31:0] interrupts);
  wire [31:0] io_addr, io_wdata;
  wire io_we, io_re, irq_ack;
  wire [31:0] retired, stalls, flushes;
  wire uart_ready;
  wire [31:0] timer_compare;
  wire timer_enabled, timer_pending;
  reg  [31:0] io_rdata;

  nexus_mips_pipeline #(.IMEM_WORDS(IMEM_WORDS), .DMEM_WORDS(DMEM_WORDS), .ENABLE_IO(1)) cpu (
      .clk(clk), .reset(reset), .entry_pc(entry_pc),
      .imem_we(imem_we), .imem_waddr(imem_waddr), .imem_wdata(imem_wdata),
      .halted(halted), .exit_code(exit_code), .retired(retired), .cycles(cycles), .stalls(stalls),
      .flushes(flushes), .io_addr(io_addr), .io_wdata(io_wdata), .io_we(io_we), .io_re(io_re),
      .io_rdata(io_rdata), .irq(timer_pending), .irq_handler(irq_handler), .irq_ack(irq_ack),
      .interrupts(interrupts));

  nexus_uart_tx #(.CLKS_PER_BIT(CLKS_PER_BIT)) uart (
      .clk(clk), .reset(reset), .start(io_we && io_addr == 32'h00010000), .data(io_wdata[7:0]),
      .tx(uart_tx), .ready(uart_ready));

  nexus_timer timer (
      .clk(clk), .reset(reset), .we(io_we), .addr(io_addr), .wdata(io_wdata), .ack(irq_ack),
      .compare(timer_compare), .enabled(timer_enabled), .pending(timer_pending));

  // GPIO output register
  always @(posedge clk) begin
    if (reset) leds <= 8'd0;
    else if (io_we && io_addr == 32'h00010300) leds <= io_wdata[7:0];
  end

  // read multiplexer of the I/O bus (address decoder)
  always @(*) begin
    case (io_addr)
      32'h00010004: io_rdata = {31'd0, uart_ready};
      32'h00010100: io_rdata = timer_compare;
      32'h00010104: io_rdata = {31'd0, timer_enabled};
      32'h00010108: io_rdata = {31'd0, timer_pending};
      32'h00010300: io_rdata = {24'd0, leds};
      32'h00010304: io_rdata = {24'd0, switches};
      default:      io_rdata = 32'd0;
    endcase
  end
endmodule
