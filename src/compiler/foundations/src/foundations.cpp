#include "nexus/compiler/foundations/foundations.hpp"

#include <algorithm>
#include <deque>
#include <sstream>

namespace nexus::compiler::foundations {

// ---- hash table ------------------------------------------------------------------------------------

HashTable::HashTable(std::size_t buckets, double max_load)
    : buckets_(std::max<std::size_t>(buckets, 1)), max_load_(max_load) {}

std::uint64_t HashTable::hash(const std::string& key) {
  std::uint64_t h = 1469598103934665603ULL;  // FNV-1a
  for (const unsigned char c : key) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

double HashTable::load_factor() const {
  return static_cast<double>(size_) / static_cast<double>(buckets_.size());
}

std::size_t HashTable::longest_chain() const {
  std::size_t longest = 0;
  for (const auto& chain : buckets_) {
    longest = std::max(longest, chain.size());
  }
  return longest;
}

void HashTable::rehash(std::size_t buckets) {
  std::vector<std::vector<std::pair<std::string, std::int64_t>>> next(buckets);
  for (auto& chain : buckets_) {
    for (auto& entry : chain) {
      next[hash(entry.first) % buckets].push_back(std::move(entry));
    }
  }
  buckets_ = std::move(next);
  ++rehashes_;
}

bool HashTable::insert(const std::string& key, std::int64_t value) {
  auto& chain = buckets_[hash(key) % buckets_.size()];
  for (auto& entry : chain) {
    ++probes_;
    if (entry.first == key) {
      entry.second = value;
      return false;
    }
  }
  chain.emplace_back(key, value);
  ++size_;
  if (load_factor() > max_load_) {
    rehash(buckets_.size() * 2);
  }
  return true;
}

std::optional<std::int64_t> HashTable::find(const std::string& key) const {
  for (const auto& entry : buckets_[hash(key) % buckets_.size()]) {
    ++probes_;
    if (entry.first == key) {
      return entry.second;
    }
  }
  return std::nullopt;
}

bool HashTable::erase(const std::string& key) {
  auto& chain = buckets_[hash(key) % buckets_.size()];
  for (auto it = chain.begin(); it != chain.end(); ++it) {
    ++probes_;
    if (it->first == key) {
      chain.erase(it);
      --size_;
      return true;
    }
  }
  return false;
}

std::vector<std::pair<std::string, std::int64_t>> HashTable::items() const {
  std::vector<std::pair<std::string, std::int64_t>> all;
  for (const auto& chain : buckets_) {
    all.insert(all.end(), chain.begin(), chain.end());
  }
  std::sort(all.begin(), all.end());
  return all;
}

ScopedSymbolTable::ScopedSymbolTable() { enter_scope(); }

void ScopedSymbolTable::enter_scope() { scopes_.emplace_back(); }

void ScopedSymbolTable::exit_scope() {
  if (scopes_.size() > 1) {
    scopes_.pop_back();
  }
}

bool ScopedSymbolTable::declare(const std::string& name, std::int64_t info) {
  if (scopes_.back().find(name).has_value()) {
    return false;
  }
  scopes_.back().insert(name, info);
  return true;
}

std::optional<std::pair<std::int64_t, std::size_t>> ScopedSymbolTable::lookup(const std::string& name) const {
  for (std::size_t depth = scopes_.size(); depth > 0; --depth) {
    if (const auto info = scopes_[depth - 1].find(name)) {
      return std::pair{*info, depth - 1};
    }
  }
  return std::nullopt;
}

// ---- trees -------------------------------------------------------------------------------------------

std::size_t Tree::add(const std::string& text, std::optional<std::size_t> parent) {
  label.push_back(text);
  children.emplace_back();
  const std::size_t id = label.size() - 1;
  if (parent) {
    children[*parent].push_back(id);
  }
  return id;
}

std::optional<Tree> parse_tree(const std::string& text) {
  Tree tree;
  std::vector<std::size_t> open;  // stack of parents
  std::optional<std::size_t> last;
  std::size_t i = 0;
  auto skip = [&] {
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == ',')) {
      ++i;
    }
  };
  skip();
  while (i < text.size()) {
    if (text[i] == '(') {
      if (!last) {
        return std::nullopt;
      }
      open.push_back(*last);
      ++i;
    } else if (text[i] == ')') {
      if (open.empty()) {
        return std::nullopt;
      }
      open.pop_back();
      ++i;
    } else {
      std::size_t end = i;
      while (end < text.size() && text[end] != '(' && text[end] != ')' && text[end] != ' ' && text[end] != ',') {
        ++end;
      }
      if (open.empty() && !tree.label.empty()) {
        return std::nullopt;  // a second root
      }
      last = tree.add(text.substr(i, end - i), open.empty() ? std::nullopt : std::optional{open.back()});
      i = end;
    }
    skip();
  }
  if (!open.empty() || tree.label.empty()) {
    return std::nullopt;
  }
  return tree;
}

