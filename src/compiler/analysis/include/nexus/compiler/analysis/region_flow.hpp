#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "nexus/compiler/analysis/cfg.hpp"
#include "nexus/compiler/analysis/data_flow.hpp"

namespace nexus::compiler::analysis {

// Region-based (non-iterative) data-flow analysis, after Aho, Lam, Sethi and Ullman, section 9.7.
//
// Region tree of a reducible flow graph, built bottom-up: every reachable block is a leaf region; natural loops,
// innermost first, each become a body region (the loop's current top-level regions, back edges removed, so an
// acyclic graph) wrapped in a loop region (the body plus its back edges); the whole graph becomes the root body
// region.  A flow graph whose forward edges (all edges but the back edges) still contain a cycle is irreducible
// and is rejected.
//
// Forward gen/kill problems are solved without iteration.  A transfer function is f(x) = gen U (x - kill), kept
// normalised (gen and kill disjoint) so that composition, meet and closure stay exact:
//   g o f   = (gen_g U (gen_f - kill_g), kill_f U kill_g)
//   f ^ g   = (gen_f U gen_g, kill_f n kill_g)   for a union meet (may problems, e.g. reaching definitions)
//           = (gen_f n gen_g, kill_f U kill_g)   for an intersection meet (must problems, e.g. available expressions)
//   f*      = id ^ f                             (gen/kill functions are idempotent)
// Bottom-up, every region R gets f_{R,IN[S]} for each subregion S and f_{R,OUT[B]} for each block B inside it;
// top-down, IN[root] = boundary and IN[S] = f_{R,IN[S]}(IN[R]).  For these distributive frameworks the result is
// the meet-over-paths solution, which equals the fixpoint of the iterative algorithm on the reachable blocks.

enum class RegionKind { Block, Body, Loop };

struct Region {
  RegionKind kind = RegionKind::Block;
  ir::BlockId header = 0;
  std::vector<std::size_t> children;  // Body: in topological order, header child first; Loop: the body region
  std::vector<ir::BlockId> blocks;    // every block inside, sorted
  std::vector<std::pair<ir::BlockId, ir::BlockId>> back_edges;  // Loop only
};

struct RegionTree {
  bool reducible = false;
  std::string error;
  std::vector<Region> regions;  // children always precede their parent
  std::size_t root = 0;
  std::vector<bool> reachable;  // per block
};

RegionTree build_region_tree(const ControlFlowGraph& cfg);
std::string print_region_tree(const ir::Function& function, const RegionTree& tree);

struct GenKill {
  DataFlowSet gen;
  DataFlowSet kill;
};

enum class Meet { Union, Intersection };

struct RegionSolution {
  bool success = false;
  std::string error;
  std::vector<DataFlowSet> in_sets;   // unreachable blocks stay empty
  std::vector<DataFlowSet> out_sets;
  std::size_t compositions = 0;
  std::size_t meets = 0;
  std::size_t closures = 0;
};

RegionSolution solve_forward_by_regions(const ControlFlowGraph& cfg, const RegionTree& tree,
                                        const std::vector<GenKill>& transfer, Meet meet, const DataFlowSet& boundary);

// Reaching definitions of locals: definition d is the d-th StoreLocal in block order.
struct ReachingDefinitionProblem {
  std::vector<std::pair<ir::BlockId, std::size_t>> sites;  // (block, instruction index) of each definition
  std::vector<ir::LocalId> locals;                         // the local each definition writes
  std::vector<GenKill> transfer;                           // per block
};

ReachingDefinitionProblem reaching_definition_problem(const ir::Function& function);
// Region tree, reaching definitions solved by regions, and a cross-check against the iterative solver.
std::string print_region_analysis(const ir::Function& function, const ControlFlowGraph& cfg);

}  // namespace nexus::compiler::analysis
