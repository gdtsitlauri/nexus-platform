module nexus_pipeline_reg #(
    parameter WIDTH = 32
) (
    input  wire             clk,
    input  wire             reset,
    input  wire             enable,
    input  wire             flush,
    input  wire [WIDTH-1:0] d,
    output reg  [WIDTH-1:0] q,
    output reg              valid
);
  always @(posedge clk or posedge reset) begin
    if (reset) begin
      q <= {WIDTH{1'b0}};
      valid <= 1'b0;
    end else if (flush) begin
      q <= {WIDTH{1'b0}};
      valid <= 1'b0;
    end else if (enable) begin
      q <= d;
      valid <= 1'b1;
    end
  end
endmodule
