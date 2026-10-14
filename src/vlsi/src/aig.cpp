#include "nexus/vlsi/aig.hpp"

#include <algorithm>
#include <functional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

#include "nexus/logic/sat.hpp"

namespace nexus::vlsi {

Aig::Aig() {
  fanin0_.push_back(0);
  fanin1_.push_back(0);
  is_input_.push_back(false);
}

Lit Aig::add_input(const std::string& name) {
  const auto node = fanin0_.size();
  fanin0_.push_back(0);
  fanin1_.push_back(0);
  is_input_.push_back(true);
  inputs_.push_back(node);
  input_names_.push_back(name);
  return static_cast<Lit>(node << 1U);
}

void Aig::add_output(const std::string& name, Lit l) {
  outputs_.push_back(l);
  output_names_.push_back(name);
}

Lit Aig::and_(Lit a, Lit b) {
  if (a > b) {
    std::swap(a, b);
  }
  if (a == kLitFalse || a == lit_not(b)) {
    return kLitFalse;
  }
  if (a == kLitTrue || a == b) {
    return b;
  }
  const auto key = std::make_pair(a, b);
  const auto it = strash_.find(key);
  if (it != strash_.end()) {
    return it->second;
  }
  const auto node = fanin0_.size();
  fanin0_.push_back(a);
  fanin1_.push_back(b);
  is_input_.push_back(false);
  const Lit l = static_cast<Lit>(node << 1U);
  strash_.emplace(key, l);
  return l;
}

Lit Aig::from_expr(const nexus::logic::Expr& e, const std::vector<Lit>& vars) {
  using K = nexus::logic::Expr::Kind;
  switch (e.kind) {
    case K::Const:
      return e.value ? kLitTrue : kLitFalse;
    case K::Var:
      return vars.at(e.var);
    case K::Not:
      return lit_not(from_expr(*e.kids[0], vars));
    case K::And:
      return and_(from_expr(*e.kids[0], vars), from_expr(*e.kids[1], vars));
    case K::Or:
      return or_(from_expr(*e.kids[0], vars), from_expr(*e.kids[1], vars));
    case K::Xor:
      return xor_(from_expr(*e.kids[0], vars), from_expr(*e.kids[1], vars));
  }
  return kLitFalse;
}

std::size_t Aig::num_ands() const {
  std::size_t n = 0;
  for (std::size_t i = 1; i < fanin0_.size(); ++i) {
    if (!is_input_[i]) {
      ++n;
    }
  }
  return n;
}

std::size_t Aig::depth() const {
  std::vector<std::size_t> level(fanin0_.size(), 0);
  for (std::size_t i = 1; i < fanin0_.size(); ++i) {
    if (!is_input_[i]) {
      level[i] = 1 + std::max(level[lit_node(fanin0_[i])], level[lit_node(fanin1_[i])]);
    }
  }
  std::size_t d = 0;
  for (const auto o : outputs_) {
    d = std::max(d, level[lit_node(o)]);
  }
  return d;
}

std::vector<std::uint64_t> Aig::simulate_nodes(const std::vector<std::uint64_t>& in) const {
  if (in.size() != inputs_.size()) {
    throw std::runtime_error("aig simulate: wrong number of input words");
  }
  std::vector<std::uint64_t> v(fanin0_.size(), 0);
  for (std::size_t i = 0; i < inputs_.size(); ++i) {
    v[inputs_[i]] = in[i];
  }
  auto val = [&](Lit l) { return lit_neg(l) ? ~v[lit_node(l)] : v[lit_node(l)]; };
  for (std::size_t i = 1; i < fanin0_.size(); ++i) {
    if (!is_input_[i]) {
      v[i] = val(fanin0_[i]) & val(fanin1_[i]);  // nodes are created in topological order
    }
  }
  return v;
}

std::vector<std::uint64_t> Aig::simulate(const std::vector<std::uint64_t>& in) const {
  const auto v = simulate_nodes(in);
  std::vector<std::uint64_t> out;
  for (const auto o : outputs_) {
    out.push_back(lit_neg(o) ? ~v[lit_node(o)] : v[lit_node(o)]);
  }
  return out;
}

Aig Aig::cleanup() const {
  Aig out;
  std::vector<Lit> map(fanin0_.size(), kLitFalse);
  for (std::size_t i = 0; i < inputs_.size(); ++i) {
    map[inputs_[i]] = out.add_input(input_names_[i]);
  }
  std::vector<bool> used(fanin0_.size(), false);
  std::function<void(std::size_t)> mark = [&](std::size_t n) {
    if (used[n]) {
      return;
    }
    used[n] = true;
    if (is_and(n)) {
      mark(lit_node(fanin0_[n]));
      mark(lit_node(fanin1_[n]));
    }
  };
  for (const auto o : outputs_) {
    mark(lit_node(o));
  }
  auto m = [&](Lit l) { return map[lit_node(l)] ^ (l & 1U); };
  for (std::size_t i = 1; i < fanin0_.size(); ++i) {
    if (used[i] && is_and(i)) {
      map[i] = out.and_(m(fanin0_[i]), m(fanin1_[i]));
    }
  }
  for (std::size_t i = 0; i < outputs_.size(); ++i) {
    out.add_output(output_names_[i], m(outputs_[i]));
  }
  return out;
}

Aig Aig::balance() const {
  Aig out;
  std::vector<Lit> map(fanin0_.size(), kLitFalse);
  std::vector<std::size_t> level_of_new;  // level per node of `out`
  for (std::size_t i = 0; i < inputs_.size(); ++i) {
    map[inputs_[i]] = out.add_input(input_names_[i]);
  }
  // fanout counts: a super-gate stops at nodes used more than once or complemented
  std::vector<std::size_t> refs(fanin0_.size(), 0);
  for (std::size_t i = 1; i < fanin0_.size(); ++i) {
    if (is_and(i)) {
      ++refs[lit_node(fanin0_[i])];
      ++refs[lit_node(fanin1_[i])];
    }
  }
  for (const auto o : outputs_) {
    ++refs[lit_node(o)];
  }
  std::vector<std::size_t> new_level(1, 0);
  auto level = [&](Lit l) {
    const auto n = lit_node(l);
    return n < new_level.size() ? new_level[n] : 0;
  };
  auto record = [&](Lit l, std::size_t lv) {
    const auto n = lit_node(l);
    if (new_level.size() <= n) {
      new_level.resize(n + 1, 0);
    }
    new_level[n] = std::max(new_level[n], lv);
  };
  std::vector<bool> done(fanin0_.size(), false);
  std::function<Lit(std::size_t)> build = [&](std::size_t n) -> Lit {
    if (!is_and(n)) {
      return map[n];
    }
    if (done[n]) {
      return map[n];
    }
    // collect the leaves of the AND super-gate rooted at n
    std::vector<Lit> leaves;
    std::function<void(Lit, bool)> collect = [&](Lit l, bool root) {
      const auto c = lit_node(l);
      if (!root && (lit_neg(l) || !is_and(c) || refs[c] > 1)) {
        const Lit nl = build(c) ^ (l & 1U);
        leaves.push_back(nl);
        return;
      }
      collect(fanin0_[c], false);
      collect(fanin1_[c], false);
    };
    collect(static_cast<Lit>(n << 1U), true);
    // combine the two shallowest repeatedly (Huffman-like) for minimum depth
    while (leaves.size() > 1) {
      std::sort(leaves.begin(), leaves.end(), [&](Lit a, Lit b) { return level(a) > level(b); });
      const Lit a = leaves.back();
      leaves.pop_back();
      const Lit b = leaves.back();
      leaves.pop_back();
      const Lit r = out.and_(a, b);
      record(r, 1 + std::max(level(a), level(b)));
      leaves.push_back(r);
    }
    done[n] = true;
    map[n] = leaves.empty() ? kLitTrue : leaves[0];
    return map[n];
  };
  for (std::size_t i = 0; i < outputs_.size(); ++i) {
    const Lit o = outputs_[i];
    const Lit nl = build(lit_node(o)) ^ (o & 1U);
    out.add_output(output_names_[i], nl);
  }
  (void)level_of_new;
  return out;
}

// ---------------------------------------------------------------- BLIF

namespace {

std::vector<std::string> tokens(const std::string& s) {
  std::istringstream in(s);
  std::vector<std::string> t;
  std::string x;
  while (in >> x) {
    t.push_back(x);
  }
  return t;
}

}  // namespace

Aig read_blif(const std::string& text) {
  // join continuation lines, drop comments
  std::vector<std::string> lines;
  {
    std::istringstream in(text);
    std::string line, acc;
    while (std::getline(in, line)) {
      const auto hash = line.find('#');
      if (hash != std::string::npos) {
        line = line.substr(0, hash);
      }
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
      }
      if (!line.empty() && line.back() == '\\') {
        acc += line.substr(0, line.size() - 1) + " ";
        continue;
      }
      acc += line;
      if (!tokens(acc).empty()) {
        lines.push_back(acc);
      }
      acc.clear();
    }
  }
  struct Names {
    std::vector<std::string> ins;
    std::string out;
    std::vector<std::pair<std::string, char>> rows;
  };
  std::vector<std::string> inputs, outputs;
  std::map<std::string, Names> defs;
  std::vector<std::pair<std::string, std::string>> latches;  // (data input, output)
  for (std::size_t i = 0; i < lines.size(); ++i) {
    auto t = tokens(lines[i]);
    if (t[0] == ".model" || t[0] == ".end") {
      continue;
    }
    if (t[0] == ".inputs") {
      inputs.insert(inputs.end(), t.begin() + 1, t.end());
    } else if (t[0] == ".outputs") {
      outputs.insert(outputs.end(), t.begin() + 1, t.end());
    } else if (t[0] == ".latch") {
      latches.emplace_back(t.at(1), t.at(2));
    } else if (t[0] == ".names") {
      Names n;
      n.ins.assign(t.begin() + 1, t.end() - 1);
      n.out = t.back();
      while (i + 1 < lines.size() && lines[i + 1][0] != '.') {
        const auto r = tokens(lines[++i]);
        if (n.ins.empty()) {
          n.rows.emplace_back("", r.at(0)[0]);
        } else {
          n.rows.emplace_back(r.at(0), r.at(1)[0]);
        }
      }
      defs[n.out] = n;
    } else if (t[0] == ".subckt" || t[0] == ".gate") {
      throw std::runtime_error("blif: .subckt/.gate not supported (flatten and map to .names first)");
    }
  }
  Aig aig;
  std::map<std::string, Lit> sig;
  for (const auto& name : inputs) {
    sig[name] = aig.add_input(name);
  }
  for (const auto& [d, q] : latches) {
    sig[q] = aig.add_input(q);
  }
  std::set<std::string> visiting;
  std::function<Lit(const std::string&)> get = [&](const std::string& name) -> Lit {
    const auto it = sig.find(name);
    if (it != sig.end()) {
      return it->second;
    }
    const auto d = defs.find(name);
    if (d == defs.end()) {
      throw std::runtime_error("blif: undriven signal " + name);
    }
    if (!visiting.insert(name).second) {
      throw std::runtime_error("blif: combinational loop through " + name);
    }
    const Names& n = d->second;
    std::vector<Lit> in;
    for (const auto& s : n.ins) {
      in.push_back(get(s));
    }
    // sum of the rows; rows with output 0 describe the off-set
    Lit sum = kLitFalse;
    bool offset = false;
    for (const auto& [cube, val] : n.rows) {
      Lit prod = kLitTrue;
      for (std::size_t k = 0; k < cube.size(); ++k) {
        if (cube[k] == '1') {
          prod = aig.and_(prod, in[k]);
        } else if (cube[k] == '0') {
          prod = aig.and_(prod, lit_not(in[k]));
        }
      }
      sum = aig.or_(sum, prod);
      offset = val == '0';
    }
    const Lit r = offset ? lit_not(sum) : sum;
    visiting.erase(name);
    sig[name] = r;
    return r;
  };
  for (const auto& name : outputs) {
    aig.add_output(name, get(name));
  }
  for (const auto& [d, q] : latches) {
    aig.add_output(q + "$next", get(d));
  }
  return aig;
}

