module nexus_control_unit(
    input  wire [5:0] opcode,
    input  wire [5:0] funct,
    output reg        reg_write,
    output reg        mem_read,
    output reg        mem_write,
    output reg        branch_eq,
    output reg        branch_ne,
    output reg        jump,
    output reg        jump_reg,
    output reg        alu_src_imm,
    output reg        mem_to_reg,
    output reg        link,
    output reg  [3:0] alu_op,
    output reg        valid
);
  localparam ALU_ADD = 4'd0;
  localparam ALU_SUB = 4'd1;
  localparam ALU_AND = 4'd2;
  localparam ALU_OR  = 4'd3;
  localparam ALU_XOR = 4'd4;
  localparam ALU_SLT = 4'd5;

  always @* begin
    reg_write = 1'b0;
    mem_read = 1'b0;
    mem_write = 1'b0;
    branch_eq = 1'b0;
    branch_ne = 1'b0;
    jump = 1'b0;
    jump_reg = 1'b0;
    alu_src_imm = 1'b0;
    mem_to_reg = 1'b0;
    link = 1'b0;
    alu_op = ALU_ADD;
    valid = 1'b1;

    case (opcode)
      6'b000000: begin
        reg_write = 1'b1;
        case (funct)
          6'h20, 6'h21: alu_op = ALU_ADD;
          6'h22: alu_op = ALU_SUB;
          6'h24: alu_op = ALU_AND;
          6'h25: alu_op = ALU_OR;
          6'h26: alu_op = ALU_XOR;
          6'h2a: alu_op = ALU_SLT;
          6'h08: begin
            reg_write = 1'b0;
            jump_reg = 1'b1;
          end
          default: valid = 1'b0;
        endcase
      end
      6'h08, 6'h09: begin
        reg_write = 1'b1;
        alu_src_imm = 1'b1;
        alu_op = ALU_ADD;
      end
      6'h0c: begin
        reg_write = 1'b1;
        alu_src_imm = 1'b1;
        alu_op = ALU_AND;
      end
      6'h0d: begin
        reg_write = 1'b1;
        alu_src_imm = 1'b1;
        alu_op = ALU_OR;
      end
      6'h0e: begin
        reg_write = 1'b1;
        alu_src_imm = 1'b1;
        alu_op = ALU_XOR;
      end
      6'h0a: begin
        reg_write = 1'b1;
        alu_src_imm = 1'b1;
        alu_op = ALU_SLT;
      end
      6'h23: begin
        reg_write = 1'b1;
        mem_read = 1'b1;
        alu_src_imm = 1'b1;
        mem_to_reg = 1'b1;
      end
      6'h2b: begin
        mem_write = 1'b1;
        alu_src_imm = 1'b1;
      end
      6'h04: begin
        branch_eq = 1'b1;
        alu_op = ALU_SUB;
      end
      6'h05: begin
        branch_ne = 1'b1;
        alu_op = ALU_SUB;
      end
      6'h02: jump = 1'b1;
      6'h03: begin
        jump = 1'b1;
        link = 1'b1;
        reg_write = 1'b1;
      end
      default: valid = 1'b0;
    endcase
  end
endmodule
