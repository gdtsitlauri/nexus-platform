`timescale 1ns/1ps

module adder_tb;
  reg [31:0] a;
  reg [31:0] b;
  reg        cin;
  wire [31:0] sum;
  wire        cout;
  wire        overflow;

  nexus_adder dut(.a(a), .b(b), .cin(cin), .sum(sum), .cout(cout), .overflow(overflow));

  task check_case;
    input [31:0] expected_sum;
    input        expected_cout;
    input        expected_overflow;
    input [255:0] label;
    begin
      #1;
      if (sum !== expected_sum || cout !== expected_cout || overflow !== expected_overflow) begin
        $display("FAIL:add:%0s sum=%h cout=%b overflow=%b", label, sum, cout, overflow);
        $fatal(1);
      end
    end
  endtask

  initial begin
    a = 32'd10; b = 32'd20; cin = 1'b0;
    check_case(32'd30, 1'b0, 1'b0, "simple");

    a = 32'hffff_ffff; b = 32'd1; cin = 1'b0;
    check_case(32'd0, 1'b1, 1'b0, "carry");

    a = 32'h7fff_ffff; b = 32'd1; cin = 1'b0;
    check_case(32'h8000_0000, 1'b0, 1'b1, "overflow");

    $display("PASS: adder_tb");
    $finish;
  end
endmodule
