#include <iostream>
#include <string>
#include <vector>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/advanced/multithreading.hpp"

namespace adv = nexus::sim::advanced;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

adv::MtResult run(const std::vector<const nexus::mips::loader::LoadedProgram*>& programs, adv::MtPolicy policy,
                  std::size_t width = 2, std::size_t miss_penalty = 20) {
  adv::MtOptions options;
  options.policy = policy;
  options.issue_width = width;
  options.miss_penalty = miss_penalty;
  return adv::run_multithreaded(programs, options);
}

}  // namespace

int main(int argc, char** argv) {
  const std::string repo = argc > 1 ? argv[1] : ".";
  const auto stream = nexus::mips::loader::load_program_from_file(repo + "/examples/multithreading/stream.s");
  const auto compute = nexus::mips::loader::load_program_from_file(repo + "/examples/multithreading/compute.s");
  if (!stream.program || !compute.program) {
    std::cerr << "cannot load the example threads\n";
    return 1;
  }
  const auto* s = &*stream.program;
  const auto* c = &*compute.program;

  // One compute-bound thread: every policy takes the same time (one instruction per cycle, no misses).
  const auto single_c = run({c}, adv::MtPolicy::Single);
  expect(single_c.success && single_c.cache_misses == 0, "compute thread runs without misses");
  for (const auto policy : {adv::MtPolicy::Coarse, adv::MtPolicy::Fine}) {
    const auto r = run({c}, policy);
    expect(r.cycles == single_c.cycles && r.issued == single_c.issued,
           adv::policy_name(policy) + " equals single with one thread");
  }
  expect(single_c.cycles == single_c.issued, "one instruction per cycle when nothing stalls");

  // Four memory-bound threads.  Both kinds of multithreading hide memory latency.  Fine-grained interleaving also
  // slows every thread down (it gets one issue slot in four), so it wins when stalls are short and loses to
  // coarse-grained switching when misses are long (Hennessy & Patterson's trade-off).
  const std::vector<const nexus::mips::loader::LoadedProgram*> four{s, s, s, s};
  const auto single = run(four, adv::MtPolicy::Single);
  const auto coarse = run(four, adv::MtPolicy::Coarse);
  const auto fine = run(four, adv::MtPolicy::Fine);
  expect(single.success && coarse.success && fine.success, "four stream threads run");
  expect(single.issued == coarse.issued && coarse.issued == fine.issued, "the same instructions under every policy");
  expect(coarse.cycles < single.cycles && fine.cycles < single.cycles, "multithreading hides memory latency");
  expect(coarse.cycles < fine.cycles, "long misses (20 cycles): coarse-grained beats fine-grained (" +
                                          std::to_string(coarse.cycles) + " vs " + std::to_string(fine.cycles) + ")");
  const auto coarse_short = run(four, adv::MtPolicy::Coarse, 2, 4);
  const auto fine_short = run(four, adv::MtPolicy::Fine, 2, 4);
  expect(fine_short.cycles < coarse_short.cycles, "short misses (4 cycles): fine-grained beats coarse-grained (" +
                                                      std::to_string(fine_short.cycles) + " vs " +
                                                      std::to_string(coarse_short.cycles) + ")");
  expect(coarse.switches > 0 && single.switches == 0, "coarse-grained multithreading switches on misses");
  for (const auto& thread : fine.threads) expect(thread.exit_code == 7, "every stream thread exits with 7");

  // A mix of memory-bound and compute-bound threads: simultaneous multithreading fills more issue slots.
  const std::vector<const nexus::mips::loader::LoadedProgram*> mix{s, c, s, c};
  const auto fine_mix = run(mix, adv::MtPolicy::Fine);
  const auto smt_mix = run(mix, adv::MtPolicy::Smt, 2);
  expect(smt_mix.cycles < fine_mix.cycles, "SMT with two issue slots finishes the mix sooner than fine-grained");
  expect(smt_mix.issued == fine_mix.issued, "SMT issues the same instructions");
  expect(smt_mix.utilization(2) <= 1.0 && fine_mix.utilization(1) <= 1.0, "utilisation never above 100%");

  if (failures == 0) {
    std::cout << "multithreading: 4 memory-bound threads take " << single.cycles << " cycles one after another, "
              << coarse.cycles << " coarse-grained, " << fine.cycles << " fine-grained (with 4-cycle misses: "
              << coarse_short.cycles << " coarse, " << fine_short.cycles << " fine); mix " << fine_mix.cycles
              << " fine vs " << smt_mix.cycles << " SMT-2\n";
  }
  return failures == 0 ? 0 : 1;
}
