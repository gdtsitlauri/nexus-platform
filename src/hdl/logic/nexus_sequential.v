// EY121 sequential building blocks.

// D flip-flop with asynchronous active-high reset
module nexus_dff (input wire clk, input wire rst, input wire d, output reg q);
  always @(posedge clk or posedge rst)
    if (rst) q <= 1'b0; else q <= d;
endmodule

// JK flip-flop: 00 hold, 01 reset, 10 set, 11 toggle
module nexus_jkff (input wire clk, input wire rst, input wire j, input wire k, output reg q);
  always @(posedge clk or posedge rst)
    if (rst) q <= 1'b0;
    else case ({j, k})
      2'b00: q <= q;
      2'b01: q <= 1'b0;
      2'b10: q <= 1'b1;
      2'b11: q <= ~q;
    endcase
endmodule

// T flip-flop
module nexus_tff (input wire clk, input wire rst, input wire t, output reg q);
  always @(posedge clk or posedge rst)
    if (rst) q <= 1'b0; else if (t) q <= ~q;
endmodule

// 4-bit universal shift register: mode 00 hold, 01 shift right (serial in from sr_in), 10 shift left, 11 load
module nexus_shift_register4 (
    input  wire       clk, rst,
    input  wire [1:0] mode,
    input  wire       sr_in, sl_in,
    input  wire [3:0] d,
    output reg  [3:0] q);
  always @(posedge clk or posedge rst)
    if (rst) q <= 4'b0;
    else case (mode)
      2'b00: q <= q;
      2'b01: q <= {sr_in, q[3:1]};
      2'b10: q <= {q[2:0], sl_in};
      2'b11: q <= d;
    endcase
endmodule

// modulo-N up/down counter with synchronous clear and terminal count
module nexus_counter #(parameter N = 10, parameter W = 4) (
    input  wire         clk, rst, en, up,
    output reg  [W-1:0] count,
    output wire         tc);
  assign tc = en & (up ? (count == N - 1) : (count == 0));
  always @(posedge clk or posedge rst)
    if (rst) count <= 0;
    else if (en) begin
      if (up) count <= (count == N - 1) ? 0 : count + 1'b1;
      else    count <= (count == 0) ? N - 1 : count - 1'b1;
    end
endmodule

// ripple counter from T flip-flops (asynchronous: each stage clocked by the previous output)
module nexus_ripple_counter4 (input wire clk, input wire rst, output wire [3:0] q);
  wire [3:0] qn = ~q;
  nexus_tff t0(.clk(clk),    .rst(rst), .t(1'b1), .q(q[0]));
  nexus_tff t1(.clk(qn[0]),  .rst(rst), .t(1'b1), .q(q[1]));
  nexus_tff t2(.clk(qn[1]),  .rst(rst), .t(1'b1), .q(q[2]));
  nexus_tff t3(.clk(qn[2]),  .rst(rst), .t(1'b1), .q(q[3]));
endmodule
