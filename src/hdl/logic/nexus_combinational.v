// EY121 combinational building blocks.

// 4-to-1 multiplexer
module nexus_mux4 #(parameter W = 1) (
    input  wire [W-1:0] d0, d1, d2, d3,
    input  wire [1:0]   sel,
    output wire [W-1:0] y);
  assign y = sel[1] ? (sel[0] ? d3 : d2) : (sel[0] ? d1 : d0);
endmodule

// 3-to-8 decoder with enable (one-hot output)
module nexus_decoder3to8 (
    input  wire [2:0] a,
    input  wire       en,
    output wire [7:0] y);
  assign y = en ? (8'b1 << a) : 8'b0;
endmodule

// 8-to-3 priority encoder: the highest-numbered active input wins; valid = any input active
module nexus_priority_encoder8 (
    input  wire [7:0] d,
    output reg  [2:0] y,
    output wire       valid);
  integer i;
  assign valid = |d;
  always @* begin
    y = 3'd0;
    for (i = 0; i < 8; i = i + 1)
      if (d[i]) y = i[2:0];
  end
endmodule

// 4-bit magnitude comparator built from bit-slice equations (as in the textbook derivation)
module nexus_comparator4 (
    input  wire [3:0] a, b,
    output wire       gt, eq, lt);
  wire [3:0] x = ~(a ^ b);  // x_i = 1 when a_i == b_i
  assign eq = &x;
  assign gt = (a[3] & ~b[3]) | (x[3] & a[2] & ~b[2]) | (x[3] & x[2] & a[1] & ~b[1]) |
              (x[3] & x[2] & x[1] & a[0] & ~b[0]);
  assign lt = ~(gt | eq);
endmodule

// BCD to seven-segment decoder (segments g..a, active high); non-BCD inputs blank the display
module nexus_bcd7seg (
    input  wire [3:0] bcd,
    output reg  [6:0] seg);
  always @* begin
    case (bcd)
      4'd0: seg = 7'b0111111;
      4'd1: seg = 7'b0000110;
      4'd2: seg = 7'b1011011;
      4'd3: seg = 7'b1001111;
      4'd4: seg = 7'b1100110;
      4'd5: seg = 7'b1101101;
      4'd6: seg = 7'b1111101;
      4'd7: seg = 7'b0000111;
      4'd8: seg = 7'b1111111;
      4'd9: seg = 7'b1101111;
      default: seg = 7'b0000000;
    endcase
  end
endmodule

// 4-bit binary to Gray code and back
module nexus_gray4 (
    input  wire [3:0] bin,
    output wire [3:0] gray,
    output wire [3:0] back);
  assign gray = bin ^ (bin >> 1);
  assign back[3] = gray[3];
  assign back[2] = back[3] ^ gray[2];
  assign back[1] = back[2] ^ gray[1];
  assign back[0] = back[1] ^ gray[0];
endmodule
