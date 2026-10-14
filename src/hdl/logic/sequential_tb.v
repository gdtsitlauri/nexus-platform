`timescale 1ns/1ps
// Self-checking testbench for the EY121 sequential blocks against reference models.
module sequential_tb;
  reg clk = 0, rst = 1;
  reg d, j, k, t, sr_in, sl_in, en, up;
  reg [1:0] mode;
  reg [3:0] load;
  wire qd, qjk, qt, tc;
  wire [3:0] sq, cnt, rip;
  reg ref_d, ref_jk, ref_t;
  reg [3:0] ref_s, ref_c;
  reg [3:0] ref_rip;
  integer cycle, errors;

  nexus_dff dff(.clk(clk), .rst(rst), .d(d), .q(qd));
  nexus_jkff jkff(.clk(clk), .rst(rst), .j(j), .k(k), .q(qjk));
  nexus_tff tff(.clk(clk), .rst(rst), .t(t), .q(qt));
  nexus_shift_register4 sr(.clk(clk), .rst(rst), .mode(mode), .sr_in(sr_in), .sl_in(sl_in), .d(load), .q(sq));
  nexus_counter #(.N(10), .W(4)) ctr(.clk(clk), .rst(rst), .en(en), .up(up), .count(cnt), .tc(tc));
  nexus_ripple_counter4 rc(.clk(clk), .rst(rst), .q(rip));

  always #5 clk = ~clk;

  initial begin
    errors = 0;
    {d, j, k, t, sr_in, sl_in, en, up, mode, load} = 0;
    ref_d = 0; ref_jk = 0; ref_t = 0; ref_s = 0; ref_c = 0; ref_rip = 0;
    #12 rst = 0;
    @(negedge clk);
    ref_rip = rip;  // the ripple counter has already seen the edges since reset
    for (cycle = 0; cycle < 400; cycle = cycle + 1) begin
      // each iteration starts at a falling edge: change inputs away from the rising edge
      {d, j, k, t, sr_in, sl_in, en, up} = $random;
      mode = $random;
      load = $random;
      @(posedge clk);
      // reference models update on the same edge
      ref_d = d;
      case ({j, k}) 2'b01: ref_jk = 0; 2'b10: ref_jk = 1; 2'b11: ref_jk = ~ref_jk; default: ; endcase
      if (t) ref_t = ~ref_t;
      case (mode) 2'b01: ref_s = {sr_in, ref_s[3:1]}; 2'b10: ref_s = {ref_s[2:0], sl_in}; 2'b11: ref_s = load; default: ; endcase
      if (en) ref_c = up ? (ref_c == 9 ? 0 : ref_c + 1) : (ref_c == 0 ? 9 : ref_c - 1);
      ref_rip = ref_rip + 1;
      #1;
      if (qd !== ref_d || qjk !== ref_jk || qt !== ref_t || sq !== ref_s || cnt !== ref_c || rip !== ref_rip) begin
        errors = errors + 1;
        if (errors <= 5) $display("FAIL cycle %0d: d %b/%b jk %b/%b t %b/%b sr %h/%h cnt %0d/%0d rip %0d/%0d", cycle, qd, ref_d, qjk,
                 ref_jk, qt, ref_t, sq, ref_s, cnt, ref_c, rip, ref_rip);
      end
      @(negedge clk);
    end
    if (errors == 0) $display("PASS sequential blocks (400 random cycles)");
    $finish;
  end
endmodule
