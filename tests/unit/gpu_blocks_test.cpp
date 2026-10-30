// Thread blocks, shared memory, bank conflicts and __syncthreads() in the SIMT model (NEY704 (i)).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/simt/model.hpp"

namespace simt = nexus::sim::simt;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string read(const std::string& path) {
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

simt::RunResult run(const std::string& text, std::size_t threads, std::size_t warp, std::size_t block) {
  const auto loaded = nexus::mips::loader::load_program_from_text(text);
  if (!loaded.program) {
    simt::RunResult bad;
    bad.error = "load failed";
    return bad;
  }
  return simt::run_kernel(*loaded.program, {.threads = threads, .warp_size = warp, .block_size = block});
}

}  // namespace

int main(int argc, char** argv) {
  const std::string repo = argc > 1 ? argv[1] : ".";
  const auto reduce = read(repo + "/examples/gpu/block_reduce.s");
  for (const std::size_t warp : {std::size_t{4}, std::size_t{8}, std::size_t{32}}) {
    const auto r = run(reduce, 64, warp, 16);
    expect(r.success, "block reduction runs with warp size " + std::to_string(warp) + ": " + r.error);
    if (!r.success) continue;
    bool sums = true;
    for (std::size_t t = 0; t < 64; ++t) {
      const std::size_t block = t / 16;
      const std::int32_t expected = static_cast<std::int32_t>(16 * 16 * block + 136);  // (16b+1) + ... + (16b+16)
      sums = sums && r.thread_results[t] == expected;
    }
    expect(sums, "every thread returns its block's sum (warp size " + std::to_string(warp) + ")");
    expect(r.barriers == 4 * 5, "five barriers released in each of the four blocks");
    expect(r.global_requests == 0 && r.shared_requests > 0, "the reduction works in shared memory only");
    expect(r.warps == 64 / std::min<std::size_t>(warp, 16), "warps never cross a block");
  }

  const auto conflict = read(repo + "/examples/gpu/bank_conflict.s");
  const auto strided = run(conflict, 64, 8, 64);
  expect(strided.success && strided.bank_conflict_cycles == 8 * 7,
         "stride 32: the 8 lanes of each warp hit one bank, 7 extra cycles per request");
  std::string unit = conflict;
  unit.replace(unit.find("addiu $s1, $zero, 32"), 20, "addiu $s1, $zero, 1 ");
  const auto unit_stride = run(unit, 64, 8, 64);
  expect(unit_stride.success && unit_stride.bank_conflict_cycles == 0, "stride 1: conflict free");
  expect(unit_stride.cycles + 56 == strided.cycles, "bank conflicts cost exactly their extra cycles");

  if (failures == 0) {
    std::cout << "GPU blocks: shared-memory block reduction with __syncthreads() correct for warp sizes 4, 8, 32; "
                 "bank conflicts counted\n";
  }
  return failures == 0 ? 0 : 1;
}
