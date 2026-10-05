#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "nexus/compiler/backend_mips/codegen.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"
#include "nexus/mips/assembler_support/printer.hpp"
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
  std::ifstream input(path);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

nexus::mips::loader::LoadedProgram assemble(const std::string& text) {
  auto loaded = nexus::mips::loader::load_program_from_text(text);
  if (!loaded.diagnostics.empty()) {
    std::cerr << loaded.diagnostics.front() << '\n';
    std::exit(1);
  }
  return *loaded.program;
}

nexus::mips::loader::LoadedProgram compile(const std::string& source, bool allocate) {
  auto parsed = nexus::compiler::frontend::parse_source(source);
  if (!parsed.diagnostics.empty() || !nexus::compiler::semantics::analyze_program(*parsed.program).diagnostics.empty()) {
    std::cerr << "kernel does not compile\n";
    std::exit(1);
  }
  const auto lowered = nexus::compiler::ir::lower_program(*parsed.program);
  nexus::compiler::backend_mips::CodegenOptions options;
  if (allocate) {
    options.register_allocation = nexus::compiler::backend_mips::RegisterAllocationMode::LinearScan;
  }
  const auto code = nexus::compiler::backend_mips::lower_module(*lowered.module, options);
  return assemble(nexus::mips::assembler_support::print_program(*code.program));
}

int collatz_steps(int n) {
  int count = 0;
  while (n != 1) {
    n = n % 2 == 0 ? n / 2 : 3 * n + 1;
    ++count;
  }
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: simt_test <repo-root>\n";
    return 1;
  }
  const std::string repo = argv[1];

  const auto coalesced = simt::run_kernel(assemble(read(repo + "/examples/gpu/coalesced.s")), {.threads = 64});
  const auto strided = simt::run_kernel(assemble(read(repo + "/examples/gpu/strided.s")), {.threads = 64});
  expect(coalesced.success && strided.success, "memory kernels run");
  expect(coalesced.memory_transactions == coalesced.memory_requests, "unit-stride accesses: one transaction per request");
  expect(strided.memory_transactions == 32 * strided.memory_requests, "128-byte stride: 32 transactions per request");
  for (std::size_t thread = 0; thread < 64; ++thread) {
    expect(coalesced.thread_results[thread] == static_cast<int>(thread) + 100, "coalesced kernel results");
  }

  for (const bool allocate : {false, true}) {
    const auto collatz = compile(read(repo + "/examples/gpu/collatz.nx"), allocate);
    const auto warp32 = simt::run_kernel(collatz, {.threads = 64, .warp_size = 32});
    const auto warp1 = simt::run_kernel(collatz, {.threads = 64, .warp_size = 1});
    expect(warp32.success && warp1.success, "collatz kernel runs");
    for (int thread = 0; thread < 64; ++thread) {
      const int expected = thread % 2 == 0 ? collatz_steps(thread + 1) : thread * 3 + 64;
      if (warp32.thread_results[static_cast<std::size_t>(thread)] != expected ||
          warp1.thread_results[static_cast<std::size_t>(thread)] != expected) {
        expect(false, "collatz result of thread " + std::to_string(thread));
        break;
      }
    }
    expect(warp32.divergent_branches > 0 && warp32.simd_efficiency(32) < 0.5, "collatz diverges heavily");
    expect(warp32.cycles < warp1.cycles, "SIMT issue is cheaper than one thread per issue slot");

    const auto uniform = simt::run_kernel(compile(read(repo + "/examples/gpu/uniform.nx"), allocate), {.threads = 96});
    expect(uniform.success && uniform.divergent_branches == 0 && uniform.simd_efficiency(32) == 1.0,
           "a uniform kernel keeps 100% SIMD efficiency");
    expect(uniform.memory_transactions == uniform.memory_requests, "interleaved local memory coalesces stack traffic");
    for (int thread = 0; thread < 96; ++thread) {
      if (uniform.thread_results[static_cast<std::size_t>(thread)] != thread * 28 + 96) {
        expect(false, "uniform kernel result of thread " + std::to_string(thread));
        break;
      }
    }
  }

  if (failures == 0) {
    std::cout << "simt_test: all checks passed\n";
  }
  return failures == 0 ? 0 : 1;
}