namespace {
void pre(const Tree& t, std::size_t n, std::vector<std::string>& out) {
  out.push_back(t.label[n]);
  for (const auto c : t.children[n]) pre(t, c, out);
}
void post(const Tree& t, std::size_t n, std::vector<std::string>& out) {
  for (const auto c : t.children[n]) post(t, c, out);
  out.push_back(t.label[n]);
}
void in(const Tree& t, std::size_t n, std::vector<std::string>& out) {
  const auto& ch = t.children[n];
  if (!ch.empty()) in(t, ch.front(), out);
  out.push_back(t.label[n]);
  for (std::size_t k = 1; k < ch.size(); ++k) in(t, ch[k], out);
}
std::size_t tree_height(const Tree& t, std::size_t n) {
  std::size_t h = 0;
  for (const auto c : t.children[n]) h = std::max(h, tree_height(t, c));
  return h + 1;
}
}  // namespace

std::vector<std::string> preorder(const Tree& tree) {
  std::vector<std::string> out;
  pre(tree, 0, out);
  return out;
}
std::vector<std::string> postorder(const Tree& tree) {
  std::vector<std::string> out;
  post(tree, 0, out);
  return out;
}
std::vector<std::string> inorder(const Tree& tree) {
  std::vector<std::string> out;
  in(tree, 0, out);
  return out;
}
std::vector<std::string> level_order(const Tree& tree) {
  std::vector<std::string> out;
  std::deque<std::size_t> queue{0};
  while (!queue.empty()) {
    const auto n = queue.front();
    queue.pop_front();
    out.push_back(tree.label[n]);
    queue.insert(queue.end(), tree.children[n].begin(), tree.children[n].end());
  }
  return out;
}
std::size_t height(const Tree& tree) { return tree_height(tree, 0); }

// ---- graphs -----------------------------------------------------------------------------------------

std::size_t Graph::node(const std::string& text) {
  const auto it = std::find(name.begin(), name.end(), text);
  if (it != name.end()) {
    return static_cast<std::size_t>(it - name.begin());
  }
  name.push_back(text);
  succ.emplace_back();
  return name.size() - 1;
}

void Graph::edge(const std::string& from, const std::string& to) {
  const auto a = node(from);
  const auto b = node(to);
  if (std::find(succ[a].begin(), succ[a].end(), b) == succ[a].end()) {
    succ[a].push_back(b);
  }
}

std::optional<Graph> parse_graph(const std::string& text, std::string* error) {
  Graph graph;
  std::istringstream lines(text);
  std::string line;
  std::size_t number = 0;
  while (std::getline(lines, line)) {
    ++number;
    if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
    std::istringstream words(line);
    std::string from;
    std::string arrow;
    if (!(words >> from)) continue;
    graph.node(from);
    if (!(words >> arrow)) continue;
    if (arrow != "->") {
      if (error) *error = "line " + std::to_string(number) + ": expected '->'";
      return std::nullopt;
    }
    std::string to;
    while (words >> to) graph.edge(from, to);
  }
  if (graph.name.empty()) {
    if (error) *error = "empty graph";
    return std::nullopt;
  }
  return graph;
}

