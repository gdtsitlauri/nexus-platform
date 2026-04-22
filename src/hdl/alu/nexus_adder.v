module nexus_adder(
    input  wire [31:0] a,
    input  wire [31:0] b,
    input  wire        cin,
    output wire [31:0] sum,
    output wire        cout,
    output wire        overflow
);
  wire [32:0] wide_sum;

  assign wide_sum = {1'b0, a} + {1'b0, b} + {32'd0, cin};
  assign sum = wide_sum[31:0];
  assign cout = wide_sum[32];
  assign overflow = (a[31] == b[31]) && (sum[31] != a[31]);
endmodule
