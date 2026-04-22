`timescale 1ns/1ps

module register_file_tb;
  reg clk;
  reg write_enable;
  reg [4:0] read_addr_a;
  reg [4:0] read_addr_b;
  reg [4:0] write_addr;
  reg [31:0] write_data;
  wire [31:0] read_data_a;
  wire [31:0] read_data_b;

  nexus_register_file dut(
      .clk(clk),
      .write_enable(write_enable),
      .read_addr_a(read_addr_a),
      .read_addr_b(read_addr_b),
      .write_addr(write_addr),
      .write_data(write_data),
      .read_data_a(read_data_a),
      .read_data_b(read_data_b)
  );

  always #5 clk = ~clk;

  initial begin
    clk = 1'b0;
    write_enable = 1'b0;
    read_addr_a = 5'd0;
    read_addr_b = 5'd0;
    write_addr = 5'd0;
    write_data = 32'd0;

    #1;
    if (read_data_a !== 32'd0 || read_data_b !== 32'd0) begin
      $display("FAIL:regfile zero register read");
      $fatal(1);
    end

    @(negedge clk);
    write_enable = 1'b1;
    write_addr = 5'd5;
    write_data = 32'h1234_5678;
    @(posedge clk);
    @(negedge clk);
    write_enable = 1'b0;
    read_addr_a = 5'd5;
    #1;
    if (read_data_a !== 32'h1234_5678) begin
      $display("FAIL:regfile write/read mismatch");
      $fatal(1);
    end

    write_enable = 1'b1;
    write_addr = 5'd0;
    write_data = 32'hffff_ffff;
    @(posedge clk);
    @(negedge clk);
    write_enable = 1'b0;
    read_addr_b = 5'd0;
    #1;
    if (read_data_b !== 32'd0) begin
      $display("FAIL:regfile x0 overwritten");
      $fatal(1);
    end

    $display("PASS: register_file_tb");
    $finish;
  end
endmodule
