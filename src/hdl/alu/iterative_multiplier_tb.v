`timescale 1ns/1ps

module iterative_multiplier_tb;
  reg clk;
  reg reset;
  reg start;
  reg signed_mode;
  reg [31:0] multiplicand;
  reg [31:0] multiplier;
  wire busy;
  wire done;
  wire [63:0] product;

  nexus_iterative_multiplier dut(
      .clk(clk),
      .reset(reset),
      .start(start),
      .signed_mode(signed_mode),
      .multiplicand(multiplicand),
      .multiplier(multiplier),
      .busy(busy),
      .done(done),
      .product(product)
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
        $display("FAIL:multiplier timeout");
        $fatal(1);
      end
    end
  endtask

  initial begin
    clk = 1'b0;
    reset = 1'b1;
    start = 1'b0;
    signed_mode = 1'b0;
    multiplicand = 32'd0;
    multiplier = 32'd0;

    @(posedge clk);
    reset = 1'b0;

    @(negedge clk);
    multiplicand = 32'd7;
    multiplier = 32'd6;
    signed_mode = 1'b0;
    start = 1'b1;
    @(posedge clk);
    @(negedge clk);
    start = 1'b0;
    wait_for_done();
    if (product !== 64'd42) begin
      $display("FAIL:multiplier unsigned product=%0d", product);
      $fatal(1);
    end

    @(negedge clk);
    multiplicand = -32'sd3;
    multiplier = 32'd5;
    signed_mode = 1'b1;
    start = 1'b1;
    @(posedge clk);
    @(negedge clk);
    start = 1'b0;
    wait_for_done();
    if ($signed(product) !== -64'sd15) begin
      $display("FAIL:multiplier signed product=%0d", $signed(product));
      $fatal(1);
    end

    $display("PASS: iterative_multiplier_tb");
    $finish;
  end
endmodule
