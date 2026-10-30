#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "nexus/compiler/polyhedral/polyhedral.hpp"

namespace poly = nexus::compiler::polyhedral;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

poly::LoopNest parse(const std::string& text) {
  auto parsed = poly::parse_loop_nest(text);
  if (!parsed.nest.has_value()) {
    std::cerr << "parse error: " << parsed.error << '\n' << text << '\n';
    std::exit(1);
  }
  return *parsed.nest;
}

poly::LoopNest load(const std::string& repo, const std::string& name) {
  std::ifstream input(repo + "/examples/loops/" + name);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return parse(buffer.str());
}

poly::Matrix identity(std::size_t depth) {
  poly::Matrix matrix(depth, std::vector<std::int64_t>(depth, 0));
  for (std::size_t index = 0; index < depth; ++index) {
    matrix[index][index] = 1;
  }
  return matrix;
}

void cache_model() {
  // Streaming copy: one miss per 8-element line of each array, nothing else.
  const auto copy = parse("params N = 255\nfor i = 0 to N {\n  A[i] = B[i]\n}\n");
  const auto stats = poly::simulate_cache(copy, {});
  expect(stats.accesses == 512 && stats.misses == 64, "streaming copy: 512 accesses, 64 compulsory misses");
  // Re-reading a 16-element vector 10 times fits in the cache: only the first pass misses.
  const auto reuse = parse("params N = 9, M = 15\nfor r = 0 to N {\n  for i = 0 to M {\n    S[r] = S[r] + V[i]\n  }\n}\n");
  const auto reuse_stats = poly::simulate_cache(reuse, {});
  expect(reuse_stats.misses == 2 + 2, "small working set: only compulsory misses (2 lines of V, 2 of S)");
  // A one-set one-way cache thrashes when two arrays alternate.
  const auto thrash = poly::simulate_cache(copy, {8, 1, 1});
  expect(thrash.misses == thrash.accesses, "direct-mapped single line: alternating arrays always miss");
}

void matmul(const std::string& repo) {
  const auto nest = load(repo, "matmul_tiling.loop");
  const auto analysis = poly::analyze_dependences(nest);
  const auto result = poly::apply_tiling(nest, analysis, identity(3), {8, 8, 8});
  expect(result.legal, "matmul 48: tiling is legal (Fourier-Motzkin path)");
  expect(result.verified && result.tiled_instances == 48 * 48 * 48, "matmul 48: tiled nest verified");
  expect(result.tiles == 216, "matmul 48: 6 x 6 x 6 tiles");
  expect(result.tiled_cache.accesses == result.original_cache.accesses, "same number of references");
  expect(result.tiled_cache.misses * 10 < result.original_cache.misses,
         "matmul 48: tiling cuts the misses by more than 10x (" + std::to_string(result.original_cache.misses) +
             " -> " + std::to_string(result.tiled_cache.misses) + ")");
  // Tiling only j and k (i untiled) is also legal and verified.
  const auto partial = poly::apply_tiling(nest, analysis, identity(3), {0, 16, 16});
  expect(partial.legal && partial.verified && partial.tiles == 9, "matmul 48: partial tiling (0,16,16) verified");
  expect(!poly::apply_tiling(nest, analysis, identity(3), {8, 8}).legal, "wrong number of tile sizes is refused");
}

void stencil(const std::string& repo) {
  const auto nest = load(repo, "stencil_skew.loop");
  const auto analysis = poly::analyze_dependences(nest);
  std::string reason;
  expect(!poly::fully_permutable(nest, analysis, identity(2), &reason), "stencil: not fully permutable as written");
  expect(reason.find("(1,-1)") != std::string::npos, "stencil: the reason names the distance (1,-1)");
  expect(!poly::apply_tiling(nest, analysis, identity(2), {4, 4}).legal, "stencil: rectangular tiling refused");
  const auto skew = poly::find_tiling_skew(nest, analysis, identity(2));
  expect(skew.has_value() && *skew == poly::Matrix({{1, 0}, {1, 1}}), "stencil: autoskew finds [[1,0],[1,1]]");
  if (skew.has_value()) {
    for (const auto& sizes : {std::vector<std::int64_t>{4, 4}, {1, 7}, {5, 0}, {0, 3}, {64, 64}}) {
      const auto result = poly::apply_tiling(nest, analysis, *skew, sizes);
      expect(result.legal && result.verified, "stencil: skewed tiling verified for sizes (" + std::to_string(sizes[0]) +
                                                  "," + std::to_string(sizes[1]) + ")");
    }
  }
  // Same stencil, too large to enumerate: the Fourier-Motzkin legality path must agree.
  const auto big = parse(
      "params N = 120, M = 150\nfor i = 1 to N {\n  for j = 1 to M {\n    A[i][j] = A[i - 1][j + 1] + A[i][j - 1]\n  }\n}\n");
  const auto big_analysis = poly::analyze_dependences(big);
  expect(!poly::fully_permutable(big, big_analysis, identity(2)), "big stencil: FM also refuses the unskewed tiling");
  const auto big_skew = poly::find_tiling_skew(big, big_analysis, identity(2));
  expect(big_skew.has_value() && *big_skew == poly::Matrix({{1, 0}, {1, 1}}), "big stencil: FM autoskew agrees");
  if (big_skew.has_value()) {
    const auto result = poly::apply_tiling(big, big_analysis, *big_skew, {16, 16});
    expect(result.legal && result.verified, "big stencil: skewed 16x16 tiling verified");
  }
}

