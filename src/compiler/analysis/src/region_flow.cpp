#include "nexus/compiler/analysis/region_flow.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <sstream>

#include "nexus/compiler/analysis/dominators.hpp"

namespace nexus::compiler::analysis {

namespace {

DataFlowSet set_union(const DataFlowSet& lhs, const DataFlowSet& rhs) {
  DataFlowSet result = lhs;
  result.insert(rhs.begin(), rhs.end());
  return result;
}

DataFlowSet set_intersection(const DataFlowSet& lhs, const DataFlowSet& rhs) {
  DataFlowSet result;
  std::set_intersection(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), std::inserter(result, result.end()));
  return result;
}

DataFlowSet set_difference(const DataFlowSet& lhs, const DataFlowSet& rhs) {
  DataFlowSet result;
  std::set_difference(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), std::inserter(result, result.end()));
  return result;
}

GenKill normalized(GenKill f) {
  f.kill = set_difference(f.kill, f.gen);
  return f;
}

// g o f: apply f first.
GenKill compose(const GenKill& g, const GenKill& f) {
  return normalized(GenKill{set_union(g.gen, set_difference(f.gen, g.kill)), set_union(f.kill, g.kill)});
}

GenKill meet_of(const GenKill& f, const GenKill& g, Meet meet) {
  if (meet == Meet::Union) {
    return normalized(GenKill{set_union(f.gen, g.gen), set_intersection(f.kill, g.kill)});
  }
  return normalized(GenKill{set_intersection(f.gen, g.gen), set_union(f.kill, g.kill)});
}

DataFlowSet evaluate(const GenKill& f, const DataFlowSet& x) { return set_union(f.gen, set_difference(x, f.kill)); }

std::string kind_text(RegionKind kind) {
  switch (kind) {
    case RegionKind::Block: return "block";
    case RegionKind::Body: return "body";
    case RegionKind::Loop: return "loop";
  }
  return "?";
}

}  // namespace

RegionTree build_region_tree(const ControlFlowGraph& cfg) {
  RegionTree tree;
  const std::size_t n = cfg.successors.size();
  tree.reachable.assign(n, false);
  if (n == 0) {
    tree.error = "empty flow graph";
    return tree;
  }
  std::vector<ir::BlockId> stack{cfg.entry_block};
  tree.reachable[cfg.entry_block] = true;
  while (!stack.empty()) {
    const auto block = stack.back();
    stack.pop_back();
    for (const auto succ : cfg.successors[block]) {
      if (!tree.reachable[succ]) {
        tree.reachable[succ] = true;
        stack.push_back(succ);
      }
    }
  }

  // Natural loops: back edges n -> h with h dominating n, merged per header.
  const auto dominators = compute_dominators(cfg);
  std::map<ir::BlockId, std::vector<ir::BlockId>> back_sources;
  std::set<std::pair<ir::BlockId, ir::BlockId>> back_edges;
  for (ir::BlockId source = 0; source < n; ++source) {
    if (!tree.reachable[source]) {
      continue;
    }
    for (const auto target : cfg.successors[source]) {
      if (dominators.dominator_sets[source].contains(target)) {
        back_sources[target].push_back(source);
        back_edges.insert({source, target});
      }
    }
  }

  // Reducible iff the forward edges form an acyclic graph.
  std::vector<std::size_t> indegree(n, 0);
  for (ir::BlockId source = 0; source < n; ++source) {
    if (!tree.reachable[source]) {
      continue;
    }
    for (const auto target : cfg.successors[source]) {
      if (!back_edges.contains({source, target})) {
        ++indegree[target];
      }
    }
  }
  std::queue<ir::BlockId> ready;
  ready.push(cfg.entry_block);
  std::size_t visited = 0;
  const auto reachable_count = static_cast<std::size_t>(std::count(tree.reachable.begin(), tree.reachable.end(), true));
  while (!ready.empty()) {
    const auto block = ready.front();
    ready.pop();
    ++visited;
    for (const auto target : cfg.successors[block]) {
      if (!back_edges.contains({block, target}) && --indegree[target] == 0) {
        ready.push(target);
      }
    }
  }
  if (visited != reachable_count) {
    tree.error = "irreducible flow graph: a cycle is entered at more than one block";
    return tree;
  }

  struct Loop {
    ir::BlockId header;
    std::set<ir::BlockId> body;
  };
  std::vector<Loop> loops;
  for (const auto& [header, sources] : back_sources) {
    Loop loop{header, {header}};
    std::vector<ir::BlockId> work(sources.begin(), sources.end());
    while (!work.empty()) {
      const auto block = work.back();
      work.pop_back();
      if (loop.body.insert(block).second) {
        for (const auto pred : cfg.predecessors[block]) {
          if (tree.reachable[pred]) {
            work.push_back(pred);
          }
        }
      }
    }
    loops.push_back(std::move(loop));
  }
  std::stable_sort(loops.begin(), loops.end(), [](const Loop& a, const Loop& b) { return a.body.size() < b.body.size(); });

  std::vector<std::size_t> top(n, 0);  // the outermost region built so far that holds each block
  for (ir::BlockId block = 0; block < n; ++block) {
    if (tree.reachable[block]) {
      top[block] = tree.regions.size();
      tree.regions.push_back(Region{RegionKind::Block, block, {}, {block}, {}});
    }
  }

  // A body region over `blocks`: its children are their current top regions, ordered topologically along the
  // forward edges that stay inside.
  auto make_body = [&](ir::BlockId header, const std::set<ir::BlockId>& blocks) {
    std::set<std::size_t> children;
    for (const auto block : blocks) {
      children.insert(top[block]);
    }
    std::map<std::size_t, std::set<std::size_t>> edges;
    std::map<std::size_t, std::size_t> incoming;
    for (const auto child : children) {
      incoming[child] = 0;
    }
    for (const auto block : blocks) {
      for (const auto succ : cfg.successors[block]) {
        if (blocks.contains(succ) && !back_edges.contains({block, succ}) && top[block] != top[succ] &&
            edges[top[block]].insert(top[succ]).second) {
          ++incoming[top[succ]];
        }
      }
    }
    Region body{RegionKind::Body, header, {}, std::vector<ir::BlockId>(blocks.begin(), blocks.end()), {}};
    std::queue<std::size_t> order;
    order.push(top[header]);
    while (!order.empty()) {
      const auto child = order.front();
      order.pop();
      body.children.push_back(child);
      for (const auto next : edges[child]) {
        if (--incoming[next] == 0) {
          order.push(next);
        }
      }
    }
    tree.regions.push_back(std::move(body));
    return tree.regions.size() - 1;
  };

  for (const auto& loop : loops) {
    const std::size_t body = make_body(loop.header, loop.body);
    Region region{RegionKind::Loop, loop.header, {body}, tree.regions[body].blocks, {}};
    for (const auto source : back_sources[loop.header]) {
      region.back_edges.emplace_back(source, loop.header);
    }
    tree.regions.push_back(std::move(region));
    for (const auto block : loop.body) {
      top[block] = tree.regions.size() - 1;
    }
  }
  std::set<ir::BlockId> all;
  for (ir::BlockId block = 0; block < n; ++block) {
    if (tree.reachable[block]) {
      all.insert(block);
    }
  }
  tree.root = make_body(cfg.entry_block, all);
  tree.reducible = true;
  return tree;
}

