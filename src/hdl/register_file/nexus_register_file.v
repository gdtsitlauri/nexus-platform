module nexus_register_file(
    input  wire        clk,
    input  wire        write_enable,
    input  wire [4:0]  read_addr_a,
    input  wire [4:0]  read_addr_b,
    input  wire [4:0]  write_addr,
    input  wire [31:0] write_data,
    output wire [31:0] read_data_a,
    output wire [31:0] read_data_b
);
  reg [31:0] regs [0:31];
  integer i;

  initial begin
    for (i = 0; i < 32; i = i + 1) begin
      regs[i] = 32'd0;
    end
  end

  assign read_data_a = (read_addr_a == 5'd0) ? 32'd0 : regs[read_addr_a];
  assign read_data_b = (read_addr_b == 5'd0) ? 32'd0 : regs[read_addr_b];

  always @(posedge clk) begin
    if (write_enable && write_addr != 5'd0) begin
      regs[write_addr] <= write_data;
    end
    regs[0] <= 32'd0;
  end
endmodule
