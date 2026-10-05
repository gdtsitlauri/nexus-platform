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

poly::LoopNest load(const std::string& repo, const std::string& name) {
  std::ifstream input(repo + "/examples/loops/" + name);
  std::stringstream buffer;
  buffer << input.rdbuf();
  auto parsed = poly::parse_loop_nest(buffer.str());
  if (!parsed.nest.has_value()) {
    std::cerr << name << ": " << parsed.error << '\n';
    std::exit(1);
  }
  return *parsed.nest;
}

void examples(const std::string& repo) {
  const auto wavefront = load(repo, "wavefront.loop");
  const auto wave = poly::analyze_dependences(wavefront);
  expect(wave.dependences.size() == 2 && !wave.parallel[0] && !wave.parallel[1],
         "wavefront: two uniform flow dependences, no parallel loop");
  const auto skew_then_interchange =
      poly::multiply(*poly::interchange_matrix(2, 1, 2), *poly::skew_matrix(2, 2, 1, 1));
  const auto wave_result = poly::apply_transformation(wavefront, wave, skew_then_interchange);
  expect(wave_result.legal && wave_result.verified, "wavefront: skew + interchange is legal and verified");
  expect(wave_result.parallel_after.size() == 2 && !wave_result.parallel_after[0] && wave_result.parallel_after[1],
         "wavefront: after skewing the inner loop becomes parallel");
  expect(!poly::apply_transformation(wavefront, wave, *poly::reversal_matrix(2, 1)).legal,
         "wavefront: reversing the outer loop is illegal");

  const auto illegal = load(repo, "interchange_illegal.loop");
  const auto illegal_deps = poly::analyze_dependences(illegal);
  expect(illegal_deps.dependences.size() == 1 && illegal_deps.dependences.front().distance ==
                                                    std::vector<std::int64_t>({1, -1}),
         "distance (1,-1) is recovered");
  expect(!poly::apply_transformation(illegal, illegal_deps, *poly::interchange_matrix(2, 1, 2)).legal,
         "interchanging a (1,-1) dependence is illegal");
  expect(illegal_deps.parallel[1] && !illegal_deps.parallel[0], "only the inner loop j is parallel");

  const auto matmul = load(repo, "matmul.loop");
  const auto mm = poly::analyze_dependences(matmul);
  expect(mm.parallel[0] && mm.parallel[1] && !mm.parallel[2], "matmul: i and j parallel, k carries the reduction");
  for (const auto& [a, b] : {std::pair<std::size_t, std::size_t>{1, 2}, {1, 3}, {2, 3}}) {
    const auto result = poly::apply_transformation(matmul, mm, *poly::interchange_matrix(3, a, b));
    expect(result.legal && result.verified, "matmul: every loop interchange is legal and verified");
  }

  const auto triangular = load(repo, "triangular.loop");
  const auto tri = poly::analyze_dependences(triangular);
  expect(tri.dependences.size() == 1 && tri.dependences.front().level == 3,
         "triangular: only the loop-independent S1 -> S2 flow dependence (even/odd rows never alias)");
  const auto tri_result = poly::apply_transformation(triangular, tri, *poly::interchange_matrix(2, 1, 2));
  expect(tri_result.legal && tri_result.verified, "triangular: interchange over a non-rectangular domain is verified");
}

// Random 2-deep nests: Fourier-Motzkin must never miss a dependence that enumeration finds, and
// every transformation the legality test accepts must reproduce the original results.
void random_nests() {
  std::mt19937 random(2026);
  auto pick = [&](int low, int high) { return static_cast<int>(random() % static_cast<unsigned>(high - low + 1)) + low; };
  std::size_t legal = 0;
  std::size_t checked = 0;
  for (int trial = 0; trial < 120; ++trial) {
    std::ostringstream text;
    text << "params N = " << pick(3, 6) << "\n";
    text << "for i = 0 to N {\n  for j = " << (trial % 3 == 0 ? "i" : "0") << " to N {\n";
    auto subscript = [&](const char* index) {
      std::ostringstream s;
      const int scale = pick(1, 2);
      s << (scale == 1 ? "" : std::to_string(scale) + " * ") << index;
      const int offset = pick(-1, 1);
      if (offset != 0) {
        s << (offset > 0 ? " + " : " - ") << std::abs(offset);
      }
      return s.str();
    };
    text << "    A[" << subscript("i") << "][" << subscript("j") << "] = A[" << subscript("i") << "][" << subscript("j")
         << "] + A[" << subscript(pick(0, 1) ? "i" : "j") << "][" << subscript("j") << "]\n  }\n}\n";
    const auto parsed = poly::parse_loop_nest(text.str());
    if (!parsed.nest.has_value()) {
      expect(false, "random nest failed to parse: " + parsed.error + "\n" + text.str());
      continue;
    }
    const auto analysis = poly::analyze_dependences(*parsed.nest);
    expect(analysis.fm_misses == 0, "Fourier-Motzkin missed a dependence:\n" + text.str());
    const std::vector<poly::Matrix> transforms = {
        *poly::interchange_matrix(2, 1, 2), *poly::reversal_matrix(2, 1), *poly::reversal_matrix(2, 2),
        *poly::skew_matrix(2, 2, 1, 1),
        poly::multiply(*poly::interchange_matrix(2, 1, 2), *poly::skew_matrix(2, 2, 1, 1))};
    for (const auto& transform : transforms) {
      const auto result = poly::apply_transformation(*parsed.nest, analysis, transform);
      ++checked;
      if (result.legal) {
        ++legal;
        if (!result.verified) {
          expect(false, "a transformation judged legal changed the results:\n" + text.str() + result.code);
        }
      }
    }
  }
  expect(legal > 50 && legal < checked, "random trials must contain both legal and illegal transformations");
  std::cout << "random nests: " << checked << " transformations checked, " << legal << " legal and verified\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: polyhedral_test <repo-root>\n";
    return 1;
  }
  examples(argv[1]);
  random_nests();
  if (failures == 0) {
    std::cout << "polyhedral_test: all checks passed\n";
  }
  return failures == 0 ? 0 : 1;
}
