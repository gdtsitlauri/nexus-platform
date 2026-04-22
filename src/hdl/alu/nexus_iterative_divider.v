module nexus_iterative_divider(
    input  wire        clk,
    input  wire        reset,
    input  wire        start,
    input  wire        signed_mode,
    input  wire [31:0] dividend,
    input  wire [31:0] divisor,
    output reg         busy,
    output reg         done,
    output reg         divide_by_zero,
    output reg  [31:0] quotient,
    output reg  [31:0] remainder
);
  reg [31:0] divisor_work;
  reg [31:0] quotient_work;
  reg [32:0] remainder_work;
  reg [5:0]  count;
  reg        negate_quotient;
  reg        negate_remainder;

  wire [31:0] dividend_abs =
      (signed_mode && dividend[31]) ? (~dividend + 32'd1) : dividend;
  wire [31:0] divisor_abs =
      (signed_mode && divisor[31]) ? (~divisor + 32'd1) : divisor;

  always @(posedge clk or posedge reset) begin
    if (reset) begin
      divisor_work <= 32'd0;
      quotient_work <= 32'd0;
      remainder_work <= 33'd0;
      count <= 6'd0;
      negate_quotient <= 1'b0;
      negate_remainder <= 1'b0;
      busy <= 1'b0;
      done <= 1'b0;
      divide_by_zero <= 1'b0;
      quotient <= 32'd0;
      remainder <= 32'd0;
    end else if (start && !busy) begin
      if (divisor == 32'd0) begin
        busy <= 1'b0;
        done <= 1'b1;
        divide_by_zero <= 1'b1;
        quotient <= 32'd0;
        remainder <= dividend;
      end else begin
        divisor_work <= divisor_abs;
        quotient_work <= dividend_abs;
        remainder_work <= 33'd0;
        count <= 6'd0;
        negate_quotient <= signed_mode && (dividend[31] ^ divisor[31]);
        negate_remainder <= signed_mode && dividend[31];
        busy <= 1'b1;
        done <= 1'b0;
        divide_by_zero <= 1'b0;
        quotient <= 32'd0;
        remainder <= 32'd0;
      end
    end else if (busy) begin
      reg [32:0] trial_remainder;
      reg [31:0] trial_quotient;

      trial_remainder = {remainder_work[31:0], quotient_work[31]};
      trial_quotient = {quotient_work[30:0], 1'b0};

      if (trial_remainder >= {1'b0, divisor_work}) begin
        trial_remainder = trial_remainder - {1'b0, divisor_work};
        trial_quotient[0] = 1'b1;
      end

      remainder_work <= trial_remainder;
      quotient_work <= trial_quotient;

      if (count == 6'd31) begin
        busy <= 1'b0;
        done <= 1'b1;
        quotient <= negate_quotient ? (~trial_quotient + 32'd1) : trial_quotient;
        remainder <= negate_remainder ? (~trial_remainder[31:0] + 32'd1) : trial_remainder[31:0];
      end else begin
        count <= count + 6'd1;
        done <= 1'b0;
      end
    end else begin
      done <= 1'b0;
      divide_by_zero <= 1'b0;
    end
  end
endmodule