std::string print_region_tree(const ir::Function& function, const RegionTree& tree) {
  std::ostringstream output;
  if (!tree.reducible) {
    output << "  " << tree.error << '\n';
    return output.str();
  }
  std::function<void(std::size_t, std::size_t)> show = [&](std::size_t id, std::size_t depth) {
    const auto& region = tree.regions[id];
    output << std::string(2 + 2 * depth, ' ') << 'R' << id << ' ' << kind_text(region.kind) << " header bb"
           << region.header << '.' << function.blocks[region.header].label;
    if (region.kind != RegionKind::Block) {
      output << " {";
      for (std::size_t index = 0; index < region.blocks.size(); ++index) {
        output << (index ? ", " : "") << "bb" << region.blocks[index];
      }
      output << '}';
    }
    for (const auto& [source, target] : region.back_edges) {
      output << " back edge bb" << source << "->bb" << target;
    }
    output << '\n';
    for (const auto child : region.children) {
      show(child, depth + 1);
    }
  };
  show(tree.root, 0);
  return output.str();
}

RegionSolution solve_forward_by_regions(const ControlFlowGraph& cfg, const RegionTree& tree,
                                        const std::vector<GenKill>& transfer, Meet meet, const DataFlowSet& boundary) {
  RegionSolution solution;
  const std::size_t n = cfg.successors.size();
  if (!tree.reducible) {
    solution.error = tree.error;
    return solution;
  }
  if (transfer.size() != n) {
    solution.error = "one transfer function per block is needed";
    return solution;
  }
  std::vector<std::map<std::size_t, GenKill>> in_fn(tree.regions.size());     // f_{R,IN[S]} per child S
  std::vector<std::map<ir::BlockId, GenKill>> out_fn(tree.regions.size());   // f_{R,OUT[B]} per block B
  std::vector<std::size_t> owner(n, 0);  // the child of the region being summarised that holds each block

  // Bottom-up: children always precede their parent in tree.regions.
  for (std::size_t id = 0; id < tree.regions.size(); ++id) {
    const auto& region = tree.regions[id];
    if (region.kind == RegionKind::Block) {
      out_fn[id][region.header] = normalized(transfer[region.header]);
      continue;
    }
    if (region.kind == RegionKind::Loop) {
      const std::size_t body = region.children.front();
      std::optional<GenKill> around;
      for (const auto& [source, target] : region.back_edges) {
        const auto& f = out_fn[body].at(source);
        if (around.has_value()) {
          ++solution.meets;
        }
        around = around.has_value() ? meet_of(*around, f, meet) : f;
      }
      ++solution.meets;
      ++solution.closures;
      const GenKill closure = meet_of(GenKill{}, around.value_or(GenKill{}), meet);  // f* = id ^ f
      in_fn[id][body] = closure;
      for (const auto& [block, f] : out_fn[body]) {
        out_fn[id][block] = compose(f, closure);
        ++solution.compositions;
      }
      continue;
    }
    for (const auto child : region.children) {
      for (const auto block : tree.regions[child].blocks) {
        owner[block] = child;
      }
    }
    for (const auto child : region.children) {
      const auto& sub = tree.regions[child];
      GenKill entry;  // the header child is entered with IN[R] itself
      if (child != region.children.front()) {
        std::optional<GenKill> merged;
        for (const auto pred : cfg.predecessors[sub.header]) {
          if (!tree.reachable[pred] || !std::binary_search(region.blocks.begin(), region.blocks.end(), pred) ||
              owner[pred] == child) {
            continue;
          }
          const auto& f = out_fn[id].at(pred);  // its child came earlier in topological order
          if (merged.has_value()) {
            ++solution.meets;
          }
          merged = merged.has_value() ? meet_of(*merged, f, meet) : f;
        }
        entry = merged.value_or(GenKill{});
      }
      in_fn[id][child] = entry;
      for (const auto& [block, f] : out_fn[child]) {
        out_fn[id][block] = compose(f, entry);
        ++solution.compositions;
      }
    }
  }

  // Top-down.
  solution.in_sets.assign(n, {});
  solution.out_sets.assign(n, {});
  std::function<void(std::size_t, const DataFlowSet&)> descend = [&](std::size_t id, const DataFlowSet& in) {
    const auto& region = tree.regions[id];
    if (region.kind == RegionKind::Block) {
      solution.in_sets[region.header] = in;
      solution.out_sets[region.header] = evaluate(normalized(transfer[region.header]), in);
      return;
    }
    for (const auto child : region.children) {
      descend(child, evaluate(in_fn[id].at(child), in));
    }
  };
  descend(tree.root, boundary);
  solution.success = true;
  return solution;
}

