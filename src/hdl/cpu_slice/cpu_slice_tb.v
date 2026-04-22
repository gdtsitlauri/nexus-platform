`timescale 1ns/1ps

module cpu_slice_tb;
  reg clk = 0;
  reg reset = 1;
  reg step = 0;
  reg [31:0] instruction = 32'd0;
  reg preload_write_enable = 0;
  reg [4:0] preload_write_addr = 5'd0;
  reg [31:0] preload_write_data = 32'd0;
  wire [31:0] stage_result;
  wire [4:0] stage_dest;
  wire stage_valid;

  nexus_cpu_slice dut(
      .clk(clk),
      .reset(reset),
      .step(step),
      .instruction(instruction),
      .preload_write_enable(preload_write_enable),
      .preload_write_addr(preload_write_addr),
      .preload_write_data(preload_write_data),
      .stage_result(stage_result),
      .stage_dest(stage_dest),
      .stage_valid(stage_valid)
  );

  always #5 clk = ~clk;

  task pulse_step(input [31:0] instr);
    begin
      instruction = instr;
      step = 1'b1;
      @(posedge clk);
      #1;
      step = 1'b0;
      instruction = 32'd0;
      @(posedge clk);
      #1;
    end
  endtask

  task preload_reg(input [4:0] addr, input [31:0] value);
    begin
      preload_write_enable = 1'b1;
      preload_write_addr = addr;
      preload_write_data = value;
      @(posedge clk);
      #1;
      preload_write_enable = 1'b0;
    end
  endtask

  initial begin
    @(posedge clk);
    #1;
    reset = 0;

    preload_reg(5'd8, 32'd1);   // $t0
    preload_reg(5'd9, 32'd2);   // $t1

    // addu $v0, $t0, $t1
    pulse_step({6'h00, 5'd8, 5'd9, 5'd2, 5'd0, 6'h21});
    if (dut.reg_file.regs[2] !== 32'd3) begin
      $display("FAIL: addu writeback mismatch");
      $finish(1);
    end

    // addiu $v0, $v0, 4
    pulse_step({6'h09, 5'd2, 5'd2, 16'd4});
    if (dut.reg_file.regs[2] !== 32'd7) begin
      $display("FAIL: addiu stage/writeback mismatch");
      $finish(1);
    end

    // ori $v1, $zero, 16'h00ff
    pulse_step({6'h0d, 5'd0, 5'd3, 16'h00ff});
    if (dut.reg_file.regs[3] !== 32'h000000ff) begin
      $display("FAIL: ori writeback mismatch");
      $finish(1);
    end

    $display("PASS: cpu_slice_tb");
    $finish(0);
  end
endmodule
