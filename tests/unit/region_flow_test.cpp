#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "nexus/compiler/analysis/cfg.hpp"
#include "nexus/compiler/analysis/region_flow.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"

namespace an = nexus::compiler::analysis;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

an::ControlFlowGraph make_cfg(const std::vector<std::vector<std::size_t>>& successors) {
  an::ControlFlowGraph cfg;
  cfg.entry_block = 0;
  cfg.successors = successors;
  cfg.predecessors.assign(successors.size(), {});
  for (std::size_t block = 0; block < successors.size(); ++block) {
    for (const auto succ : successors[block]) {
      cfg.predecessors[succ].push_back(block);
    }
  }
  return cfg;
}

std::vector<bool> reachable_blocks(const an::ControlFlowGraph& cfg) {
  std::vector<bool> seen(cfg.successors.size(), false);
  std::vector<std::size_t> work{0};
  seen[0] = true;
  while (!work.empty()) {
    const auto block = work.back();
    work.pop_back();
    for (const auto succ : cfg.successors[block]) {
      if (!seen[succ]) {
        seen[succ] = true;
        work.push_back(succ);
      }
    }
  }
  return seen;
}

// Reference: the classic iterative algorithm over the reachable blocks, with an ENTRY node whose OUT is the boundary.
std::vector<an::DataFlowSet> iterative_in(const an::ControlFlowGraph& cfg, const std::vector<an::GenKill>& transfer,
                                          an::Meet meet, const an::DataFlowSet& boundary, const an::DataFlowSet& universe) {
  const auto reachable = reachable_blocks(cfg);
  const std::size_t n = cfg.successors.size();
  auto apply = [&](std::size_t block, const an::DataFlowSet& x) {
    an::DataFlowSet out = transfer[block].gen;
    for (const auto value : x) {
      if (!transfer[block].kill.contains(value)) {
        out.insert(value);
      }
    }
    return out;
  };
  std::vector<an::DataFlowSet> in(n, meet == an::Meet::Union ? an::DataFlowSet{} : universe);
  std::vector<an::DataFlowSet> out(n);
  for (std::size_t block = 0; block < n; ++block) {
    out[block] = apply(block, in[block]);
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t block = 0; block < n; ++block) {
      if (!reachable[block]) {
        continue;
      }
      std::optional<an::DataFlowSet> merged;
      auto combine = [&](const an::DataFlowSet& value) {
        if (!merged.has_value()) {
          merged = value;
          return;
        }
        an::DataFlowSet next;
        for (const auto element : (meet == an::Meet::Union ? value : *merged)) {
          if (meet == an::Meet::Union || value.contains(element)) {
            next.insert(element);
          }
        }
        if (meet == an::Meet::Union) {
          next.insert(merged->begin(), merged->end());
        }
        merged = next;
      };
      if (block == 0) {
        combine(boundary);
      }
      for (const auto pred : cfg.predecessors[block]) {
        if (reachable[pred]) {
          combine(out[pred]);
        }
      }
      const auto new_in = merged.value_or(an::DataFlowSet{});
      const auto new_out = apply(block, new_in);
      if (new_in != in[block] || new_out != out[block]) {
        in[block] = new_in;
        out[block] = new_out;
        changed = true;
      }
    }
  }
  return in;
}

// Reference reducibility: T1 (remove a self loop) and T2 (merge a node into its unique predecessor) until no
// change; the reachable graph is reducible iff it shrinks to one node.
bool reducible_by_t1_t2(const an::ControlFlowGraph& cfg) {
  const auto reachable = reachable_blocks(cfg);
  std::vector<std::set<std::size_t>> succ(cfg.successors.size());
  std::vector<std::set<std::size_t>> pred(cfg.successors.size());
  std::set<std::size_t> alive;
  for (std::size_t block = 0; block < cfg.successors.size(); ++block) {
    if (!reachable[block]) {
      continue;
    }
    alive.insert(block);
    for (const auto s : cfg.successors[block]) {
      succ[block].insert(s);
      pred[s].insert(block);
    }
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto node : alive) {
      if (succ[node].erase(node) > 0) {
        pred[node].erase(node);
        changed = true;
      }
      if (node != 0 && pred[node].size() == 1) {
        const auto into = *pred[node].begin();
        succ[into].erase(node);
        for (const auto s : succ[node]) {
          pred[s].erase(node);
          pred[s].insert(into);
          succ[into].insert(s);
        }
        succ[node].clear();
        pred[node].clear();
        alive.erase(node);
        changed = true;
        break;
      }
    }
  }
  return alive.size() == 1;
}

