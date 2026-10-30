// A small shared-memory multiprocessor memory system (NEY704 (e)): N cores, each with a private direct-mapped
// cache, kept coherent by the MSI snooping protocol on one shared bus.
//
//   core i --req--> cache i --+
//   core j --req--> cache j --+-- shared bus (round-robin arbiter, one transaction per cycle) -- memory
//
// Each line holds one 32-bit word.  A request that hits with enough permission (read: S or M; write: M) is served
// by the cache in one cycle.  Otherwise the cache asks for the bus; the granted cache performs one atomic bus
// transaction while every other cache snoops it:
//   BusRd  (read miss):           an M copy elsewhere is flushed to memory and drops to S
//   BusRdX (write miss / upgrade): an M copy elsewhere is flushed; every other copy is invalidated
// A dirty (M) victim is written back to memory first.  Requests stay valid until resp_valid is seen.  The event
// counters are updated with blocking assignments because several caches can count in the same cycle.
module nexus_msi_system #(
    parameter integer N = 4,
    parameter integer LINES = 4,   // per cache, direct mapped
    parameter integer AW = 6       // word address width
) (
    input  wire              clk,
    input  wire              rst,
    input  wire [N-1:0]      req_valid,
    input  wire [N-1:0]      req_write,
    input  wire [N*AW-1:0]   req_addr,
    input  wire [N*32-1:0]   req_wdata,
    output reg  [N-1:0]      resp_valid,
    output reg  [N*32-1:0]   resp_rdata,
    output reg  [31:0]       hits,
    output reg  [31:0]       misses,
    output reg  [31:0]       invalidations,
    output reg  [31:0]       flushes,
    output reg  [31:0]       writebacks
);
  localparam integer IW = $clog2(LINES);
  localparam integer TW = AW - IW;
  localparam [1:0] I = 2'd0, S = 2'd1, M = 2'd2;

  reg [1:0]    state [0:N*LINES-1];
  reg [TW-1:0] tag   [0:N*LINES-1];
  reg [31:0]   data  [0:N*LINES-1];
  reg [31:0]   mem   [0:(1<<AW)-1];

  integer c, k, g, slot, other, start;
  reg [AW-1:0] addr;
  reg [IW-1:0] index;
  reg [TW-1:0] t;
  reg          write, hit;
  reg [31:0]   value;

  function automatic [AW-1:0] addr_of(input integer core);
    addr_of = req_addr[core*AW +: AW];
  endfunction

  // Does core `core` hit with enough permission for its current request?
  function automatic permitted(input integer core);
    integer s;
    reg [AW-1:0] a;
    begin
      a = addr_of(core);
      s = core * LINES + a[IW-1:0];
      permitted = state[s] != I && tag[s] == a[AW-1:IW] && (!req_write[core] || state[s] == M);
    end
  endfunction

  always @(posedge clk) begin
    if (rst) begin
      for (k = 0; k < N * LINES; k = k + 1) begin
        state[k] = I;
        tag[k] = 0;
        data[k] = 0;
      end
      for (k = 0; k < (1 << AW); k = k + 1) mem[k] = 0;
      resp_valid <= 0;
      resp_rdata <= 0;
      hits = 0;
      misses = 0;
      invalidations = 0;
      flushes = 0;
      writebacks = 0;
      start = 0;
    end else begin
      resp_valid <= 0;

      // 1. Arbitration: the first core from `start` (round robin) that needs the bus.
      g = -1;
      for (k = 0; k < N; k = k + 1) begin
        c = (start + k) % N;
        if (g < 0 && req_valid[c] && !permitted(c)) g = c;
      end

      // 2. The granted bus transaction, snooped by every other cache.
      if (g >= 0) begin
        addr = addr_of(g);
        index = addr[IW-1:0];
        t = addr[AW-1:IW];
        write = req_write[g];
        slot = g * LINES + index;
        if (state[slot] == M && tag[slot] != t) begin          // dirty victim
          mem[{tag[slot], index}] = data[slot];
          writebacks = writebacks + 1;
        end
        for (c = 0; c < N; c = c + 1) begin
          other = c * LINES + index;
          if (c != g && state[other] != I && tag[other] == t) begin
            if (state[other] == M) begin                       // the owner supplies the data
              mem[addr] = data[other];
              flushes = flushes + 1;
            end
            if (write) begin
              state[other] = I;
              invalidations = invalidations + 1;
            end else begin
              state[other] = S;
            end
          end
        end
        value = mem[addr];
        tag[slot] = t;
        if (write) begin
          state[slot] = M;
          data[slot] = req_wdata[g*32 +: 32];
        end else begin
          state[slot] = S;
          data[slot] = value;
        end
        resp_rdata[g*32 +: 32] <= write ? 32'd0 : value;
        resp_valid[g] <= 1'b1;
        misses = misses + 1;
        start = (g + 1) % N;
      end

      // 3. Hits, after the transaction (so a copy it just invalidated misses instead).
      for (c = 0; c < N; c = c + 1) begin
        if (c != g && req_valid[c] && permitted(c)) begin
          addr = addr_of(c);
          slot = c * LINES + addr[IW-1:0];
          if (req_write[c]) data[slot] = req_wdata[c*32 +: 32];
          resp_rdata[c*32 +: 32] <= req_write[c] ? 32'd0 : data[slot];
          resp_valid[c] <= 1'b1;
          hits = hits + 1;
        end
      end
    end
  end
endmodule
