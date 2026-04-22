`timescale 1ns/1ps

module iterative_divider_tb;
  reg clk;
  reg reset;
  reg start;
  reg signed_mode;
  reg [31:0] dividend;
  reg [31:0] divisor;
  wire busy;
  wire done;
  wire divide_by_zero;
  wire [31:0] quotient;
  wire [31:0] remainder;

  nexus_iterative_divider dut(
      .clk(clk),
      .reset(reset),
      .start(start),
      .signed_mode(signed_mode),
      .dividend(dividend),
      .divisor(divisor),
      .busy(busy),
      .done(done),
      .divide_by_zero(divide_by_zero),
      .quotient(quotient),
      .remainder(remainder)
  );

  always #5 clk = ~clk;

  task wait_for_done;
    integer steps;
    begin
      steps = 0;
      while (!done && steps < 40) begin
        @(posedge clk);
        steps = steps + 1;
      end
      if (!done) begin
        $display("FAIL:divider timeout");
        $fatal(1);
      end
    end
  endtask

  initial begin
    clk = 1'b0;
    reset = 1'b1;
    start = 1'b0;
    signed_mode = 1'b0;
    dividend = 32'd0;
    divisor = 32'd1;

    @(posedge clk);
    reset = 1'b0;

    @(negedge clk);
    dividend = 32'd20;
    divisor = 32'd3;
    signed_mode = 1'b0;
    start = 1'b1;
    @(posedge clk);
    @(negedge clk);
    start = 1'b0;
    wait_for_done();
    if (quotient !== 32'd6 || remainder !== 32'd2 || divide_by_zero !== 1'b0) begin
      $display("FAIL:divider unsigned q=%0d r=%0d", quotient, remainder);
      $fatal(1);
    end

    @(negedge clk);
    dividend = -32'sd21;
    divisor = 32'd4;
    signed_mode = 1'b1;
    start = 1'b1;
    @(posedge clk);
    @(negedge clk);
    start = 1'b0;
    wait_for_done();
    if ($signed(quotient) !== -32'sd5 || $signed(remainder) !== -32'sd1) begin
      $display("FAIL:divider signed q=%0d r=%0d", $signed(quotient), $signed(remainder));
      $fatal(1);
    end

    @(negedge clk);
    dividend = 32'd9;
    divisor = 32'd0;
    signed_mode = 1'b0;
    start = 1'b1;
    @(posedge clk);
    @(negedge clk);
    start = 1'b0;
    @(posedge clk);
    if (divide_by_zero !== 1'b1 || quotient !== 32'd0 || remainder !== 32'd9) begin
      $display("FAIL:divider divide-by-zero");
      $fatal(1);
    end

    $display("PASS: iterative_divider_tb");
    $finish;
  end
endmodule