std::string write_blif(const Aig& aig, const std::string& model) {
  std::ostringstream out;
  auto name = [&](std::size_t node) -> std::string {
    if (node == 0) {
      return "const0";
    }
    if (aig.is_input(node)) {
      const auto it = std::find(aig.inputs().begin(), aig.inputs().end(), node);
      return aig.input_names()[static_cast<std::size_t>(it - aig.inputs().begin())];
    }
    return "n" + std::to_string(node);
  };
  out << ".model " << model << "\n.inputs";
  for (const auto& n : aig.input_names()) {
    out << ' ' << n;
  }
  out << "\n.outputs";
  for (const auto& n : aig.output_names()) {
    out << ' ' << n;
  }
  out << "\n.names const0\n";
  for (std::size_t i = 1; i < aig.num_nodes(); ++i) {
    if (!aig.is_and(i)) {
      continue;
    }
    const Lit a = aig.fanin0(i), b = aig.fanin1(i);
    out << ".names " << name(lit_node(a)) << ' ' << name(lit_node(b)) << ' ' << name(i) << '\n'
        << (lit_neg(a) ? '0' : '1') << (lit_neg(b) ? '0' : '1') << " 1\n";
  }
  for (std::size_t i = 0; i < aig.outputs().size(); ++i) {
    const Lit o = aig.outputs()[i];
    out << ".names " << name(lit_node(o)) << ' ' << aig.output_names()[i] << '\n' << (lit_neg(o) ? '0' : '1') << " 1\n";
  }
  out << ".end\n";
  return out.str();
}