std::vector<an::GenKill> random_transfer(std::mt19937& random, std::size_t blocks, std::size_t universe) {
  std::vector<an::GenKill> transfer(blocks);
  for (auto& f : transfer) {
    for (std::size_t element = 0; element < universe; ++element) {
      const auto roll = random() % 8;
      if (roll == 0) {
        f.gen.insert(element);
      } else if (roll == 1) {
        f.kill.insert(element);
      } else if (roll == 2) {  // overlapping gen and kill: the function is gen U (x - kill) either way
        f.gen.insert(element);
        f.kill.insert(element);
      }
    }
  }
  return transfer;
}

// Structured programs: sequences, if/else, while, do-while, break and continue, nested.  Always reducible.
std::vector<std::vector<std::size_t>> structured_program(std::mt19937& random) {
  std::vector<std::vector<std::size_t>> succ;
  auto block = [&]() {
    succ.emplace_back();
    return succ.size() - 1;
  };
  auto edge = [&](std::size_t from, std::size_t to) { succ[from].push_back(to); };
  struct LoopContext {
    std::size_t header;
    std::size_t exit;
  };
  std::vector<LoopContext> loops;
  std::function<std::size_t(std::size_t, int)> statements = [&](std::size_t at, int depth) -> std::size_t {
    const int count = 1 + static_cast<int>(random() % 3);
    for (int index = 0; index < count; ++index) {
      const auto kind = depth >= 4 ? 0U : random() % 7;
      if (kind == 0) {
        const auto next = block();
        edge(at, next);
        at = next;
      } else if (kind == 1 || kind == 2) {  // if / if-else
        const auto then_block = block();
        const auto join = block();
        edge(at, then_block);
        edge(statements(then_block, depth + 1), join);
        if (kind == 2) {
          const auto else_block = block();
          edge(at, else_block);
          edge(statements(else_block, depth + 1), join);
        } else {
          edge(at, join);
        }
        at = join;
      } else if (kind == 3 || kind == 4) {  // while
        const auto header = block();
        const auto body = block();
        const auto exit = block();
        edge(at, header);
        edge(header, body);
        edge(header, exit);
        loops.push_back({header, exit});
        edge(statements(body, depth + 1), header);
        loops.pop_back();
        at = exit;
      } else if (kind == 5) {  // do-while
        const auto body = block();
        const auto exit = block();
        edge(at, body);
        loops.push_back({body, exit});
        const auto end = statements(body, depth + 1);
        loops.pop_back();
        edge(end, body);
        edge(end, exit);
        at = exit;
      } else if (!loops.empty()) {  // break or continue out of a conditional
        const auto taken = block();
        const auto next = block();
        edge(at, taken);
        edge(at, next);
        edge(taken, random() % 2 == 0 ? loops.back().exit : loops.back().header);
        at = next;
      }
    }
    return at;
  };
  const auto entry = block();
  statements(entry, 0);
  return succ;
}