void triangular() {
  // Bounds depend on outer indices: the point loops must clip to both the tile and the triangle.
  const auto nest = parse(
      "params N = 13\nfor i = 0 to N {\n  for j = i to N {\n    for k = 0 to j {\n"
      "      A[i][j] = A[i][j] + B[j][k] * 3\n    }\n  }\n}\n");
  const auto analysis = poly::analyze_dependences(nest);
  for (const auto& sizes : {std::vector<std::int64_t>{3, 4, 5}, {2, 2, 2}, {0, 5, 1}}) {
    const auto result = poly::apply_tiling(nest, analysis, identity(3), sizes);
    expect(result.legal && result.verified, "triangular 3-deep nest tiled and verified");
  }
}

// Random 2-deep nests and random tile sizes: whenever tiling is accepted, the tiled code must compute the same
// arrays; refused tilings are counted to show the legality test is not vacuous.  Larger nests exercise the
// Fourier-Motzkin legality path, whose acceptances are checked the same way.
void random_nests() {
  std::mt19937 random(709);
  auto pick = [&](int low, int high) { return static_cast<int>(random() % static_cast<unsigned>(high - low + 1)) + low; };
  std::size_t accepted = 0;
  std::size_t refused = 0;
  std::size_t skewed = 0;
  std::size_t large = 0;
  for (int trial = 0; trial < 160; ++trial) {
    const bool big = trial % 8 == 0;
    std::ostringstream text;
    text << "params N = " << (big ? pick(45, 55) : pick(4, 9)) << "\n";
    text << "for i = 0 to N {\n  for j = " << (trial % 3 == 0 ? "i" : "0") << " to N {\n";
    auto offset = [&](const char* index) {
      const int value = pick(-2, 2);
      return std::string(index) + (value == 0 ? "" : (value > 0 ? " + " : " - ") + std::to_string(std::abs(value)));
    };
    text << "    A[" << offset("i") << "][" << offset("j") << "] = A[" << offset("i") << "][" << offset("j") << "] + A["
         << offset("i") << "][" << offset("j") << "]\n  }\n}\n";
    const auto nest = parse(text.str());
    const auto analysis = poly::analyze_dependences(nest);
    large += big ? 1 : 0;
    const std::vector<std::int64_t> sizes{pick(0, 5), pick(1, 6)};
    auto transform = identity(2);
    if (!poly::fully_permutable(nest, analysis, transform)) {
      const auto skew = poly::find_tiling_skew(nest, analysis, transform);
      if (!skew.has_value()) {
        ++refused;
        expect(!poly::apply_tiling(nest, analysis, transform, sizes).legal, "a refused nest must not be tiled");
        continue;
      }
      ++skewed;
      transform = *skew;
    }
    const auto result = poly::apply_tiling(nest, analysis, transform, sizes);
    expect(result.legal && result.verified, "accepted tiling must be verified:\n" + text.str());
    accepted += result.legal ? 1 : 0;
  }
  std::cout << "random nests: " << accepted << " tilings accepted and verified (" << skewed << " after autoskew, " << large
            << " nests on the Fourier-Motzkin path), " << refused << " nests with no legal skew\n";
  expect(accepted > 100 && skewed > 10, "the random sample exercises plain and skewed tiling");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string repo = argc > 1 ? argv[1] : ".";
  cache_model();
  matmul(repo);
  stencil(repo);
  triangular();
  random_nests();
  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "tiling_test: all checks passed\n";
  return 0;
}