ReachingDefinitionProblem reaching_definition_problem(const ir::Function& function) {
  ReachingDefinitionProblem problem;
  std::map<ir::LocalId, DataFlowSet> definitions_of;
  for (ir::BlockId block = 0; block < function.blocks.size(); ++block) {
    const auto& instructions = function.blocks[block].instructions;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
      if (instructions[index].kind == ir::InstructionKind::StoreLocal) {
        definitions_of[instructions[index].local].insert(problem.sites.size());
        problem.sites.emplace_back(block, index);
        problem.locals.push_back(instructions[index].local);
      }
    }
  }
  problem.transfer.assign(function.blocks.size(), {});
  for (DataFlowEntityId d = 0; d < problem.sites.size(); ++d) {
    auto& f = problem.transfer[problem.sites[d].first];
    // A later definition of the same local in the block replaces this one.
    for (const auto other : definitions_of[problem.locals[d]]) {
      f.gen.erase(other);
    }
    f.gen.insert(d);
    const auto& same_local = definitions_of[problem.locals[d]];
    f.kill.insert(same_local.begin(), same_local.end());
  }
  for (auto& f : problem.transfer) {
    f = normalized(f);
  }
  return problem;
}

std::string print_region_analysis(const ir::Function& function, const ControlFlowGraph& cfg) {
  std::ostringstream output;
  output << "func " << function.name << " regions:\n";
  const auto tree = build_region_tree(cfg);
  output << print_region_tree(function, tree);
  if (!tree.reducible) {
    return output.str();
  }
  const auto problem = reaching_definition_problem(function);
  const auto solution = solve_forward_by_regions(cfg, tree, problem.transfer, Meet::Union, {});
  auto definitions = [&](const DataFlowSet& set) {
    std::string text = "{";
    for (const auto d : set) {
      text += (text.size() > 1 ? ", " : "") + std::string("d") + std::to_string(d) + ":" +
              ir::local_info(function, problem.locals[d]).name;
    }
    return text + "}";
  };
  output << "reaching definitions (by regions):\n";
  for (ir::BlockId block = 0; block < function.blocks.size(); ++block) {
    if (!tree.reachable[block]) {
      continue;
    }
    output << "  bb" << block << '.' << function.blocks[block].label << "  in " << definitions(solution.in_sets[block])
           << "  out " << definitions(solution.out_sets[block]) << '\n';
  }
  DataFlowProblem iterative_problem;
  iterative_problem.direction = DataFlowDirection::Forward;
  for (const auto& f : problem.transfer) {
    iterative_problem.gen_sets.push_back(f.gen);
    iterative_problem.kill_sets.push_back(f.kill);
  }
  const auto iterative = solve_data_flow(cfg, iterative_problem);
  bool agree = true;
  for (ir::BlockId block = 0; block < function.blocks.size(); ++block) {
    agree = agree && (!tree.reachable[block] || (iterative.in_sets[block] == solution.in_sets[block] &&
                                                 iterative.out_sets[block] == solution.out_sets[block]));
  }
  output << "region solver: " << solution.compositions << " compositions, " << solution.meets << " meets, "
         << solution.closures << " closures, no iteration; iterative solver (" << iterative.iterations
         << " passes) " << (agree ? "agrees" : "DISAGREES") << '\n';
  return output.str();
}

}  // namespace nexus::compiler::analysis
