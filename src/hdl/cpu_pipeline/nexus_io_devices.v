// I/O devices on the memory-mapped bus of the pipelined core (EY321 (g): I/O units and their communication with
// the processor through a bus, polling and interrupts).  The memory map matches the C++ simulators (src/sim/io),
// so the same program runs on mips-sim and on this RTL:
//   0x10000  console data (write: send the low byte)       0x10004  console status (bit 0: ready for a byte)
//   0x10100  timer compare (cycles until the interrupt)    0x10104  timer control (bit 0: enable)
//   0x10108  timer status (bit 0: interrupt pending; write clears)
// The console is busy for BUSY_CYCLES cycles after every byte, so a program must poll the status register
// (programmed I/O); the timer raises `irq` when its count reaches zero (interrupt-driven I/O).

module nexus_console #(parameter BUSY_CYCLES = 4) (
    input  wire       clk,
    input  wire       reset,
    input  wire       start,
    input  wire [7:0] data,
    output wire       ready,
    output reg        sent,       // one-cycle pulse when a byte is accepted
    output reg  [7:0] sent_byte);
  reg [7:0] busy;
  assign ready = busy == 8'd0;
  always @(posedge clk) begin
    sent <= 1'b0;
    if (reset) begin
      busy <= 8'd0;
    end else if (start && ready) begin
      busy <= BUSY_CYCLES[7:0];
      sent <= 1'b1;
      sent_byte <= data;
    end else if (busy != 8'd0) begin
      busy <= busy - 8'd1;
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

// The core, the two devices and the address decoder of the I/O bus.
module nexus_io_system #(
    parameter IMEM_WORDS = 4096,
    parameter DMEM_WORDS = 16384,
    parameter BUSY_CYCLES = 4
) (
    input  wire        clk,
    input  wire        reset,
    input  wire [31:0] entry_pc,
    input  wire [31:0] irq_handler,
    output wire        halted,
    output wire [31:0] exit_code,
    output wire [31:0] retired,
    output wire [31:0] cycles,
    output wire [31:0] interrupts,
    output wire        console_sent,
    output wire [7:0]  console_byte,
    output reg  [31:0] status_polls);
  wire [31:0] io_addr, io_wdata;
  wire io_we, io_re, irq_ack;
  wire [31:0] stalls, flushes;
  wire console_ready;
  wire [31:0] timer_compare;
  wire timer_enabled, timer_pending;
  reg  [31:0] io_rdata;

  nexus_mips_pipeline #(.IMEM_WORDS(IMEM_WORDS), .DMEM_WORDS(DMEM_WORDS), .ENABLE_IO(1)) cpu (
      .clk(clk), .reset(reset), .entry_pc(entry_pc),
      .imem_we(1'b0), .imem_waddr(32'd0), .imem_wdata(32'd0),
      .halted(halted), .exit_code(exit_code), .retired(retired), .cycles(cycles), .stalls(stalls),
      .flushes(flushes), .io_addr(io_addr), .io_wdata(io_wdata), .io_we(io_we), .io_re(io_re),
      .io_rdata(io_rdata), .irq(timer_pending), .irq_handler(irq_handler), .irq_ack(irq_ack),
      .interrupts(interrupts));

  nexus_console #(.BUSY_CYCLES(BUSY_CYCLES)) console (
      .clk(clk), .reset(reset), .start(io_we && io_addr == 32'h00010000), .data(io_wdata[7:0]),
      .ready(console_ready), .sent(console_sent), .sent_byte(console_byte));

  nexus_timer timer (
      .clk(clk), .reset(reset), .we(io_we), .addr(io_addr), .wdata(io_wdata), .ack(irq_ack),
      .compare(timer_compare), .enabled(timer_enabled), .pending(timer_pending));

  always @(posedge clk) begin
    if (reset) status_polls <= 32'd0;
    else if (io_re && io_addr == 32'h00010004) status_polls <= status_polls + 32'd1;
  end

  // read multiplexer of the I/O bus (address decoder)
  always @(*) begin
    case (io_addr)
      32'h00010004: io_rdata = {31'd0, console_ready};
      32'h00010100: io_rdata = timer_compare;
      32'h00010104: io_rdata = {31'd0, timer_enabled};
      32'h00010108: io_rdata = {31'd0, timer_pending};
      default:      io_rdata = 32'd0;
    endcase
  end
endmodule