void compare(const an::ControlFlowGraph& cfg, const std::vector<an::GenKill>& transfer, std::size_t universe_size,
             const std::string& what, std::size_t& checked) {
  const auto tree = an::build_region_tree(cfg);
  expect(tree.reducible, what + ": region tree built");
  if (!tree.reducible) {
    return;
  }
  an::DataFlowSet universe;
  for (std::size_t element = 0; element < universe_size; ++element) {
    universe.insert(element);
  }
  const auto reachable = reachable_blocks(cfg);
  for (const auto meet : {an::Meet::Union, an::Meet::Intersection}) {
    const an::DataFlowSet boundary = meet == an::Meet::Union ? an::DataFlowSet{1, 3} : an::DataFlowSet{0, 2, 4, 5};
    const auto solution = an::solve_forward_by_regions(cfg, tree, transfer, meet, boundary);
    const auto expected = iterative_in(cfg, transfer, meet, boundary, universe);
    bool same = solution.success;
    for (std::size_t block = 0; same && block < cfg.successors.size(); ++block) {
      same = !reachable[block] || solution.in_sets[block] == expected[block];
    }
    expect(same, what + (meet == an::Meet::Union ? ": union" : ": intersection") + " meet differs from the iterative solution");
    ++checked;
  }
}

void hand_examples() {
  // Dragon-book style nest: 0 -> 1 (outer header) -> 2 (inner header) -> 3 -> 2, 3 -> 4 -> 1, 1 -> 5.
  const auto cfg = make_cfg({{1}, {2, 5}, {3}, {2, 4}, {1}, {}});
  const auto tree = an::build_region_tree(cfg);
  expect(tree.reducible, "nested loops are reducible");
  std::size_t loops = 0;
  std::size_t inner = 0;
  for (std::size_t id = 0; id < tree.regions.size(); ++id) {
    if (tree.regions[id].kind == an::RegionKind::Loop) {
      ++loops;
      if (tree.regions[id].header == 2) {
        inner = id;
        expect(tree.regions[id].blocks == std::vector<std::size_t>({2, 3}), "inner loop body {2, 3}");
      } else {
        expect(tree.regions[id].header == 1 && tree.regions[id].blocks == std::vector<std::size_t>({1, 2, 3, 4}),
               "outer loop body {1, 2, 3, 4}");
        expect(inner != 0 && inner < id, "the inner loop region is built before the outer one");
      }
    }
  }
  expect(loops == 2, "two loop regions");
  expect(tree.regions[tree.root].kind == an::RegionKind::Body && tree.regions[tree.root].blocks.size() == 6,
         "the root body region covers every block");
  // The classic irreducible graph: 0 -> 1, 0 -> 2, 1 <-> 2.
  const auto irreducible = an::build_region_tree(make_cfg({{1, 2}, {2}, {1}}));
  expect(!irreducible.reducible && irreducible.error.find("irreducible") != std::string::npos,
         "a two-entry cycle is rejected as irreducible");
  // An unreachable block jumping into a loop must not hide the back edge (dominators ignore unreachable blocks).
  const auto dead = make_cfg({{1}, {2, 3}, {1}, {}, {2}});
  const auto dead_tree = an::build_region_tree(dead);
  expect(dead_tree.reducible && !dead_tree.reachable[4], "unreachable predecessor ignored");
  // Reaching definitions in a loop: the definition in the body reaches the header around the back edge.
  std::vector<an::GenKill> rd(3);
  rd[0] = {{0}, {0, 1}};  // d0: x = ...
  rd[2] = {{1}, {0, 1}};  // d1: x = ... in the body
  const auto loop = make_cfg({{1}, {2}, {1}});
  const auto solution = an::solve_forward_by_regions(loop, an::build_region_tree(loop), rd, an::Meet::Union, {});
  expect(solution.success && solution.in_sets[1] == an::DataFlowSet({0, 1}) && solution.out_sets[2] == an::DataFlowSet({1}),
         "loop: both definitions reach the header, only d1 leaves the body");
  expect(solution.closures == 1, "one closure for one loop");
}