bool DepthFirst::acyclic() const {
  return std::none_of(edges.begin(), edges.end(), [](const auto& e) { return e.second == EdgeKind::Back; });
}

DepthFirst depth_first(const Graph& graph, std::size_t root) {
  const std::size_t n = graph.name.size();
  DepthFirst result;
  std::vector<int> state(n, 0);  // 0 new, 1 on stack, 2 done
  std::vector<std::size_t> discover(n, 0);
  std::size_t clock = 0;
  std::function<void(std::size_t)> visit = [&](std::size_t v) {
    state[v] = 1;
    discover[v] = clock++;
    result.preorder.push_back(v);
    for (const auto w : graph.succ[v]) {
      EdgeKind kind;
      if (state[w] == 0) {
        kind = EdgeKind::Tree;
        result.edges.push_back({{v, w}, kind});
        visit(w);
        continue;
      }
      if (state[w] == 1) {
        kind = EdgeKind::Back;
      } else {
        kind = discover[w] > discover[v] ? EdgeKind::Forward : EdgeKind::Cross;
      }
      result.edges.push_back({{v, w}, kind});
    }
    state[v] = 2;
    result.postorder.push_back(v);
  };
  if (root < n) visit(root);
  for (std::size_t v = 0; v < n; ++v) {
    if (state[v] == 0) visit(v);
  }
  return result;
}

std::vector<std::size_t> breadth_first(const Graph& graph, std::size_t root) {
  const std::size_t n = graph.name.size();
  std::vector<bool> seen(n, false);
  std::vector<std::size_t> order;
  auto run = [&](std::size_t start) {
    std::deque<std::size_t> queue{start};
    seen[start] = true;
    while (!queue.empty()) {
      const auto v = queue.front();
      queue.pop_front();
      order.push_back(v);
      for (const auto w : graph.succ[v]) {
        if (!seen[w]) {
          seen[w] = true;
          queue.push_back(w);
        }
      }
    }
  };
  if (root < n) run(root);
  for (std::size_t v = 0; v < n; ++v) {
    if (!seen[v]) run(v);
  }
  return order;
}

std::optional<std::vector<std::size_t>> topological_order(const Graph& graph) {
  const std::size_t n = graph.name.size();
  std::vector<std::size_t> indegree(n, 0);
  for (const auto& out : graph.succ) {
    for (const auto w : out) ++indegree[w];
  }
  std::deque<std::size_t> ready;
  for (std::size_t v = 0; v < n; ++v) {
    if (indegree[v] == 0) ready.push_back(v);
  }
  std::vector<std::size_t> order;
  while (!ready.empty()) {
    const auto v = ready.front();
    ready.pop_front();
    order.push_back(v);
    for (const auto w : graph.succ[v]) {
      if (--indegree[w] == 0) ready.push_back(w);
    }
  }
  if (order.size() != n) return std::nullopt;
  return order;
}

std::vector<std::vector<std::size_t>> strongly_connected_components(const Graph& graph) {
  const std::size_t n = graph.name.size();
  constexpr std::size_t unset = static_cast<std::size_t>(-1);
  std::vector<std::size_t> index(n, unset);
  std::vector<std::size_t> low(n, 0);
  std::vector<bool> on_stack(n, false);
  std::vector<std::size_t> stack;
  std::vector<std::vector<std::size_t>> components;
  std::size_t counter = 0;
  std::function<void(std::size_t)> strong = [&](std::size_t v) {
    index[v] = low[v] = counter++;
    stack.push_back(v);
    on_stack[v] = true;
    for (const auto w : graph.succ[v]) {
      if (index[w] == unset) {
        strong(w);
        low[v] = std::min(low[v], low[w]);
      } else if (on_stack[w]) {
        low[v] = std::min(low[v], index[w]);
      }
    }
    if (low[v] == index[v]) {
      std::vector<std::size_t> component;
      std::size_t w;
      do {
        w = stack.back();
        stack.pop_back();
        on_stack[w] = false;
        component.push_back(w);
      } while (w != v);
      std::sort(component.begin(), component.end());
      components.push_back(std::move(component));
    }
  };
  for (std::size_t v = 0; v < n; ++v) {
    if (index[v] == unset) strong(v);
  }
  return components;
}