// ---------------------------------------------------------------- equivalence

namespace {

// Tseitin clauses of an AIG; returns the SAT literal of every node (inputs shared through `input_vars`).
std::vector<int> encode(const Aig& g, const std::vector<int>& input_vars, nexus::logic::SatSolver& s) {
  std::vector<int> var(g.num_nodes(), 0);
  var[0] = s.new_var();
  s.add_clause({-var[0]});  // constant false
  for (std::size_t i = 0; i < g.inputs().size(); ++i) {
    var[g.inputs()[i]] = input_vars[i];
  }
  auto lit = [&](Lit l) { return lit_neg(l) ? -var[lit_node(l)] : var[lit_node(l)]; };
  for (std::size_t n = 1; n < g.num_nodes(); ++n) {
    if (!g.is_and(n)) {
      continue;
    }
    var[n] = s.new_var();
    const int a = lit(g.fanin0(n)), b = lit(g.fanin1(n)), y = var[n];
    s.add_clause({-y, a});
    s.add_clause({-y, b});
    s.add_clause({y, -a, -b});
  }
  return var;
}

}  // namespace

AigEquivalence aig_equivalent(const Aig& a, const Aig& b) {
  AigEquivalence r;
  if (a.input_names().size() != b.input_names().size()) {
    r.mismatch = "different input counts";
    return r;
  }
  // map b's inputs and outputs onto a's by name
  std::vector<std::size_t> b_in_of_a;
  for (const auto& n : a.input_names()) {
    const auto it = std::find(b.input_names().begin(), b.input_names().end(), n);
    if (it == b.input_names().end()) {
      r.mismatch = "input " + n + " missing";
      return r;
    }
    b_in_of_a.push_back(static_cast<std::size_t>(it - b.input_names().begin()));
  }
  // random simulation first
  std::mt19937_64 rng(705);
  for (int round = 0; round < 32; ++round) {
    std::vector<std::uint64_t> wa(a.num_inputs()), wb(b.num_inputs());
    for (std::size_t i = 0; i < wa.size(); ++i) {
      wa[i] = rng();
      wb[b_in_of_a[i]] = wa[i];
    }
    const auto oa = a.simulate(wa);
    const auto ob = b.simulate(wb);
    for (std::size_t o = 0; o < a.output_names().size(); ++o) {
      const auto it = std::find(b.output_names().begin(), b.output_names().end(), a.output_names()[o]);
      if (it == b.output_names().end()) {
        r.mismatch = "output " + a.output_names()[o] + " missing";
        return r;
      }
      if (oa[o] != ob[static_cast<std::size_t>(it - b.output_names().begin())]) {
        r.mismatch = "output " + a.output_names()[o] + " differs in random simulation";
        return r;
      }
    }
  }
  // SAT miter per output
  for (std::size_t o = 0; o < a.output_names().size(); ++o) {
    nexus::logic::SatSolver s;
    std::vector<int> in_a;
    for (std::size_t i = 0; i < a.num_inputs(); ++i) {
      in_a.push_back(s.new_var());
    }
    std::vector<int> in_b(b.num_inputs());
    for (std::size_t i = 0; i < a.num_inputs(); ++i) {
      in_b[b_in_of_a[i]] = in_a[i];
    }
    const auto va = encode(a, in_a, s);
    const auto vb = encode(b, in_b, s);
    const auto bo = static_cast<std::size_t>(
        std::find(b.output_names().begin(), b.output_names().end(), a.output_names()[o]) - b.output_names().begin());
    const Lit la = a.outputs()[o], lb = b.outputs()[bo];
    const int x = lit_neg(la) ? -va[lit_node(la)] : va[lit_node(la)];
    const int y = lit_neg(lb) ? -vb[lit_node(lb)] : vb[lit_node(lb)];
    s.add_clause({x, y});    // x != y
    s.add_clause({-x, -y});
    const auto res = s.solve();
    r.conflicts += s.conflicts();
    if (res == nexus::logic::SatSolver::Result::Sat) {
      std::string cex = "output " + a.output_names()[o] + " differs for";
      for (std::size_t i = 0; i < a.num_inputs(); ++i) {
        cex += " " + a.input_names()[i] + "=" + (s.value(in_a[i]) ? "1" : "0");
      }
      r.mismatch = cex;
      return r;
    }
  }
  r.equivalent = true;
  return r;
}

}  // namespace nexus::vlsi