void random_programs() {
  std::mt19937 random(97);
  std::size_t checked = 0;
  std::size_t blocks = 0;
  std::size_t loop_regions = 0;
  for (int trial = 0; trial < 800; ++trial) {
    const auto cfg = make_cfg(structured_program(random));
    blocks += cfg.successors.size();
    const auto tree = an::build_region_tree(cfg);
    for (const auto& region : tree.regions) {
      loop_regions += region.kind == an::RegionKind::Loop ? 1 : 0;
    }
    expect(reducible_by_t1_t2(cfg), "structured programs are reducible (T1/T2 reference)");
    compare(cfg, random_transfer(random, cfg.successors.size(), 6), 6, "structured program " + std::to_string(trial),
            checked);
  }
  std::cout << "structured programs: " << checked << " solutions equal to the iterative ones (" << blocks << " blocks, "
            << loop_regions << " loop regions)\n";
}

void random_graphs() {
  std::mt19937 random(709);
  std::size_t reducible = 0;
  std::size_t irreducible = 0;
  std::size_t checked = 0;
  for (int trial = 0; trial < 4000; ++trial) {
    const std::size_t n = 2 + random() % 7;
    std::vector<std::vector<std::size_t>> succ(n);
    for (std::size_t block = 0; block < n; ++block) {
      const std::size_t out = 1 + random() % 3;
      for (std::size_t edge = 0; edge < out; ++edge) {
        const std::size_t target = random() % n;
        if (std::find(succ[block].begin(), succ[block].end(), target) == succ[block].end()) {
          succ[block].push_back(target);
        }
      }
    }
    const auto cfg = make_cfg(succ);
    const auto tree = an::build_region_tree(cfg);
    const bool expected = reducible_by_t1_t2(cfg);
    expect(tree.reducible == expected, "trial " + std::to_string(trial) + ": reducibility differs from T1/T2");
    if (tree.reducible) {
      ++reducible;
      compare(cfg, random_transfer(random, n, 6), 6, "random graph " + std::to_string(trial), checked);
    } else {
      ++irreducible;
    }
  }
  std::cout << "random graphs: " << reducible << " reducible (" << checked << " solutions equal to the iterative ones), "
            << irreducible << " irreducible, all classified as by T1/T2\n";
  expect(irreducible > 100 && reducible > 1000, "the random sample contains both kinds of graphs");
}

std::optional<nexus::compiler::ir::Module> lower(const std::string& source) {
  auto parsed = nexus::compiler::frontend::parse_source(source);
  if (!parsed.program || !parsed.diagnostics.empty()) {
    return std::nullopt;
  }
  if (!nexus::compiler::semantics::analyze_program(*parsed.program).diagnostics.empty()) {
    return std::nullopt;
  }
  auto lowered = nexus::compiler::ir::lower_program(*parsed.program);
  if (!lowered.module || !lowered.diagnostics.empty()) {
    return std::nullopt;
  }
  return std::move(*lowered.module);
}

// Every program of the language in the repository: reaching definitions by regions equal the iterative solver's.
void language_examples(const std::string& repo) {
  std::size_t functions = 0;
  std::vector<std::filesystem::path> files;
  for (const auto* folder : {"/examples", "/tests/programs"}) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(repo + folder)) {
      if (entry.path().extension() == ".nx") {
        files.push_back(entry.path());
      }
    }
  }
  std::sort(files.begin(), files.end());
  for (const auto& path : files) {
    std::ifstream input(path);
    std::stringstream text;
    text << input.rdbuf();
    const auto module = lower(text.str());
    if (!module.has_value()) {
      continue;  // examples that are meant to fail
    }
    for (const auto& function : module->functions) {
      const auto report = an::print_region_analysis(function, an::build_cfg(function));
      expect(report.find("agrees") != std::string::npos,
             path.filename().string() + " " + function.name + ":\n" + report);
      ++functions;
    }
  }
  std::cout << "language programs: " << files.size() << " files, " << functions << " functions, reaching definitions by regions agree\n";
  expect(functions >= 5, "the examples were found");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string repo = argc > 1 ? argv[1] : ".";
  hand_examples();
  random_programs();
  random_graphs();
  language_examples(repo);
  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "region_flow_test: all checks passed\n";
  return 0;
}