std::vector<std::vector<bool>> transitive_closure(const Graph& graph) {
  const std::size_t n = graph.name.size();
  std::vector<std::vector<bool>> reach(n, std::vector<bool>(n, false));
  for (std::size_t v = 0; v < n; ++v) {
    reach[v][v] = true;
    for (const auto w : graph.succ[v]) reach[v][w] = true;
  }
  for (std::size_t k = 0; k < n; ++k) {
    for (std::size_t i = 0; i < n; ++i) {
      if (!reach[i][k]) continue;
      for (std::size_t j = 0; j < n; ++j) {
        if (reach[k][j]) reach[i][j] = true;
      }
    }
  }
  return reach;
}

std::vector<std::size_t> closure(std::vector<std::size_t> seed,
                                 const std::function<std::vector<std::size_t>(std::size_t)>& step) {
  std::vector<std::size_t> result;
  std::vector<std::size_t> work = std::move(seed);
  while (!work.empty()) {
    const auto x = work.back();
    work.pop_back();
    if (std::find(result.begin(), result.end(), x) != result.end()) continue;
    result.push_back(x);
    for (const auto y : step(x)) work.push_back(y);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::string edge_kind_name(EdgeKind kind) {
  switch (kind) {
    case EdgeKind::Tree: return "tree";
    case EdgeKind::Back: return "back";
    case EdgeKind::Forward: return "forward";
    case EdgeKind::Cross: return "cross";
  }
  return "?";
}

std::string report(const Graph& graph) {
  std::ostringstream out;
  auto names = [&](const std::vector<std::size_t>& ids) {
    std::string s;
    for (const auto id : ids) s += (s.empty() ? "" : " ") + graph.name[id];
    return s;
  };
  const auto dfs = depth_first(graph);
  out << "nodes: " << graph.name.size() << '\n';
  out << "DFS preorder:  " << names(dfs.preorder) << '\n';
  out << "DFS postorder: " << names(dfs.postorder) << '\n';
  std::vector<std::size_t> rpo(dfs.postorder.rbegin(), dfs.postorder.rend());
  out << "reverse postorder: " << names(rpo) << '\n';
  out << "edges:";
  for (const auto& [e, kind] : dfs.edges) {
    out << ' ' << graph.name[e.first] << "->" << graph.name[e.second] << '(' << edge_kind_name(kind) << ')';
  }
  out << '\n';
  out << "BFS: " << names(breadth_first(graph)) << '\n';
  if (const auto topo = topological_order(graph)) {
    out << "topological order: " << names(*topo) << '\n';
  } else {
    out << "topological order: none (the graph has a cycle)\n";
  }
  out << "strongly connected components:";
  for (const auto& c : strongly_connected_components(graph)) out << " {" << names(c) << '}';
  out << '\n';
  const auto reach = transitive_closure(graph);
  out << "transitive closure (Warshall):\n";
  for (std::size_t i = 0; i < reach.size(); ++i) {
    std::vector<std::size_t> row;
    for (std::size_t j = 0; j < reach.size(); ++j) {
      if (reach[i][j]) row.push_back(j);
    }
    out << "  " << graph.name[i] << " reaches " << names(row) << '\n';
  }
  return out.str();
}

}  // namespace nexus::compiler::foundations
