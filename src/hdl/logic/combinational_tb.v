`timescale 1ns/1ps
// Exhaustive self-checking testbench for the EY121 combinational blocks.
module combinational_tb;
  reg  [3:0] d0, d1, d2, d3;
  reg  [1:0] sel;
  wire [3:0] mux_y;
  reg  [2:0] a3;
  reg        en;
  wire [7:0] dec_y;
  reg  [7:0] pd;
  wire [2:0] pe_y;
  wire       pe_valid;
  reg  [3:0] ca, cb;
  wire       gt, eq, lt;
  reg  [3:0] bcd;
  wire [6:0] seg;
  wire [3:0] gray, back;
  integer i, j, k, errors;
  reg [2:0] want;
  reg [6:0] ref_seg [0:15];

  nexus_mux4 #(.W(4)) mux(.d0(d0), .d1(d1), .d2(d2), .d3(d3), .sel(sel), .y(mux_y));
  nexus_decoder3to8 dec(.a(a3), .en(en), .y(dec_y));
  nexus_priority_encoder8 pe(.d(pd), .y(pe_y), .valid(pe_valid));
  nexus_comparator4 cmp(.a(ca), .b(cb), .gt(gt), .eq(eq), .lt(lt));
  nexus_bcd7seg sevseg(.bcd(bcd), .seg(seg));
  nexus_gray4 g4(.bin(ca), .gray(gray), .back(back));

  initial begin
    errors = 0;
    ref_seg[0] = 7'h3f; ref_seg[1] = 7'h06; ref_seg[2] = 7'h5b; ref_seg[3] = 7'h4f; ref_seg[4] = 7'h66;
    ref_seg[5] = 7'h6d; ref_seg[6] = 7'h7d; ref_seg[7] = 7'h07; ref_seg[8] = 7'h7f; ref_seg[9] = 7'h6f;
    for (i = 10; i < 16; i = i + 1) ref_seg[i] = 7'h00;
    d0 = 4'h3; d1 = 4'h9; d2 = 4'hc; d3 = 4'h5;
    for (i = 0; i < 4; i = i + 1) begin
      sel = i[1:0]; #1;
      if (mux_y !== (i == 0 ? d0 : i == 1 ? d1 : i == 2 ? d2 : d3)) begin errors = errors + 1; $display("FAIL mux %0d", i); end
    end
    for (i = 0; i < 16; i = i + 1) begin
      a3 = i[2:0]; en = i[3]; #1;
      if (dec_y !== (en ? (8'b1 << a3) : 8'b0)) begin errors = errors + 1; $display("FAIL decoder %0d", i); end
    end
    for (i = 0; i < 256; i = i + 1) begin
      pd = i[7:0]; #1;
      want = 0;
      for (k = 0; k < 8; k = k + 1) if (pd[k]) want = k[2:0];
      if (pe_valid !== (i != 0) || (i != 0 && pe_y !== want)) begin errors = errors + 1; $display("FAIL encoder %0d", i); end
    end
    for (i = 0; i < 16; i = i + 1)
      for (j = 0; j < 16; j = j + 1) begin
        ca = i[3:0]; cb = j[3:0]; #1;
        if (gt !== (i > j) || eq !== (i == j) || lt !== (i < j)) begin errors = errors + 1; $display("FAIL cmp %0d %0d", i, j); end
      end
    for (i = 0; i < 16; i = i + 1) begin
      ca = i[3:0]; #1;
      if (back !== ca || gray !== (ca ^ (ca >> 1))) begin errors = errors + 1; $display("FAIL gray %0d", i); end
    end
    for (i = 0; i < 16; i = i + 1) begin
      bcd = i[3:0]; #1;
      if (seg !== ref_seg[i]) begin errors = errors + 1; $display("FAIL 7seg %0d", i); end
    end
    if (errors == 0) $display("PASS combinational blocks");
    else $display("FAIL %0d errors", errors);
    $finish;
  end
endmodule
