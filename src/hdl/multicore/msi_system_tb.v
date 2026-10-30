// Testbench for nexus_msi_system: four cores send random reads and writes to a few shared addresses.
// Checks, every cycle:
//   - coherence: every read returns the value of the last write to that address (golden memory, updated in the
//     order the system answers);
//   - the MSI invariant: a line is Modified in at most one cache, and then in no other cache at all;
// and at the end that memory plus the dirty lines equal the golden memory.
`timescale 1ns/1ps
module msi_system_tb;
  localparam integer N = 4;
  localparam integer LINES = 4;
  localparam integer AW = 6;
  localparam integer REQUESTS = 400;    // per core
  localparam integer SHARED = 12;       // addresses actually used: lots of sharing and conflicts

  reg clk = 0;
  reg rst = 1;
  reg  [N-1:0]    req_valid = 0;
  reg  [N-1:0]    req_write = 0;
  reg  [N*AW-1:0] req_addr = 0;
  reg  [N*32-1:0] req_wdata = 0;
  wire [N-1:0]    resp_valid;
  wire [N*32-1:0] resp_rdata;
  wire [31:0] hits, misses, invalidations, flushes, writebacks;

  nexus_msi_system #(.N(N), .LINES(LINES), .AW(AW)) dut(
      .clk(clk), .rst(rst), .req_valid(req_valid), .req_write(req_write), .req_addr(req_addr),
      .req_wdata(req_wdata), .resp_valid(resp_valid), .resp_rdata(resp_rdata), .hits(hits), .misses(misses),
      .invalidations(invalidations), .flushes(flushes), .writebacks(writebacks));

  always #5 clk = ~clk;

  reg [31:0] golden [0:(1<<AW)-1];
  integer done [0:N-1];
  integer errors = 0;
  integer seed = 704;
  integer c, k, line, owners, sharers, cycles, finished;
  reg [AW-1:0] a;

  task automatic new_request(input integer core);
    begin
      a = ($unsigned($random(seed)) % SHARED) * 3 % (1 << AW);   // spread over sets and tags
      req_addr[core*AW +: AW] = a;
      req_write[core] = ($unsigned($random(seed)) % 10) < 4;
      req_wdata[core*32 +: 32] = {core[7:0], 24'(done[core])};
      req_valid[core] = 1'b1;
    end
  endtask

  initial begin
    for (k = 0; k < (1 << AW); k = k + 1) golden[k] = 0;
    for (c = 0; c < N; c = c + 1) done[c] = 0;
    @(negedge clk);
    rst = 0;
    for (c = 0; c < N; c = c + 1) new_request(c);
    cycles = 0;
    finished = 0;
    while (finished < N && cycles < 100000) begin
      @(negedge clk);
      cycles = cycles + 1;
      // the answers of this cycle (no two can conflict on one address: a write needs the only copy)
      for (c = 0; c < N; c = c + 1) begin
        if (req_valid[c] && resp_valid[c]) begin
          a = req_addr[c*AW +: AW];
          if (req_write[c]) begin
            golden[a] = req_wdata[c*32 +: 32];
          end else if (resp_rdata[c*32 +: 32] !== golden[a]) begin
            $display("FAIL: core %0d read %h at address %0d, expected %h", c, resp_rdata[c*32 +: 32], a, golden[a]);
            errors = errors + 1;
          end
          done[c] = done[c] + 1;
          if (done[c] == REQUESTS) begin
            req_valid[c] = 1'b0;
            finished = finished + 1;
          end else begin
            new_request(c);
          end
        end
      end
      // the MSI invariant on every address
      for (k = 0; k < (1 << AW); k = k + 1) begin
        owners = 0;
        sharers = 0;
        line = k % LINES;
        for (c = 0; c < N; c = c + 1) begin
          if (dut.tag[c*LINES + line] == k / LINES) begin
            if (dut.state[c*LINES + line] == 2) owners = owners + 1;
            if (dut.state[c*LINES + line] == 1) sharers = sharers + 1;
          end
        end
        if (owners > 1 || (owners == 1 && sharers > 0)) begin
          $display("FAIL: address %0d has %0d Modified and %0d Shared copies at cycle %0d", k, owners, sharers, cycles);
          errors = errors + 1;
        end
      end
    end
    // memory plus the dirty lines must hold the golden values
    for (k = 0; k < (1 << AW); k = k + 1) begin
      line = k % LINES;
      a = k;
      owners = -1;
      for (c = 0; c < N; c = c + 1) begin
        if (dut.state[c*LINES + line] == 2 && dut.tag[c*LINES + line] == k / LINES) owners = c;
      end
      if ((owners >= 0 ? dut.data[owners*LINES + line] : dut.mem[k]) !== golden[k]) begin
        $display("FAIL: final value of address %0d differs from the golden memory", k);
        errors = errors + 1;
      end
    end
    $display("%0d cores x %0d requests in %0d cycles: %0d hits, %0d bus transactions, %0d invalidations, %0d flushes, %0d writebacks",
             N, REQUESTS, cycles, hits, misses, invalidations, flushes, writebacks);
    if (finished == N && errors == 0) $display("PASS");
    else $display("FAIL: %0d error(s), %0d core(s) finished", errors, finished);
    $finish;
  end
endmodule
