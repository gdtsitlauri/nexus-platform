#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nexus/mips/loader/parser.hpp"

namespace nexus::sim::simt {

// A GPU-style SIMT core executing MIPS kernels.
//
//   * `threads` threads run the kernel (label `kernel`, falling back to `main`) with
//     $a0 = global thread id and $a1 = thread count; each returns a value in $v0.
//   * Threads are grouped into warps of `warp_size` lanes that issue one instruction per cycle
//     in lock step; warps are scheduled round-robin.
//   * Branch divergence uses a SIMT reconvergence stack: on a divergent branch both paths run
//     with partial masks and the warp reconverges at the branch's immediate post-dominator.
//   * Memory: addresses below the stack region are global and shared; each thread's stack is
//     private "local memory", interleaved across threads exactly like CUDA local memory, so that
//     identical stack offsets from a warp coalesce.  Each warp-level load/store is split into
//     128-byte transactions to measure coalescing.
//   * Thread blocks (block_size > 0): threads are grouped into blocks; warps never cross a block;
//     $a2 = block id and $a3 = thread index inside the block.  Bytes [kSharedBase, kSharedBase + 16 KiB)
//     are the block's shared memory (32 banks of 4-byte words: a warp access costs one extra cycle per
//     additional distinct word in the busiest bank), and a store to kBarrierAddress is __syncthreads():
//     the warp waits until every warp of its block has reached the barrier or finished.
inline constexpr std::int32_t kSharedBase = 0x44000;
inline constexpr std::int32_t kBarrierAddress = 0x48000;

struct RunOptions {
  std::size_t threads = 64;
  std::size_t warp_size = 32;
  std::size_t block_size = 0;  // 0: one block holding every thread
  std::size_t max_warp_instructions = 2'000'000;
  bool trace = false;
};

struct RunResult {
  bool success = false;
  std::string error;
  std::vector<std::int32_t> thread_results;
  std::size_t warps = 0;
  std::size_t cycles = 0;               // warp-instruction issue slots
  std::size_t warp_instructions = 0;
  std::size_t thread_instructions = 0;  // sum over active lanes
  std::size_t divergent_branches = 0;
  std::size_t uniform_branches = 0;
  std::size_t max_stack_depth = 0;
  std::size_t memory_requests = 0;      // warp-level loads/stores
  std::size_t memory_transactions = 0;  // 128-byte segments touched
  std::size_t global_requests = 0;
  std::size_t local_requests = 0;
  std::size_t shared_requests = 0;
  std::size_t bank_conflict_cycles = 0;
  std::size_t barriers = 0;            // barrier releases
  std::size_t barrier_wait_cycles = 0;  // issue slots in which a warp waited at a barrier
  std::vector<std::string> trace_lines;

  [[nodiscard]] double simd_efficiency(std::size_t warp_size) const {
    return warp_instructions == 0 ? 0.0
                                  : static_cast<double>(thread_instructions) /
                                        static_cast<double>(warp_instructions * warp_size);
  }
};

RunResult run_kernel(const mips::loader::LoadedProgram& program, const RunOptions& options = {});

}  // namespace nexus::sim::simt
