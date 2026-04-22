module nexus_cpu_slice(
    input  wire        clk,
    input  wire        reset,
    input  wire        step,
    input  wire [31:0] instruction,
    input  wire        preload_write_enable,
    input  wire [4:0]  preload_write_addr,
    input  wire [31:0] preload_write_data,
    output wire [31:0] stage_result,
    output wire [4:0]  stage_dest,
    output wire        stage_valid
);
  wire [5:0] opcode = instruction[31:26];
  wire [4:0] rs = instruction[25:21];
  wire [4:0] rt = instruction[20:16];
  wire [4:0] rd = instruction[15:11];
  wire [5:0] funct = instruction[5:0];
  wire [15:0] imm = instruction[15:0];

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
  wire control_valid;

  nexus_control_unit control(
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
      .valid(control_valid)
  );

  wire [31:0] read_a;
  wire [31:0] read_b;
  wire [31:0] wb_result;
  wire [4:0] wb_dest;
  wire wb_valid;
  wire [31:0] reg_write_data = preload_write_enable ? preload_write_data : wb_result;
  wire [4:0] reg_write_addr = preload_write_enable ? preload_write_addr : wb_dest;
  wire reg_write_enable = preload_write_enable | wb_valid;

  nexus_register_file reg_file(
      .clk(clk),
      .write_enable(reg_write_enable),
      .read_addr_a(rs),
      .read_addr_b(rt),
      .write_addr(reg_write_addr),
      .write_data(reg_write_data),
      .read_data_a(read_a),
      .read_data_b(read_b)
  );

  wire [31:0] imm_ext = {{16{imm[15]}}, imm};
  wire [31:0] alu_rhs = alu_src_imm ? imm_ext : read_b;
  wire [31:0] alu_value;
  wire alu_zero;
  wire alu_overflow;

  nexus_alu alu(
      .a((opcode == 6'h00 && funct == 6'h00) ? {27'd0, instruction[10:6]} : read_a),
      .b(alu_rhs),
      .op(alu_op),
      .result(alu_value),
      .zero(alu_zero),
      .overflow(alu_overflow)
  );

  wire [4:0] execute_dest = link ? 5'd31 : ((opcode == 6'h00) ? rd : rt);
  wire execute_valid = step && control_valid && reg_write && !mem_read && !mem_write && !branch_eq &&
      !branch_ne && !jump && !jump_reg && !mem_to_reg;

  nexus_pipeline_reg #(.WIDTH(32)) result_reg(
      .clk(clk),
      .reset(reset),
      .enable(step && execute_valid),
      .flush(step && !execute_valid),
      .d(alu_value),
      .q(wb_result),
      .valid(wb_valid)
  );

  nexus_pipeline_reg #(.WIDTH(5)) dest_reg(
      .clk(clk),
      .reset(reset),
      .enable(step && execute_valid),
      .flush(step && !execute_valid),
      .d(execute_dest),
      .q(wb_dest),
      .valid()
  );

  assign stage_result = alu_value;
  assign stage_dest = execute_dest;
  assign stage_valid = execute_valid && !alu_overflow;
endmodule
