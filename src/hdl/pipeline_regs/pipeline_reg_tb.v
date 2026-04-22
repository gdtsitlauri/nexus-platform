`timescale 1ns/1ps

module pipeline_reg_tb;
  reg clk;
  reg reset;
  reg enable;
  reg flush;
  reg [31:0] d;
  wire [31:0] q;
  wire valid;

  nexus_pipeline_reg dut(
      .clk(clk),
      .reset(reset),
      .enable(enable),
      .flush(flush),
      .d(d),
      .q(q),
      .valid(valid)
  );

  always #5 clk = ~clk;

  initial begin
    clk = 1'b0;
    reset = 1'b1;
    enable = 1'b0;
    flush = 1'b0;
    d = 32'd0;

    @(posedge clk);
    reset = 1'b0;
    if (q !== 32'd0 || valid !== 1'b0) begin
      $display("FAIL:pipeline reset");
      $fatal(1);
    end

    @(negedge clk);
    enable = 1'b1;
    d = 32'hdead_beef;
    @(posedge clk);
    #1;
    if (q !== 32'hdead_beef || valid !== 1'b1) begin
      $display("FAIL:pipeline load");
      $fatal(1);
    end

    @(negedge clk);
    enable = 1'b0;
    d = 32'h0000_0001;
    @(posedge clk);
    #1;
    if (q !== 32'hdead_beef || valid !== 1'b1) begin
      $display("FAIL:pipeline hold");
      $fatal(1);
    end

    @(negedge clk);
    flush = 1'b1;
    @(posedge clk);
    #1;
    if (q !== 32'd0 || valid !== 1'b0) begin
      $display("FAIL:pipeline flush");
      $fatal(1);
    end
    @(negedge clk);
    flush = 1'b0;

    $display("PASS: pipeline_reg_tb");
    $finish;
  end
endmodule
