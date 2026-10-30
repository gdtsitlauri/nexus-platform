// Nexus multi-cycle MIPS core (EY321 (d): the control unit at the level of micro-operations, single- and
// multi-cycle timing, hardwired and microprogrammed implementation).
//
// One datapath (IR, A, B, ALUOut, MDR, HI/LO, register file, separate instruction and data memories) executes each
// instruction in 3-5 steps, as in Patterson & Hennessy:
//   0 FETCH     IR <= imem[PC], PC <= PC + 1
//   1 DECODE    A <= R[rs], B <= R[rt], ALUOut <= PC + sext(imm)            (branch target)
//   2 MEMADDR   ALUOut <= A + sext(imm)            3 MEMREAD   MDR <= dmem[ALUOut]     4 LOADWB  R[rt] <= MDR
//   5 MEMWRITE  dmem[ALUOut] <= B
//   6 REXEC     ALUOut <= A op B (or B << shamt)   7 RWB       R[rd] <= ALUOut
//   8 BRANCH    if (A ==/!= B) PC <= ALUOut        9 JUMP      PC <= target (jal: R[31] <= PC)
//  10 IEXEC     ALUOut <= A op imm                11 IWB       R[rt] <= ALUOut
//  12 JR        PC <= A (halt on the sentinel)    13 MULDIV    HI, LO <= A * B or A / B, A % B
//  14 MFWB      R[rd] <= HI or LO                 15 STOP      illegal instruction
// MICROCODE = 0: a hardwired control unit (a finite-state machine in combinational logic).
// MICROCODE = 1: a microprogrammed control unit: a control store indexed by the micro-PC, whose words hold the
//   control signals and a sequencing field (next, fetch, dispatch on the opcode class, dispatch load/store).
// Both produce the same control signals in every state, so both run every program identically; the PC counts
// instruction words, and `jr $ra` to the sentinel 0x80000000 halts with the exit code in $v0.
module nexus_mips_multicycle #(
    parameter IMEM_WORDS = 4096,
    parameter DMEM_WORDS = 16384,
    parameter MICROCODE = 0
) (
    input  wire        clk,
    input  wire        reset,
    input  wire [31:0] entry_pc,
    output reg         halted,
    output wire [31:0] exit_code,
    output reg  [31:0] retired,
    output reg  [31:0] cycles
);
  localparam [31:0] HALT_ADDRESS = 32'h80000000;
  localparam [3:0] S_FETCH = 4'd0, S_DECODE = 4'd1, S_MEMADDR = 4'd2, S_MEMREAD = 4'd3, S_LOADWB = 4'd4,
                   S_MEMWRITE = 4'd5, S_REXEC = 4'd6, S_RWB = 4'd7, S_BRANCH = 4'd8, S_JUMP = 4'd9,
                   S_IEXEC = 4'd10, S_IWB = 4'd11, S_JR = 4'd12, S_MULDIV = 4'd13, S_MFWB = 4'd14, S_STOP = 4'd15;

  reg [31:0] imem [0:IMEM_WORDS-1];
  reg [31:0] dmem [0:DMEM_WORDS-1];
  reg [31:0] regs [0:31];
  reg [31:0] pc, ir, a, b, alu_out, mdr, hi, lo;
  reg [3:0]  state;
  assign exit_code = regs[2];

  // ---------------------------------------------------------------- instruction fields and opcode class
  wire [5:0]  opcode = ir[31:26];
  wire [4:0]  rs = ir[25:21], rt = ir[20:16], rd = ir[15:11], shamt = ir[10:6];
  wire [5:0]  funct = ir[5:0];
  wire [31:0] sext = {{16{ir[15]}}, ir[15:0]};
  wire [31:0] zext = {16'd0, ir[15:0]};

  localparam [3:0] C_RALU = 4'd0, C_JR = 4'd1, C_MULDIV = 4'd2, C_MF = 4'd3, C_LOADSTORE = 4'd4, C_BRANCH = 4'd5,
                   C_JUMP = 4'd6, C_IALU = 4'd7, C_ILLEGAL = 4'd8;
  reg [3:0] op_class;
  always @(*) begin
    case (opcode)
      6'h00: case (funct)
               6'h20, 6'h21, 6'h22, 6'h24, 6'h25, 6'h26, 6'h2a, 6'h2b, 6'h00: op_class = C_RALU;
               6'h08: op_class = C_JR;
               6'h18, 6'h1a: op_class = C_MULDIV;
               6'h10, 6'h12: op_class = C_MF;
               default: op_class = C_ILLEGAL;
             endcase
      6'h23, 6'h2b: op_class = C_LOADSTORE;
      6'h04, 6'h05: op_class = C_BRANCH;
      6'h02, 6'h03: op_class = C_JUMP;
      6'h09, 6'h0b, 6'h0d, 6'h0e, 6'h0f: op_class = C_IALU;
      default: op_class = C_ILLEGAL;
    endcase
  end

  // ---------------------------------------------------------------- control signals
  // bit: 0 fetch, 1 decode, 2 memaddr, 3 mdr_write, 4 wb_rt_mdr, 5 mem_write, 6 alu_r, 7 wb_rd_alu, 8 branch,
  //      9 jump, 10 alu_i, 11 wb_rt_alu, 12 jr, 13 muldiv, 14 mf_wb, 15 retire, 16 stop
  localparam integer CTRL_BITS = 17;
  wire [CTRL_BITS-1:0] ctrl;
  wire [3:0] next_state;

  generate
    if (MICROCODE != 0) begin : microprogrammed
      // control store: {sequencing (2 bits), control signals}; sequencing 0 next, 1 fetch, 2 dispatch 1, 3 dispatch 2
      localparam [1:0] SEQ_NEXT = 2'd0, SEQ_FETCH = 2'd1, SEQ_DISPATCH1 = 2'd2, SEQ_DISPATCH2 = 2'd3;
      reg [CTRL_BITS+1:0] control_store [0:15];
      reg [3:0] dispatch1 [0:8];
      initial begin
        control_store[S_FETCH]    = {SEQ_NEXT,      17'b0_0000_0000_0000_0001};
        control_store[S_DECODE]   = {SEQ_DISPATCH1, 17'b0_0000_0000_0000_0010};
        control_store[S_MEMADDR]  = {SEQ_DISPATCH2, 17'b0_0000_0000_0000_0100};
        control_store[S_MEMREAD]  = {SEQ_NEXT,      17'b0_0000_0000_0000_1000};
        control_store[S_LOADWB]   = {SEQ_FETCH,     17'b0_1000_0000_0001_0000};
        control_store[S_MEMWRITE] = {SEQ_FETCH,     17'b0_1000_0000_0010_0000};
        control_store[S_REXEC]    = {SEQ_NEXT,      17'b0_0000_0000_0100_0000};
        control_store[S_RWB]      = {SEQ_FETCH,     17'b0_1000_0000_1000_0000};
        control_store[S_BRANCH]   = {SEQ_FETCH,     17'b0_1000_0001_0000_0000};
        control_store[S_JUMP]     = {SEQ_FETCH,     17'b0_1000_0010_0000_0000};
        control_store[S_IEXEC]    = {SEQ_NEXT,      17'b0_0000_0100_0000_0000};
        control_store[S_IWB]      = {SEQ_FETCH,     17'b0_1000_1000_0000_0000};
        control_store[S_JR]       = {SEQ_FETCH,     17'b0_1001_0000_0000_0000};
        control_store[S_MULDIV]   = {SEQ_FETCH,     17'b0_1010_0000_0000_0000};
        control_store[S_MFWB]     = {SEQ_FETCH,     17'b0_1100_0000_0000_0000};
        control_store[S_STOP]     = {SEQ_FETCH,     17'b1_0000_0000_0000_0000};
        dispatch1[C_RALU]      = S_REXEC;
        dispatch1[C_JR]        = S_JR;
        dispatch1[C_MULDIV]    = S_MULDIV;
        dispatch1[C_MF]        = S_MFWB;
        dispatch1[C_LOADSTORE] = S_MEMADDR;
        dispatch1[C_BRANCH]    = S_BRANCH;
        dispatch1[C_JUMP]      = S_JUMP;
        dispatch1[C_IALU]      = S_IEXEC;
        dispatch1[C_ILLEGAL]   = S_STOP;
      end
      wire [CTRL_BITS+1:0] word = control_store[state];
      assign ctrl = word[CTRL_BITS-1:0];
      assign next_state = word[CTRL_BITS+1:CTRL_BITS] == SEQ_NEXT      ? state + 4'd1
                        : word[CTRL_BITS+1:CTRL_BITS] == SEQ_FETCH     ? S_FETCH
                        : word[CTRL_BITS+1:CTRL_BITS] == SEQ_DISPATCH1 ? dispatch1[op_class]
                        : (opcode == 6'h23 ? S_MEMREAD : S_MEMWRITE);  // dispatch 2: load or store
    end else begin : hardwired
      reg [CTRL_BITS-1:0] c;
      reg [3:0] n;
      always @(*) begin
        c = {CTRL_BITS{1'b0}};
        n = S_FETCH;
        case (state)
          S_FETCH:    begin c[0] = 1'b1; n = S_DECODE; end
          S_DECODE: begin
            c[1] = 1'b1;
            case (op_class)
              C_RALU:      n = S_REXEC;
              C_JR:        n = S_JR;
              C_MULDIV:    n = S_MULDIV;
              C_MF:        n = S_MFWB;
              C_LOADSTORE: n = S_MEMADDR;
              C_BRANCH:    n = S_BRANCH;
              C_JUMP:      n = S_JUMP;
              C_IALU:      n = S_IEXEC;
              default:     n = S_STOP;
            endcase
          end
          S_MEMADDR:  begin c[2] = 1'b1; n = opcode == 6'h23 ? S_MEMREAD : S_MEMWRITE; end
          S_MEMREAD:  begin c[3] = 1'b1; n = S_LOADWB; end
          S_LOADWB:   begin c[4] = 1'b1; c[15] = 1'b1; end
          S_MEMWRITE: begin c[5] = 1'b1; c[15] = 1'b1; end
          S_REXEC:    begin c[6] = 1'b1; n = S_RWB; end
          S_RWB:      begin c[7] = 1'b1; c[15] = 1'b1; end
          S_BRANCH:   begin c[8] = 1'b1; c[15] = 1'b1; end
          S_JUMP:     begin c[9] = 1'b1; c[15] = 1'b1; end
          S_IEXEC:    begin c[10] = 1'b1; n = S_IWB; end
          S_IWB:      begin c[11] = 1'b1; c[15] = 1'b1; end
          S_JR:       begin c[12] = 1'b1; c[15] = 1'b1; end
          S_MULDIV:   begin c[13] = 1'b1; c[15] = 1'b1; end
          S_MFWB:     begin c[14] = 1'b1; c[15] = 1'b1; end
          default:    begin c[16] = 1'b1; end
        endcase
      end
      assign ctrl = c;
      assign next_state = n;
    end
  endgenerate

  // ---------------------------------------------------------------- ALU control
  reg [31:0] alu_r;
  always @(*) begin
    case (funct)
      6'h22:   alu_r = a - b;
      6'h24:   alu_r = a & b;
      6'h25:   alu_r = a | b;
      6'h26:   alu_r = a ^ b;
      6'h2a:   alu_r = ($signed(a) < $signed(b)) ? 32'd1 : 32'd0;
      6'h2b:   alu_r = (a < b) ? 32'd1 : 32'd0;
      6'h00:   alu_r = b << shamt;
      default: alu_r = a + b;  // add, addu
    endcase
  end
  reg [31:0] alu_i;
  always @(*) begin
    case (opcode)
      6'h0b:   alu_i = (a < sext) ? 32'd1 : 32'd0;  // sltiu
      6'h0d:   alu_i = a | zext;                    // ori
      6'h0e:   alu_i = a ^ zext;                    // xori
      6'h0f:   alu_i = {ir[15:0], 16'd0};           // lui
      default: alu_i = a + sext;                    // addiu
    endcase
  end
  wire signed [63:0] product = $signed(a) * $signed(b);
  wire [31:0] word_index = {2'b00, alu_out[31:2]};

  // ---------------------------------------------------------------- datapath
  integer k;
  always @(posedge clk) begin
    if (reset) begin
      pc <= entry_pc;
      state <= S_FETCH;
      halted <= 1'b0;
      retired <= 32'd0;
      cycles <= 32'd0;
      hi <= 32'd0;
      lo <= 32'd0;
      for (k = 0; k < 32; k = k + 1) regs[k] <= 32'd0;
      regs[29] <= (DMEM_WORDS - 16) * 4;  // $sp
      regs[31] <= HALT_ADDRESS;           // $ra
    end else if (!halted) begin
      cycles <= cycles + 32'd1;
      state <= next_state;
      if (ctrl[15]) retired <= retired + 32'd1;
      if (ctrl[0]) begin
        ir <= imem[pc];
        pc <= pc + 32'd1;
      end
      if (ctrl[1]) begin
        a <= regs[rs];
        b <= regs[rt];
        alu_out <= pc + sext;
      end
      if (ctrl[2]) alu_out <= a + sext;
      if (ctrl[3]) mdr <= (word_index < DMEM_WORDS) ? dmem[word_index] : 32'd0;
      if (ctrl[4] && rt != 5'd0) regs[rt] <= mdr;
      if (ctrl[5] && word_index < DMEM_WORDS) dmem[word_index] <= b;
      if (ctrl[6]) alu_out <= alu_r;
      if (ctrl[7] && rd != 5'd0) regs[rd] <= alu_out;
      if (ctrl[8] && ((opcode == 6'h04) == (a == b))) pc <= alu_out;
      if (ctrl[9]) begin
        pc <= {6'd0, ir[25:0]};
        if (opcode == 6'h03) regs[31] <= pc;
      end
      if (ctrl[10]) alu_out <= alu_i;
      if (ctrl[11] && rt != 5'd0) regs[rt] <= alu_out;
      if (ctrl[12]) begin
        if (a == HALT_ADDRESS) halted <= 1'b1;
        else pc <= a;
      end
      if (ctrl[13]) begin
        if (funct == 6'h18) begin
          hi <= product[63:32];
          lo <= product[31:0];
        end else if (b != 32'd0) begin
          lo <= $signed(a) / $signed(b);
          hi <= $signed(a) % $signed(b);
        end
      end
      if (ctrl[14] && rd != 5'd0) regs[rd] <= (funct == 6'h10) ? hi : lo;
      if (ctrl[16]) halted <= 1'b1;
    end
  end
endmodule
