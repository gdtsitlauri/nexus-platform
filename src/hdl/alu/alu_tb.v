`timescale 1ns/1ps

module alu_tb;
  reg [31:0] a;
  reg [31:0] b;
  reg [3:0]  op;
  wire [31:0] result;
  wire zero;
  wire overflow;

  nexus_alu dut(.a(a), .b(b), .op(op), .result(result), .zero(zero), .overflow(overflow));

  task check_case;
    input [31:0] expected_result;
    input        expected_zero;
    input        expected_overflow;
    input [255:0] label;
    begin
      #1;
      if (result !== expected_result || zero !== expected_zero || overflow !== expected_overflow) begin
        $display(
            "FAIL:alu:%0s result=%h zero=%b overflow=%b",
            label,
            result,
            zero,
            overflow
        );
        $fatal(1);
      end
    end
  endtask

  initial begin
    a = 32'd4; b = 32'd5; op = 4'd0; check_case(32'd9, 1'b0, 1'b0, "add");
    a = 32'd9; b = 32'd9; op = 4'd1; check_case(32'd0, 1'b1, 1'b0, "sub-zero");
    a = 32'h0f0f_0f0f; b = 32'h00ff_00ff; op = 4'd2; check_case(32'h000f_000f, 1'b0, 1'b0, "and");
    a = 32'h0f0f_0f0f; b = 32'h00ff_00ff; op = 4'd3; check_case(32'h0fff_0fff, 1'b0, 1'b0, "or");
    a = 32'h0f0f_0f0f; b = 32'h00ff_00ff; op = 4'd4; check_case(32'h0ff0_0ff0, 1'b0, 1'b0, "xor");
    a = 32'hffff_fffd; b = 32'd2; op = 4'd5; check_case(32'd1, 1'b0, 1'b0, "slt");
    a = 32'd2; b = 32'd3; op = 4'd6; check_case(32'd12, 1'b0, 1'b0, "sll");
    a = 32'd2; b = 32'd16; op = 4'd7; check_case(32'd4, 1'b0, 1'b0, "srl");
    a = 32'h7fff_ffff; b = 32'd1; op = 4'd0; check_case(32'h8000_0000, 1'b0, 1'b1, "overflow");
    $display("PASS: alu_tb");
    $finish;
  end
endmodule
