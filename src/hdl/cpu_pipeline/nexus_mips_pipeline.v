// Nexus 5-stage pipelined MIPS core (IF, ID, EX, MEM, WB).
//
// Implements the Nexus MIPS subset with real MIPS32 encodings:
//   R: add addu sub and or xor slt sltu sll jr mult div mfhi mflo
//   I: addiu ori xori sltiu lui lw sw beq bne     J: j jal
// Hazards:
//   - data:    EX/MEM -> EX and MEM/WB -> EX forwarding, write-before-read register file,
//              one-cycle load-use interlock
//   - control: branches and jumps resolve in EX; the two younger instructions are flushed
//   - HI/LO:   mult/div update HI/LO in EX, so a following mfhi/mflo always sees the new value
// The program counter counts instruction words (as in the Nexus simulators).  Execution halts when
// `jr $ra` targets the sentinel 0x80000000 that reset places in $ra; the core then drains the
// pipeline and raises `halted`, with the exit code in $v0.
module nexus_mips_pipeline #(
    parameter IMEM_WORDS = 4096,
    parameter DMEM_WORDS = 16384
) (
    input  wire        clk,
    input  wire        reset,
    input  wire [31:0] entry_pc,
    // program-load port (used while reset is held; tie low when the image is preloaded)
    input  wire        imem_we,
    input  wire [31:0] imem_waddr,
    input  wire [31:0] imem_wdata,
    output reg         halted,
    output wire [31:0] exit_code,
    output reg  [31:0] retired,
    output reg  [31:0] cycles,
    output reg  [31:0] stalls,
    output reg  [31:0] flushes
);
  localparam [31:0] HALT_ADDRESS = 32'h80000000;

  // ---------------------------------------------------------------- state
  reg [31:0] imem [0:IMEM_WORDS-1];
  reg [31:0] dmem [0:DMEM_WORDS-1];
  reg [31:0] regs [0:31];
  reg [31:0] hi;
  reg [31:0] lo;
  reg [31:0] pc;
  reg        fetch_stopped;

  assign exit_code = regs[2];

  // IF/ID
  reg        ifid_valid;
  reg [31:0] ifid_instr;
  reg [31:0] ifid_pc;

  // ID/EX
  reg        idex_valid;
  reg [31:0] idex_pc;
  reg [31:0] idex_rs_val;
  reg [31:0] idex_rt_val;
  reg [4:0]  idex_rs;
  reg [4:0]  idex_rt;
  reg [4:0]  idex_dest;
  reg [31:0] idex_imm;
  reg [4:0]  idex_shamt;
  reg [4:0]  idex_op;
  reg        idex_reg_write;
  reg        idex_mem_read;
  reg        idex_mem_write;
  reg [25:0] idex_jump_target;

  // EX/MEM
  reg        exmem_valid;
  reg [31:0] exmem_alu;
  reg [31:0] exmem_store;
  reg [4:0]  exmem_dest;
  reg        exmem_reg_write;
  reg        exmem_mem_read;
  reg        exmem_mem_write;
  reg        exmem_halt;

  // MEM/WB
  reg        memwb_valid;
  reg [31:0] memwb_result;
  reg [4:0]  memwb_dest;
  reg        memwb_reg_write;
  reg        memwb_halt;

  // ---------------------------------------------------------------- internal operation codes
  localparam OP_NOP = 5'd0,  OP_ADD = 5'd1,  OP_SUB = 5'd2,  OP_AND = 5'd3,  OP_OR = 5'd4,
             OP_XOR = 5'd5,  OP_SLT = 5'd6,  OP_SLTU = 5'd7, OP_SLL = 5'd8,  OP_LUI = 5'd9,
             OP_ORI = 5'd10, OP_XORI = 5'd11, OP_SLTIU = 5'd12, OP_ADDI = 5'd13, OP_LW = 5'd14,
             OP_SW = 5'd15,  OP_BEQ = 5'd16, OP_BNE = 5'd17, OP_J = 5'd18,   OP_JAL = 5'd19,
             OP_JR = 5'd20,  OP_MULT = 5'd21, OP_DIV = 5'd22, OP_MFHI = 5'd23, OP_MFLO = 5'd24,
             OP_ILLEGAL = 5'd31;

  // ---------------------------------------------------------------- ID: decode
  wire [5:0]  id_opcode = ifid_instr[31:26];
  wire [4:0]  id_rs     = ifid_instr[25:21];
  wire [4:0]  id_rt     = ifid_instr[20:16];
  wire [4:0]  id_rd     = ifid_instr[15:11];
  wire [4:0]  id_shamt  = ifid_instr[10:6];
  wire [5:0]  id_funct  = ifid_instr[5:0];
  wire [15:0] id_imm16  = ifid_instr[15:0];
  wire [31:0] id_sext   = {{16{id_imm16[15]}}, id_imm16};
  wire [31:0] id_zext   = {16'd0, id_imm16};

  reg [4:0]  id_op;
  reg [4:0]  id_dest;
  reg        id_reg_write;
  reg        id_uses_rs;
  reg        id_uses_rt;
  reg [31:0] id_imm;

  always @(*) begin
    id_op = OP_ILLEGAL;
    id_dest = 5'd0;
    id_reg_write = 1'b0;
    id_uses_rs = 1'b0;
    id_uses_rt = 1'b0;
    id_imm = id_sext;
    case (id_opcode)
      6'h00: begin
        id_dest = id_rd;
        id_reg_write = 1'b1;
        id_uses_rs = 1'b1;
        id_uses_rt = 1'b1;
        case (id_funct)
          6'h20, 6'h21: id_op = OP_ADD;
          6'h22: id_op = OP_SUB;
          6'h24: id_op = OP_AND;
          6'h25: id_op = OP_OR;
          6'h26: id_op = OP_XOR;
          6'h2a: id_op = OP_SLT;
          6'h2b: id_op = OP_SLTU;
          6'h00: begin id_op = OP_SLL; id_uses_rs = 1'b0; end
          6'h08: begin id_op = OP_JR; id_reg_write = 1'b0; id_uses_rt = 1'b0; end
          6'h18: begin id_op = OP_MULT; id_reg_write = 1'b0; end
          6'h1a: begin id_op = OP_DIV; id_reg_write = 1'b0; end
          6'h10: begin id_op = OP_MFHI; id_uses_rs = 1'b0; id_uses_rt = 1'b0; end
          6'h12: begin id_op = OP_MFLO; id_uses_rs = 1'b0; id_uses_rt = 1'b0; end
          default: begin id_op = OP_ILLEGAL; id_reg_write = 1'b0; end
        endcase
      end
      6'h09: begin id_op = OP_ADDI;  id_dest = id_rt; id_reg_write = 1'b1; id_uses_rs = 1'b1; end
      6'h0b: begin id_op = OP_SLTIU; id_dest = id_rt; id_reg_write = 1'b1; id_uses_rs = 1'b1; end
      6'h0d: begin id_op = OP_ORI;   id_dest = id_rt; id_reg_write = 1'b1; id_uses_rs = 1'b1; id_imm = id_zext; end
      6'h0e: begin id_op = OP_XORI;  id_dest = id_rt; id_reg_write = 1'b1; id_uses_rs = 1'b1; id_imm = id_zext; end
      6'h0f: begin id_op = OP_LUI;   id_dest = id_rt; id_reg_write = 1'b1; id_imm = id_zext; end
      6'h23: begin id_op = OP_LW;    id_dest = id_rt; id_reg_write = 1'b1; id_uses_rs = 1'b1; end
      6'h2b: begin id_op = OP_SW;    id_uses_rs = 1'b1; id_uses_rt = 1'b1; end
      6'h04: begin id_op = OP_BEQ;   id_uses_rs = 1'b1; id_uses_rt = 1'b1; end
      6'h05: begin id_op = OP_BNE;   id_uses_rs = 1'b1; id_uses_rt = 1'b1; end
      6'h02: begin id_op = OP_J; end
      6'h03: begin id_op = OP_JAL;   id_dest = 5'd31; id_reg_write = 1'b1; end
      default: id_op = OP_ILLEGAL;
    endcase
    if (id_dest == 5'd0) begin
      id_reg_write = 1'b0;
    end
  end

  // Register read with write-before-read bypass from WB.
  wire [31:0] id_rs_val = (memwb_valid && memwb_reg_write && memwb_dest == id_rs && id_rs != 5'd0)
                          ? memwb_result : regs[id_rs];
  wire [31:0] id_rt_val = (memwb_valid && memwb_reg_write && memwb_dest == id_rt && id_rt != 5'd0)
                          ? memwb_result : regs[id_rt];

  // Load-use interlock.
  wire load_use = idex_valid && idex_mem_read && idex_dest != 5'd0 && ifid_valid &&
                  ((id_uses_rs && idex_dest == id_rs) || (id_uses_rt && idex_dest == id_rt));

  // ---------------------------------------------------------------- EX: forwarding and execution
  wire fwd_mem_rs = exmem_valid && exmem_reg_write && !exmem_mem_read && exmem_dest != 5'd0 && exmem_dest == idex_rs;
  wire fwd_wb_rs  = memwb_valid && memwb_reg_write && memwb_dest != 5'd0 && memwb_dest == idex_rs;
  wire fwd_mem_rt = exmem_valid && exmem_reg_write && !exmem_mem_read && exmem_dest != 5'd0 && exmem_dest == idex_rt;
  wire fwd_wb_rt  = memwb_valid && memwb_reg_write && memwb_dest != 5'd0 && memwb_dest == idex_rt;
  wire [31:0] ex_a = fwd_mem_rs ? exmem_alu : (fwd_wb_rs ? memwb_result : idex_rs_val);
  wire [31:0] ex_b = fwd_mem_rt ? exmem_alu : (fwd_wb_rt ? memwb_result : idex_rt_val);

  wire signed [63:0] ex_product = $signed(ex_a) * $signed(ex_b);
  wire        ex_div_ok   = ex_b != 32'd0;
  wire [31:0] ex_quotient = ex_div_ok ? $signed(ex_a) / $signed(ex_b) : 32'd0;
  wire [31:0] ex_remainder = ex_div_ok ? $signed(ex_a) % $signed(ex_b) : 32'd0;

  reg [31:0] ex_result;
  always @(*) begin
    case (idex_op)
      OP_ADD:   ex_result = ex_a + ex_b;
      OP_SUB:   ex_result = ex_a - ex_b;
      OP_AND:   ex_result = ex_a & ex_b;
      OP_OR:    ex_result = ex_a | ex_b;
      OP_XOR:   ex_result = ex_a ^ ex_b;
      OP_SLT:   ex_result = ($signed(ex_a) < $signed(ex_b)) ? 32'd1 : 32'd0;
      OP_SLTU:  ex_result = (ex_a < ex_b) ? 32'd1 : 32'd0;
      OP_SLL:   ex_result = ex_b << idex_shamt;
      OP_LUI:   ex_result = {idex_imm[15:0], 16'd0};
      OP_ORI:   ex_result = ex_a | idex_imm;
      OP_XORI:  ex_result = ex_a ^ idex_imm;
      OP_SLTIU: ex_result = (ex_a < idex_imm) ? 32'd1 : 32'd0;
      OP_ADDI, OP_LW, OP_SW: ex_result = ex_a + idex_imm;
      OP_JAL:   ex_result = idex_pc + 32'd1;
      OP_MFHI:  ex_result = hi;
      OP_MFLO:  ex_result = lo;
      default:  ex_result = 32'd0;
    endcase
  end

  wire ex_taken_branch = idex_valid && ((idex_op == OP_BEQ && ex_a == ex_b) || (idex_op == OP_BNE && ex_a != ex_b));
  wire ex_jump = idex_valid && (idex_op == OP_J || idex_op == OP_JAL);
  wire ex_halt = idex_valid && idex_op == OP_JR && ex_a == HALT_ADDRESS;
  wire ex_jr = idex_valid && idex_op == OP_JR && !ex_halt;
  wire ex_redirect = ex_taken_branch || ex_jump || ex_jr;
  wire [31:0] ex_target = ex_taken_branch ? idex_pc + 32'd1 + idex_imm
                        : ex_jump ? {6'd0, idex_jump_target}
                        : ex_a;

  // ---------------------------------------------------------------- MEM
  wire [31:0] mem_word_index = {2'b00, exmem_alu[31:2]};
  wire [31:0] mem_load = (mem_word_index < DMEM_WORDS) ? dmem[mem_word_index] : 32'd0;

  // ---------------------------------------------------------------- sequential logic
  always @(posedge clk) begin
    if (imem_we && imem_waddr < IMEM_WORDS) begin
      imem[imem_waddr] <= imem_wdata;
    end
  end

  integer index;
  always @(posedge clk) begin
    if (reset) begin
      pc <= entry_pc;
      fetch_stopped <= 1'b0;
      halted <= 1'b0;
      retired <= 32'd0;
      cycles <= 32'd0;
      stalls <= 32'd0;
      flushes <= 32'd0;
      hi <= 32'd0;
      lo <= 32'd0;
      ifid_valid <= 1'b0;
      idex_valid <= 1'b0;
      exmem_valid <= 1'b0;
      memwb_valid <= 1'b0;
      for (index = 0; index < 32; index = index + 1) begin
        regs[index] <= 32'd0;
      end
      regs[29] <= (DMEM_WORDS - 16) * 4;  // $sp
      regs[31] <= HALT_ADDRESS;           // $ra
    end else if (!halted) begin
      cycles <= cycles + 32'd1;

      // WB
      if (memwb_valid) begin
        retired <= retired + 32'd1;
        if (memwb_reg_write && memwb_dest != 5'd0) begin
          regs[memwb_dest] <= memwb_result;
        end
        if (memwb_halt) begin
          halted <= 1'b1;
        end
      end

      // MEM -> MEM/WB
      memwb_valid <= exmem_valid;
      memwb_result <= exmem_mem_read ? mem_load : exmem_alu;
      memwb_dest <= exmem_dest;
      memwb_reg_write <= exmem_reg_write;
      memwb_halt <= exmem_halt;
      if (exmem_valid && exmem_mem_write && mem_word_index < DMEM_WORDS) begin
        dmem[mem_word_index] <= exmem_store;
      end

      // EX -> EX/MEM
      exmem_valid <= idex_valid;
      exmem_alu <= ex_result;
      exmem_store <= ex_b;
      exmem_dest <= idex_dest;
      exmem_reg_write <= idex_valid && idex_reg_write;
      exmem_mem_read <= idex_valid && idex_mem_read;
      exmem_mem_write <= idex_valid && idex_mem_write;
      exmem_halt <= ex_halt;
      if (idex_valid && idex_op == OP_MULT) begin
        hi <= ex_product[63:32];
        lo <= ex_product[31:0];
      end
      if (idex_valid && idex_op == OP_DIV && ex_div_ok) begin
        hi <= ex_remainder;
        lo <= ex_quotient;
      end

      // ID -> ID/EX, IF -> IF/ID, PC
      if (ex_halt) begin
        fetch_stopped <= 1'b1;
        idex_valid <= 1'b0;
        ifid_valid <= 1'b0;
        flushes <= flushes + 32'd2;
      end else if (ex_redirect) begin
        pc <= ex_target;
        idex_valid <= 1'b0;
        ifid_valid <= 1'b0;
        flushes <= flushes + {30'd0, ifid_valid} + {31'd0, 1'b1};
      end else if (load_use) begin
        idex_valid <= 1'b0;  // bubble; PC and IF/ID hold
        stalls <= stalls + 32'd1;
      end else begin
        idex_valid <= ifid_valid;
        idex_pc <= ifid_pc;
        idex_rs_val <= id_rs_val;
        idex_rt_val <= id_rt_val;
        idex_rs <= id_uses_rs ? id_rs : 5'd0;
        idex_rt <= id_uses_rt ? id_rt : 5'd0;
        idex_dest <= id_reg_write ? id_dest : 5'd0;
        idex_imm <= id_imm;
        idex_shamt <= id_shamt;
        idex_op <= id_op;
        idex_reg_write <= id_reg_write;
        idex_mem_read <= id_op == OP_LW;
        idex_mem_write <= id_op == OP_SW;
        idex_jump_target <= ifid_instr[25:0];
        if (!fetch_stopped && pc < IMEM_WORDS) begin
          ifid_valid <= 1'b1;
          ifid_instr <= imem[pc];
          ifid_pc <= pc;
          pc <= pc + 32'd1;
        end else begin
          ifid_valid <= 1'b0;
        end
      end
    end
  end
endmodule
