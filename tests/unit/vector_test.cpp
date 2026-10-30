#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "nexus/sim/vector/model.hpp"

namespace vec = nexus::sim::vector;

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

vec::Result daxpy(const std::string& text, std::int64_t n, bool chaining = true, std::size_t lanes = 1) {
  vec::Options options;
  options.params["n"] = n;
  options.chaining = chaining;
  options.lanes = lanes;
  return vec::run(text, options);
}

}  // namespace

int main(int argc, char** argv) {
  const std::string repo = argc > 1 ? argv[1] : ".";
  const auto text = read(repo + "/examples/vector/daxpy.vasm");

  // Correctness, including vector lengths that are not multiples of MVL (strip-mining).
  for (const std::int64_t n : {1, 63, 64, 65, 200, 257}) {
    const auto r = daxpy(text, n);
    expect(r.success, "DAXPY runs for n = " + std::to_string(n) + ": " + r.error);
    if (!r.success) continue;
    const auto expected = vec::daxpy_reference(3.0, r.arrays.at("X"), std::vector<double>(static_cast<std::size_t>(n), 2.0));
    expect(r.arrays.at("Y") == expected, "DAXPY result equals the scalar loop for n = " + std::to_string(n));
    const auto strips = static_cast<std::size_t>((n + 63) / 64);
    expect(r.strips == strips && r.elements == 5 * static_cast<std::size_t>(n), "strip count and element operations");
    // With chaining DAXPY is three convoys per strip: (LV, MULVS) (LV, ADDVV) (SV).
    expect(r.convoys == 3 * strips, "three convoys per strip with chaining");
    expect(daxpy(text, n, false).convoys == 4 * strips, "four convoys per strip without chaining");
  }

  // Chaining and lanes shorten the schedule; the results do not change.
  const auto chained = daxpy(text, 256);
  const auto unchained = daxpy(text, 256, false);
  const auto four_lanes = daxpy(text, 256, true, 4);
  expect(chained.cycles < unchained.cycles, "chaining reduces the cycle count");
  expect(four_lanes.cycles < chained.cycles, "four lanes reduce the cycle count");
  expect(chained.arrays == unchained.arrays && chained.arrays == four_lanes.arrays, "timing never changes results");

  // Steady state: every additional strip costs the same, at least one chime (VL cycles) per convoy and at most
  // that plus the start-up latencies of the units.
  const auto t2 = daxpy(text, 128).cycles;
  const auto t3 = daxpy(text, 192).cycles;
  const auto t4 = daxpy(text, 256).cycles;
  const auto per_strip = t4 - t3;
  expect(t3 - t2 == per_strip, "the cost per strip is constant in steady state");
  expect(per_strip >= 3 * 64 && per_strip <= 3 * 64 + 12 + 7 + 6 + 12 + 10,
         "per-strip time = 3 chimes of 64 cycles plus at most the start-up latencies (got " + std::to_string(per_strip) + ")");

  // Strided loads.
  const auto column = vec::run(read(repo + "/examples/vector/column.vasm"));
  expect(column.success, "column program runs: " + column.error);
  if (column.success) {
    std::vector<double> expected;
    for (int row = 0; row < 8; ++row) expected.push_back(row * 8 + 3 + 1);
    expect(column.arrays.at("C") == expected, "LVWS with stride 8 reads one matrix column");
  }
  expect(!vec::run("LV V9, R1\nHALT\n").success, "a bad register is reported");

  if (failures == 0) {
    std::cout << "vector processor: strip-mining, chaining, lanes, convoys and strided access OK ("
              << chained.cycles << " cycles chained vs " << unchained.cycles << " unchained for n = 256)\n";
  }
  return failures == 0 ? 0 : 1;
}
