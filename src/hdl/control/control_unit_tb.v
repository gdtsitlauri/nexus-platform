`timescale 1ns/1ps

module control_unit_tb;
  reg [5:0] opcode;
  reg [5:0] funct;
  wire reg_write;
  wire mem_read;
  wire mem_write;
  wire branch_eq;
  wire branch_ne;
  wire jump;
  wire jump_reg;
  wire alu_src_imm;
  wire mem_to_reg;
  wire link;
  wire [3:0] alu_op;
  wire valid;

  nexus_control_unit dut(
      .opcode(opcode),
      .funct(funct),
      .reg_write(reg_write),
      .mem_read(mem_read),
      .mem_write(mem_write),
      .branch_eq(branch_eq),
      .branch_ne(branch_ne),
      .jump(jump),
      .jump_reg(jump_reg),
      .alu_src_imm(alu_src_imm),
      .mem_to_reg(mem_to_reg),
      .link(link),
      .alu_op(alu_op),
      .valid(valid)
  );

  task check_case;
    input expected_valid;
    input expected_reg_write;
    input expected_mem_read;
    input expected_mem_write;
    input expected_branch_eq;
    input expected_branch_ne;
    input expected_jump;
    input expected_jump_reg;
    input expected_link;
    input [255:0] label;
    begin
      #1;
      if (valid !== expected_valid || reg_write !== expected_reg_write || mem_read !== expected_mem_read ||
          mem_write !== expected_mem_write || branch_eq !== expected_branch_eq ||
          branch_ne !== expected_branch_ne || jump !== expected_jump ||
          jump_reg !== expected_jump_reg || link !== expected_link) begin
        $display("FAIL:control:%0s", label);
        $fatal(1);
      end
    end
  endtask

  initial begin
    opcode = 6'b000000; funct = 6'h20; check_case(1'b1, 1'b1, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, "add");
    opcode = 6'h23; funct = 6'd0; check_case(1'b1, 1'b1, 1'b1, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, "lw");
    opcode = 6'h2b; funct = 6'd0; check_case(1'b1, 1'b0, 1'b0, 1'b1, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, "sw");
    opcode = 6'h04; funct = 6'd0; check_case(1'b1, 1'b0, 1'b0, 1'b0, 1'b1, 1'b0, 1'b0, 1'b0, 1'b0, "beq");
    opcode = 6'h03; funct = 6'd0; check_case(1'b1, 1'b1, 1'b0, 1'b0, 1'b0, 1'b0, 1'b1, 1'b0, 1'b1, "jal");
    opcode = 6'h3f; funct = 6'd0; check_case(1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, "invalid");
    $display("PASS: control_unit_tb");
    $finish;
  end
endmodule
