#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "nexus/compiler/passes/modulo_schedule.hpp"

namespace ms = nexus::compiler::passes;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

ms::Ddg load(const std::string& path) {
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  auto parsed = ms::parse_ddg(buffer.str());
  if (!parsed.ddg) {
    std::cerr << path << ": " << parsed.error << '\n';
    std::exit(1);
  }
  return *parsed.ddg;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string repo = argc > 1 ? argv[1] : ".";
  const auto daxpy = load(repo + "/examples/schedules/daxpy.ddg");
  const auto d = ms::modulo_schedule(daxpy);
  expect(d.res_mii == 3 && d.rec_mii == 1 && d.ii == 3, "DAXPY: three memory operations on one port give II = ResMII = 3");
  expect(d.success && ms::verify_schedule(daxpy, d).empty(), "DAXPY schedule meets every constraint");
  expect(d.stages >= 2, "DAXPY overlaps iterations (more than one stage)");

  const auto dot = load(repo + "/examples/schedules/dot_product.ddg");
  const auto p = ms::modulo_schedule(dot);
  expect(p.rec_mii == 4 && p.ii == 4, "dot product: the add recurrence gives II = RecMII = 4");
  expect(p.success && ms::verify_schedule(dot, p).empty(), "dot product schedule meets every constraint");

  expect(!ms::parse_ddg("resources alu 1\nop a alu\nedge a -> a\n").ddg.has_value(), "a zero-distance self loop is rejected");

  // Random loop bodies: every schedule must be valid, II >= MII, and almost always II = MII.
  std::mt19937 rng(13);
  int at_mii = 0;
  const int rounds = 400;
  for (int round = 0; round < rounds; ++round) {
    std::ostringstream text;
    const int alus = 1 + static_cast<int>(rng() % 2);
    const int mems = 1 + static_cast<int>(rng() % 2);
    text << "resources alu " << alus << ", mem " << mems << ", mul 1\n";
    const int n = 4 + static_cast<int>(rng() % 8);
    const char* kinds[] = {"alu", "mem", "mul"};
    for (int v = 0; v < n; ++v) text << "op o" << v << ' ' << kinds[rng() % 3] << " latency " << 1 + rng() % 4 << '\n';
    for (int v = 1; v < n; ++v) {
      text << "edge o" << rng() % v << " -> o" << v << '\n';  // forward, same iteration
    }
    const int back = static_cast<int>(rng() % 3);
    for (int k = 0; k < back; ++k) {
      const int from = static_cast<int>(rng() % n);
      const int to = static_cast<int>(rng() % (from + 1));
      text << "edge o" << from << " -> o" << to << " distance " << 1 + rng() % 2 << '\n';  // loop-carried
    }
    const auto parsed = ms::parse_ddg(text.str());
    expect(parsed.ddg.has_value(), "random DDG parses: " + parsed.error);
    if (!parsed.ddg) continue;
    const auto s = ms::modulo_schedule(*parsed.ddg);
    expect(s.success, "random DDG is scheduled");
    if (!s.success) continue;
    expect(ms::verify_schedule(*parsed.ddg, s).empty(), "random schedule valid: " + ms::verify_schedule(*parsed.ddg, s));
    expect(s.ii >= s.mii, "II never below MII");
    at_mii += s.ii == s.mii ? 1 : 0;
  }
  expect(at_mii * 100 >= rounds * 90, "II = MII for at least 90% of random loops (got " + std::to_string(at_mii) + "/" +
                                          std::to_string(rounds) + ")");
  if (failures == 0) {
    std::cout << "modulo scheduling: DAXPY II=3, dot product II=4, " << at_mii << "/" << rounds
              << " random loops at II = MII, all schedules valid\n";
  }
  return failures == 0 ? 0 : 1;
}
