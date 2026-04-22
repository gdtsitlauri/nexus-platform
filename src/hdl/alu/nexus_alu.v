module nexus_alu(
    input  wire [31:0] a,
    input  wire [31:0] b,
    input  wire [3:0]  op,
    output reg  [31:0] result,
    output wire        zero,
    output reg         overflow
);
  localparam ALU_ADD = 4'd0;
  localparam ALU_SUB = 4'd1;
  localparam ALU_AND = 4'd2;
  localparam ALU_OR  = 4'd3;
  localparam ALU_XOR = 4'd4;
  localparam ALU_SLT = 4'd5;
  localparam ALU_SLL = 4'd6;
  localparam ALU_SRL = 4'd7;

  wire [31:0] add_sum;
  wire add_cout;
  wire add_overflow;
  wire [31:0] sub_sum;
  wire sub_cout;
  wire sub_overflow;

  nexus_adder add_adder(
      .a(a),
      .b(b),
      .cin(1'b0),
      .sum(add_sum),
      .cout(add_cout),
      .overflow(add_overflow)
  );

  nexus_adder sub_adder(
      .a(a),
      .b(~b),
      .cin(1'b1),
      .sum(sub_sum),
      .cout(sub_cout),
      .overflow(sub_overflow)
  );

  always @* begin
    result = 32'd0;
    overflow = 1'b0;
    case (op)
      ALU_ADD: begin
        result = add_sum;
        overflow = add_overflow;
      end
      ALU_SUB: begin
        result = sub_sum;
        overflow = sub_overflow;
      end
      ALU_AND: result = a & b;
      ALU_OR:  result = a | b;
      ALU_XOR: result = a ^ b;
      ALU_SLT: result = ($signed(a) < $signed(b)) ? 32'd1 : 32'd0;
      ALU_SLL: result = b << a[4:0];
      ALU_SRL: result = b >> a[4:0];
      default: result = 32'd0;
    endcase
  end

  assign zero = (result == 32'd0);
endmodule
