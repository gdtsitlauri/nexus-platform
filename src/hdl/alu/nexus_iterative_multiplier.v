module nexus_iterative_multiplier(
    input  wire        clk,
    input  wire        reset,
    input  wire        start,
    input  wire        signed_mode,
    input  wire [31:0] multiplicand,
    input  wire [31:0] multiplier,
    output reg         busy,
    output reg         done,
    output reg  [63:0] product
);
  reg [63:0] acc;
  reg [63:0] multiplicand_work;
  reg [31:0] multiplier_work;
  reg [5:0]  count;
  reg        negate_result;

  wire [31:0] multiplicand_abs =
      (signed_mode && multiplicand[31]) ? (~multiplicand + 32'd1) : multiplicand;
  wire [31:0] multiplier_abs =
      (signed_mode && multiplier[31]) ? (~multiplier + 32'd1) : multiplier;

  always @(posedge clk or posedge reset) begin
    if (reset) begin
      acc <= 64'd0;
      multiplicand_work <= 64'd0;
      multiplier_work <= 32'd0;
      count <= 6'd0;
      negate_result <= 1'b0;
      busy <= 1'b0;
      done <= 1'b0;
      product <= 64'd0;
    end else if (start && !busy) begin
      acc <= 64'd0;
      multiplicand_work <= {32'd0, multiplicand_abs};
      multiplier_work <= multiplier_abs;
      count <= 6'd0;
      negate_result <= signed_mode && (multiplicand[31] ^ multiplier[31]);
      busy <= 1'b1;
      done <= 1'b0;
      product <= 64'd0;
    end else if (busy) begin
      reg [63:0] next_acc;
      next_acc = acc;
      if (multiplier_work[0]) begin
        next_acc = acc + multiplicand_work;
      end

      acc <= next_acc;
      multiplicand_work <= multiplicand_work << 1;
      multiplier_work <= multiplier_work >> 1;

      if (count == 6'd31) begin
        busy <= 1'b0;
        done <= 1'b1;
        product <= negate_result ? (~next_acc + 64'd1) : next_acc;
      end else begin
        count <= count + 6'd1;
        done <= 1'b0;
      end
    end else begin
      done <= 1'b0;
    end
  end
endmodule
