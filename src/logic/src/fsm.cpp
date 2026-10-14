#include "nexus/logic/fsm.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include "nexus/logic/boolean.hpp"
#include "nexus/logic/codes.hpp"

namespace nexus::logic {

namespace {

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r");
  if (b == std::string::npos) {
    return "";
  }
  const auto e = s.find_last_not_of(" \t\r");
  return s.substr(b, e - b + 1);
}

std::vector<std::string> split(const std::string& s) {
  std::istringstream in(s);
  std::vector<std::string> out;
  std::string t;
  while (in >> t) {
    out.push_back(t);
  }
  return out;
}

std::string bits(std::uint32_t v, std::size_t width) {
  std::string s;
  for (std::size_t i = 0; i < width; ++i) {
    s += ((v >> (width - 1 - i)) & 1U) != 0 ? '1' : '0';
  }
  return s;
}

std::uint32_t parse_bits(const std::string& s, std::size_t width) {
  if (s.size() != width) {
    throw std::runtime_error("fsm: expected " + std::to_string(width) + " bits, got '" + s + "'");
  }
  std::uint32_t v = 0;
  for (const char c : s) {
    if (c != '0' && c != '1') {
      throw std::runtime_error("fsm: bad bit string '" + s + "'");
    }
    v = (v << 1U) | static_cast<std::uint32_t>(c - '0');
  }
  return v;
}

// all input values matching a pattern of 0/1/-
std::vector<std::uint32_t> expand(const std::string& pattern, std::size_t width) {
  if (pattern.size() != width) {
    throw std::runtime_error("fsm: input pattern width");
  }
  std::vector<std::uint32_t> out;
  for (std::uint32_t v = 0; v < (1U << width); ++v) {
    bool ok = true;
    for (std::size_t i = 0; i < width; ++i) {
      const char c = pattern[i];
      const bool b = ((v >> (width - 1 - i)) & 1U) != 0;
      if ((c == '0' && b) || (c == '1' && !b) || (c != '0' && c != '1' && c != '-')) {
        ok = false;
      }
    }
    if (ok) {
      out.push_back(v);
    }
  }
  return out;
}

constexpr std::size_t kUnset = std::numeric_limits<std::size_t>::max();

bool cover_value(const std::vector<Cube>& cover, std::uint32_t minterm) {
  return std::any_of(cover.begin(), cover.end(), [&](const Cube& c) { return c.covers(minterm); });
}

}  // namespace

Fsm parse_fsm(const std::string& text) {
  Fsm f;
  std::istringstream in(text);
  std::string line;
  std::string reset_name;
  bool header = false;
  std::map<std::string, std::size_t> index;
  struct Row {
    std::string state, pattern, next;
    bool has_output = false;
    std::string out;
  };
  std::vector<Row> rows;
  std::vector<std::pair<std::string, std::string>> moore_outputs;
  auto state_id = [&](const std::string& name) {
    const auto it = index.find(name);
    if (it != index.end()) {
      return it->second;
    }
    index.emplace(name, f.states.size());
    f.states.push_back(name);
    return f.states.size() - 1;
  };
  while (std::getline(in, line)) {
    const auto hash = line.find('#');
    if (hash != std::string::npos) {
      line = line.substr(0, hash);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    auto tok = split(line);
    if (tok[0] == "fsm") {
      header = true;
      if (tok.size() < 3) {
        throw std::runtime_error("fsm: header needs a name and mealy|moore");
      }
      f.name = tok[1];
      f.moore = tok[2] == "moore";
      for (std::size_t i = 3; i < tok.size(); ++i) {
        const auto eq = tok[i].find('=');
        const std::string key = tok[i].substr(0, eq);
        const std::string val = tok[i].substr(eq + 1);
        if (key == "inputs") {
          f.input_bits = static_cast<std::size_t>(std::stoul(val));
        } else if (key == "outputs") {
          f.output_bits = static_cast<std::size_t>(std::stoul(val));
        } else if (key == "reset") {
          reset_name = val;
        }
      }
      continue;
    }
    if (!header) {
      throw std::runtime_error("fsm: missing 'fsm' header line");
    }
    if (tok.size() == 3 && tok[1] == "/") {
      moore_outputs.emplace_back(tok[0], tok[2]);
      state_id(tok[0]);
      continue;
    }
    if (tok.size() >= 4 && tok[2] == "->") {
      Row r{tok[0], tok[1], tok[3], false, ""};
      if (tok.size() == 6 && tok[4] == "/") {
        r.has_output = true;
        r.out = tok[5];
      }
      state_id(r.state);
      state_id(r.next);
      rows.push_back(r);
      continue;
    }
    throw std::runtime_error("fsm: cannot parse line '" + line + "'");
  }
  const std::size_t n = f.states.size();
  const std::size_t inputs = std::size_t{1} << f.input_bits;
  f.next.assign(n, std::vector<std::size_t>(inputs, kUnset));
  f.output.assign(n, std::vector<std::uint32_t>(inputs, 0));
  for (const auto& r : rows) {
    const std::size_t s = index.at(r.state);
    for (const auto v : expand(r.pattern, f.input_bits)) {
      f.next[s][v] = index.at(r.next);
      if (!f.moore) {
        if (!r.has_output) {
          throw std::runtime_error("fsm: Mealy transition without output");
        }
        f.output[s][v] = parse_bits(r.out, f.output_bits);
      }
    }
  }
  for (const auto& [name, out] : moore_outputs) {
    const std::uint32_t v = parse_bits(out, f.output_bits);
    for (auto& o : f.output[index.at(name)]) {
      o = v;
    }
  }
  for (std::size_t s = 0; s < n; ++s) {
    for (std::size_t v = 0; v < inputs; ++v) {
      if (f.next[s][v] == kUnset) {
        throw std::runtime_error("fsm: state " + f.states[s] + " has no transition for input " +
                                 bits(static_cast<std::uint32_t>(v), f.input_bits));
      }
    }
  }
  f.reset = reset_name.empty() ? 0 : index.at(reset_name);
  return f;
}

std::string format_fsm(const Fsm& f) {
  std::ostringstream out;
  out << "fsm " << f.name << ' ' << (f.moore ? "moore" : "mealy") << " inputs=" << f.input_bits
      << " outputs=" << f.output_bits << " reset=" << f.states[f.reset] << '\n';
  for (std::size_t s = 0; s < f.states.size(); ++s) {
    if (f.moore) {
      out << f.states[s] << " / " << bits(f.output[s][0], f.output_bits) << '\n';
    }
    for (std::size_t v = 0; v < f.next[s].size(); ++v) {
      out << f.states[s] << ' ' << bits(static_cast<std::uint32_t>(v), f.input_bits) << " -> "
          << f.states[f.next[s][v]];
      if (!f.moore) {
        out << " / " << bits(f.output[s][v], f.output_bits);
      }
      out << '\n';
    }
  }
  return out.str();
}

std::string format_state_table(const Fsm& f) {
  std::ostringstream out;
  const std::size_t inputs = std::size_t{1} << f.input_bits;
  out << "present";
  for (std::size_t v = 0; v < inputs; ++v) {
    out << " | x=" << bits(static_cast<std::uint32_t>(v), f.input_bits);
  }
  if (f.moore) {
    out << " | out";
  }
  out << '\n';
  for (std::size_t s = 0; s < f.states.size(); ++s) {
    out << (s == f.reset ? "*" : " ") << f.states[s];
    for (std::size_t v = 0; v < inputs; ++v) {
      out << " | " << f.states[f.next[s][v]];
      if (!f.moore) {
        out << '/' << bits(f.output[s][v], f.output_bits);
      }
    }
    if (f.moore) {
      out << " | " << bits(f.output[s][0], f.output_bits);
    }
    out << '\n';
  }
  return out.str();
}

std::vector<std::uint32_t> simulate(const Fsm& f, const std::vector<std::uint32_t>& inputs) {
  std::vector<std::uint32_t> out;
  std::size_t s = f.reset;
  for (const auto in : inputs) {
    out.push_back(f.output[s][in]);
    s = f.next[s][in];
  }
  return out;
}

bool fsm_equivalent(const Fsm& a, const Fsm& b, std::vector<std::uint32_t>* witness) {
  if (a.input_bits != b.input_bits || a.output_bits != b.output_bits) {
    return false;
  }
  const std::size_t inputs = std::size_t{1} << a.input_bits;
  using Pair = std::pair<std::size_t, std::size_t>;
  std::map<Pair, std::pair<Pair, std::uint32_t>> parent;
  std::deque<Pair> queue;
  const Pair start{a.reset, b.reset};
  parent[start] = {start, 0};
  queue.push_back(start);
  while (!queue.empty()) {
    const Pair p = queue.front();
    queue.pop_front();
    for (std::uint32_t v = 0; v < inputs; ++v) {
      if (a.output[p.first][v] != b.output[p.second][v]) {
        if (witness != nullptr) {
          witness->clear();
          witness->push_back(v);
          for (Pair q = p; q != start; q = parent[q].first) {
            witness->push_back(parent[q].second);
          }
          std::reverse(witness->begin(), witness->end());
        }
        return false;
      }
      const Pair nxt{a.next[p.first][v], b.next[p.second][v]};
      if (parent.count(nxt) == 0) {
        parent[nxt] = {p, v};
        queue.push_back(nxt);
      }
    }
  }
  return true;
}

Fsm minimize_fsm(const Fsm& f, std::vector<std::size_t>* block_of) {
  const std::size_t n = f.states.size();
  const std::size_t inputs = std::size_t{1} << f.input_bits;
  // reachable states
  std::vector<bool> reach(n, false);
  std::deque<std::size_t> q{f.reset};
  reach[f.reset] = true;
  while (!q.empty()) {
    const auto s = q.front();
    q.pop_front();
    for (const auto t : f.next[s]) {
      if (!reach[t]) {
        reach[t] = true;
        q.push_back(t);
      }
    }
  }
  // initial partition: equal output rows (Moore: equal state output)
  std::vector<std::size_t> block(n, kUnset);
  {
    std::map<std::vector<std::uint32_t>, std::size_t> by_out;
    for (std::size_t s = 0; s < n; ++s) {
      if (!reach[s]) {
        continue;
      }
      const auto it = by_out.emplace(f.output[s], by_out.size()).first;
      block[s] = it->second;
    }
  }
  // refine until stable: states stay together only if their successors are in the same blocks
  for (;;) {
    std::map<std::vector<std::size_t>, std::size_t> sig;
    std::vector<std::size_t> nb(n, kUnset);
    for (std::size_t s = 0; s < n; ++s) {
      if (!reach[s]) {
        continue;
      }
      std::vector<std::size_t> key{block[s]};
      for (std::size_t v = 0; v < inputs; ++v) {
        key.push_back(block[f.next[s][v]]);
      }
      nb[s] = sig.emplace(key, sig.size()).first->second;
    }
    const std::size_t old_count =
        std::set<std::size_t>(block.begin(), block.end()).size() - (std::count(block.begin(), block.end(), kUnset) > 0 ? 1 : 0);
    block = nb;
    if (sig.size() == old_count) {
      break;
    }
  }
  // build the minimised machine with blocks numbered in order of first state
  std::map<std::size_t, std::size_t> renumber;
  std::vector<std::size_t> rep;
  for (std::size_t s = 0; s < n; ++s) {
    if (reach[s] && renumber.count(block[s]) == 0) {
      renumber.emplace(block[s], rep.size());
      rep.push_back(s);
    }
  }
  Fsm m;
  m.name = f.name + "_min";
  m.moore = f.moore;
  m.input_bits = f.input_bits;
  m.output_bits = f.output_bits;
  for (const auto s : rep) {
    std::string name = f.states[s];
    for (std::size_t t = 0; t < n; ++t) {
      if (t != s && reach[t] && block[t] == block[s]) {
        name += "_" + f.states[t];
      }
    }
    m.states.push_back(name);
    std::vector<std::size_t> row;
    for (std::size_t v = 0; v < inputs; ++v) {
      row.push_back(renumber.at(block[f.next[s][v]]));
    }
    m.next.push_back(row);
    m.output.push_back(f.output[s]);
  }
  m.reset = renumber.at(block[f.reset]);
  if (block_of != nullptr) {
    block_of->assign(n, kUnset);
    for (std::size_t s = 0; s < n; ++s) {
      if (reach[s]) {
        (*block_of)[s] = renumber.at(block[s]);
      }
    }
  }
  return m;
}

std::vector<std::uint32_t> state_codes(std::size_t num_states, Encoding encoding, std::size_t* bits_out) {
  std::size_t k = 0;
  if (encoding == Encoding::OneHot) {
    k = num_states;
  } else {
    while ((std::size_t{1} << k) < num_states) {
      ++k;
    }
    k = std::max<std::size_t>(k, 1);
  }
  std::vector<std::uint32_t> codes;
  for (std::size_t s = 0; s < num_states; ++s) {
    const auto v = static_cast<std::uint32_t>(s);
    switch (encoding) {
      case Encoding::Binary:
        codes.push_back(v);
        break;
      case Encoding::Gray:
        codes.push_back(gray_encode(v));
        break;
      case Encoding::OneHot:
        codes.push_back(1U << v);
        break;
    }
  }
  if (bits_out != nullptr) {
    *bits_out = k;
  }
  return codes;
}

std::size_t SequentialCircuit::literals() const {
  std::size_t total = 0;
  for (const auto& cover : excite) {
    for (const auto& c : cover) {
      total += literal_count(c);
    }
  }
  for (const auto& cover : outputs) {
    for (const auto& c : cover) {
      total += literal_count(c);
    }
  }
  return total;
}

std::string SequentialCircuit::equations() const {
  std::ostringstream out;
  const char* names[] = {"D", "T", "JK"};
  out << "flip-flops: " << names[static_cast<int>(flip_flop)] << ", state bits: " << state_bits << '\n';
  for (std::size_t i = state_bits; i-- > 0;) {
    if (flip_flop == FlipFlop::JK) {
      out << "  J" << i << " = " << sop_to_string(excite[2 * i], vars) << '\n';
      out << "  K" << i << " = " << sop_to_string(excite[2 * i + 1], vars) << '\n';
    } else {
      out << "  " << (flip_flop == FlipFlop::D ? "D" : "T") << i << " = " << sop_to_string(excite[i], vars) << '\n';
    }
  }
  for (std::size_t o = output_bits; o-- > 0;) {
    out << "  z" << o << " = " << sop_to_string(outputs[o], vars) << '\n';
  }
  out << "  literals: " << literals() << '\n';
  return out.str();
}

SequentialCircuit synthesize(const Fsm& f, Encoding encoding, FlipFlop flip_flop) {
  if (encoding == Encoding::OneHot && flip_flop != FlipFlop::D) {
    throw std::runtime_error("synthesize: one-hot encoding uses D flip-flops");
  }
  SequentialCircuit c;
  c.encoding = encoding;
  c.flip_flop = flip_flop;
  c.input_bits = f.input_bits;
  c.output_bits = f.output_bits;
  c.moore = f.moore;
  c.codes = state_codes(f.states.size(), encoding, &c.state_bits);
  c.reset_code = c.codes[f.reset];
  const std::size_t k = c.state_bits;
  const std::size_t m = f.input_bits;
  const std::size_t n = k + m;
  if (encoding != Encoding::OneHot && n > 12) {
    throw std::runtime_error("synthesize: too many state and input bits for exact two-level minimisation");
  }
  for (std::size_t i = k; i-- > 0;) {
    c.vars.push_back("q" + std::to_string(i));
  }
  for (std::size_t i = m; i-- > 0;) {
    c.vars.push_back("x" + std::to_string(i));
  }
  const std::uint32_t inputs = 1U << m;

  if (encoding == Encoding::OneHot) {
    // D_i = OR over transitions (s, in) -> i of q_s AND (minimised input condition); outputs alike
    c.excite.assign(k, {});
    c.outputs.assign(f.output_bits, {});
    const std::uint32_t input_mask = inputs - 1U;
    auto add_terms = [&](std::vector<Cube>& cover, std::size_t s, const std::vector<std::uint32_t>& ins) {
      if (ins.empty()) {
        return;
      }
      const auto r = minimize_sop(m, ins);
      const std::uint32_t qbit = 1U << (s + m);  // state bit s sits above the input bits
      for (const auto& cube : r.cover.cubes) {
        cover.push_back(Cube{qbit | (cube.value & input_mask), qbit | (cube.care & input_mask)});
      }
    };
    for (std::size_t s = 0; s < f.states.size(); ++s) {
      std::map<std::size_t, std::vector<std::uint32_t>> to;
      for (std::uint32_t v = 0; v < inputs; ++v) {
        to[f.next[s][v]].push_back(v);
      }
      for (const auto& [t, ins] : to) {
        add_terms(c.excite[t], s, ins);
      }
      for (std::size_t o = 0; o < f.output_bits; ++o) {
        std::vector<std::uint32_t> ins;
        for (std::uint32_t v = 0; v < inputs; ++v) {
          if (((f.output[s][v] >> o) & 1U) != 0) {
            ins.push_back(v);
          }
        }
        add_terms(c.outputs[o], s, ins);
      }
    }
    return c;
  }

  std::map<std::uint32_t, std::size_t> state_of_code;
  for (std::size_t s = 0; s < c.codes.size(); ++s) {
    state_of_code[c.codes[s]] = s;
  }
  const std::size_t functions = flip_flop == FlipFlop::JK ? 2 * k : k;
  std::vector<std::vector<std::uint32_t>> ones(functions), dcs(functions);
  std::vector<std::vector<std::uint32_t>> out_ones(f.output_bits), out_dcs(f.output_bits);
  for (std::uint32_t code = 0; code < (1U << k); ++code) {
    const auto it = state_of_code.find(code);
    for (std::uint32_t v = 0; v < inputs; ++v) {
      const std::uint32_t minterm = (code << m) | v;
      if (it == state_of_code.end()) {
        for (auto& d : dcs) {
          d.push_back(minterm);
        }
        for (auto& d : out_dcs) {
          d.push_back(minterm);
        }
        continue;
      }
      const std::size_t s = it->second;
      const std::uint32_t nxt = c.codes[f.next[s][v]];
      for (std::size_t i = 0; i < k; ++i) {
        const bool q = ((code >> i) & 1U) != 0;
        const bool qn = ((nxt >> i) & 1U) != 0;
        switch (flip_flop) {
          case FlipFlop::D:
            if (qn) {
              ones[i].push_back(minterm);
            }
            break;
          case FlipFlop::T:
            if (q != qn) {
              ones[i].push_back(minterm);
            }
            break;
          case FlipFlop::JK:
            // excitation table: 0->0 J=0 K=x; 0->1 J=1 K=x; 1->0 J=x K=1; 1->1 J=x K=0
            if (!q) {
              if (qn) {
                ones[2 * i].push_back(minterm);
              }
              dcs[2 * i + 1].push_back(minterm);
            } else {
              dcs[2 * i].push_back(minterm);
              if (!qn) {
                ones[2 * i + 1].push_back(minterm);
              }
            }
            break;
        }
      }
      for (std::size_t o = 0; o < f.output_bits; ++o) {
        if (((f.output[s][v] >> o) & 1U) != 0) {
          out_ones[o].push_back(minterm);
        }
      }
    }
  }
  for (std::size_t i = 0; i < functions; ++i) {
    c.excite.push_back(minimize_sop(n, ones[i], dcs[i]).cover.cubes);
  }
  for (std::size_t o = 0; o < f.output_bits; ++o) {
    c.outputs.push_back(minimize_sop(n, out_ones[o], out_dcs[o]).cover.cubes);
  }
  return c;
}

std::uint32_t next_code(const SequentialCircuit& c, std::uint32_t code, std::uint32_t input) {
  const std::uint32_t minterm = (code << c.input_bits) | input;
  std::uint32_t nxt = 0;
  for (std::size_t i = 0; i < c.state_bits; ++i) {
    const bool q = ((code >> i) & 1U) != 0;
    bool qn = false;
    switch (c.flip_flop) {
      case FlipFlop::D:
        qn = cover_value(c.excite[i], minterm);
        break;
      case FlipFlop::T:
        qn = q != cover_value(c.excite[i], minterm);
        break;
      case FlipFlop::JK: {
        const bool j = cover_value(c.excite[2 * i], minterm);
        const bool kk = cover_value(c.excite[2 * i + 1], minterm);
        qn = (j && !q) || (!kk && q);
        break;
      }
    }
    if (qn) {
      nxt |= 1U << i;
    }
  }
  return nxt;
}

std::uint32_t output_of(const SequentialCircuit& c, std::uint32_t code, std::uint32_t input) {
  const std::uint32_t minterm = (code << c.input_bits) | input;
  std::uint32_t out = 0;
  for (std::size_t o = 0; o < c.output_bits; ++o) {
    if (cover_value(c.outputs[o], minterm)) {
      out |= 1U << o;
    }
  }
  return out;
}

std::vector<std::uint32_t> simulate(const SequentialCircuit& c, const std::vector<std::uint32_t>& inputs) {
  std::vector<std::uint32_t> out;
  std::uint32_t code = c.reset_code;
  for (const auto in : inputs) {
    out.push_back(output_of(c, code, in));
    code = next_code(c, code, in);
  }
  return out;
}

Fsm analyze(const SequentialCircuit& c) {
  Fsm f;
  f.name = "analysed";
  f.moore = c.moore;
  f.input_bits = c.input_bits;
  f.output_bits = c.output_bits;
  const std::uint32_t inputs = 1U << c.input_bits;
  std::map<std::uint32_t, std::size_t> id;
  std::deque<std::uint32_t> q{c.reset_code};
  id[c.reset_code] = 0;
  std::vector<std::uint32_t> order{c.reset_code};
  while (!q.empty()) {
    const auto code = q.front();
    q.pop_front();
    for (std::uint32_t v = 0; v < inputs; ++v) {
      const auto nxt = next_code(c, code, v);
      if (id.count(nxt) == 0) {
        id[nxt] = order.size();
        order.push_back(nxt);
        q.push_back(nxt);
      }
    }
  }
  for (const auto code : order) {
    f.states.push_back("S" + bits(code, c.state_bits));
    std::vector<std::size_t> row;
    std::vector<std::uint32_t> outs;
    for (std::uint32_t v = 0; v < inputs; ++v) {
      row.push_back(id.at(next_code(c, code, v)));
      outs.push_back(output_of(c, code, v));
    }
    f.next.push_back(row);
    f.output.push_back(outs);
  }
  f.reset = 0;
  return f;
}

SequentialCircuit parse_circuit(const std::string& text) {
  SequentialCircuit c;
  std::istringstream in(text);
  std::string line;
  std::vector<std::string> state_names, input_names;
  std::map<std::string, std::string> eq;  // "D q1" -> expression
  std::vector<std::string> out_exprs;
  std::string reset_bits;
  while (std::getline(in, line)) {
    const auto hash = line.find('#');
    if (hash != std::string::npos) {
      line = line.substr(0, hash);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    const auto tok = split(line);
    if (tok[0] == "flipflop") {
      c.flip_flop = tok[1] == "T" ? FlipFlop::T : (tok[1] == "JK" ? FlipFlop::JK : FlipFlop::D);
    } else if (tok[0] == "state") {
      state_names.assign(tok.begin() + 1, tok.end());
    } else if (tok[0] == "inputs") {
      input_names.assign(tok.begin() + 1, tok.end());
    } else if (tok[0] == "reset") {
      reset_bits = tok[1];
    } else if (tok.size() >= 4 && tok[2] == "=") {
      const std::string expr = trim(line.substr(line.find('=') + 1));
      if (tok[0] == "out") {
        out_exprs.push_back(expr);
      } else {
        eq[tok[0] + " " + tok[1]] = expr;
      }
    } else {
      throw std::runtime_error("circuit: cannot parse '" + line + "'");
    }
  }
  c.state_bits = state_names.size();
  c.input_bits = input_names.size();
  c.output_bits = out_exprs.size();
  c.vars = state_names;
  c.vars.insert(c.vars.end(), input_names.begin(), input_names.end());
  const std::size_t n = c.vars.size();
  auto cover_of = [&](const std::string& expr) {
    const Function fn = parse(expr, c.vars);
    std::vector<std::uint32_t> ones;
    for (const auto mt : minterms(truth_table(fn))) {
      ones.push_back(static_cast<std::uint32_t>(mt));
    }
    return minimize_sop(n, ones).cover.cubes;
  };
  const std::string ff = c.flip_flop == FlipFlop::JK ? "" : (c.flip_flop == FlipFlop::T ? "T" : "D");
  c.excite.resize(c.flip_flop == FlipFlop::JK ? 2 * c.state_bits : c.state_bits);
  for (std::size_t idx = 0; idx < c.state_bits; ++idx) {
    const std::string& q = state_names[idx];
    const std::size_t bit = c.state_bits - 1 - idx;  // the first listed state variable is the MSB
    if (c.flip_flop == FlipFlop::JK) {
      c.excite[2 * bit] = cover_of(eq.at("J " + q));
      c.excite[2 * bit + 1] = cover_of(eq.at("K " + q));
    } else {
      c.excite[bit] = cover_of(eq.at(ff + " " + q));
    }
  }
  for (std::size_t o = 0; o < out_exprs.size(); ++o) {
    c.outputs.push_back(cover_of(out_exprs[out_exprs.size() - 1 - o]));  // listed MSB first
  }
  c.reset_code = reset_bits.empty() ? 0 : parse_bits(reset_bits, c.state_bits);
  return c;
}

std::string to_verilog_behavioural(const Fsm& f) {
  std::ostringstream v;
  std::size_t k = 1;
  while ((std::size_t{1} << k) < f.states.size()) {
    ++k;
  }
  const std::size_t inputs = std::size_t{1} << f.input_bits;
  v << "// generated by nexus-eda from FSM '" << f.name << "' (behavioural)\n";
  v << "module " << f.name << "_behav(input wire clk, input wire rst, input wire [" << f.input_bits - 1
    << ":0] x, output reg [" << f.output_bits - 1 << ":0] z);\n";
  v << "  reg [" << k - 1 << ":0] state, next;\n";
  v << "  always @(posedge clk) state <= rst ? " << k << "'d" << f.reset << " : next;\n";
  v << "  always @* begin\n    next = state;\n    z = 0;\n    case (state)\n";
  for (std::size_t s = 0; s < f.states.size(); ++s) {
    v << "      " << k << "'d" << s << ": case (x)\n";
    for (std::size_t in = 0; in < inputs; ++in) {
      v << "        " << f.input_bits << "'d" << in << ": begin next = " << k << "'d" << f.next[s][in] << "; z = "
        << f.output_bits << "'d" << f.output[s][in] << "; end\n";
    }
    v << "        default: ;\n      endcase\n";
  }
  v << "      default: ;\n    endcase\n  end\nendmodule\n";
  return v.str();
}

namespace {
std::string sop_verilog(const std::vector<Cube>& cover, const std::vector<std::string>& vars) {
  if (cover.empty()) {
    return "1'b0";
  }
  std::string out;
  for (std::size_t t = 0; t < cover.size(); ++t) {
    if (t > 0) {
      out += " | ";
    }
    const std::size_t n = vars.size();
    std::string term;
    for (std::size_t i = 0; i < n; ++i) {
      const std::uint32_t bit = 1U << (n - 1 - i);
      if ((cover[t].care & bit) == 0) {
        continue;
      }
      if (!term.empty()) {
        term += " & ";
      }
      term += ((cover[t].value & bit) != 0 ? "" : "~") + vars[i];
    }
    out += term.empty() ? "1'b1" : "(" + term + ")";
  }
  return out;
}
}  // namespace

std::string to_verilog_structural(const SequentialCircuit& c, const std::string& module_name) {
  std::ostringstream v;
  const std::size_t k = c.state_bits;
  v << "// generated by nexus-eda (structural: minimised excitation and output equations)\n";
  v << "module " << module_name << "(input wire clk, input wire rst, input wire [" << c.input_bits - 1
    << ":0] x, output wire [" << c.output_bits - 1 << ":0] z);\n";
  v << "  reg [" << k - 1 << ":0] q;\n";
  for (std::size_t i = 0; i < k; ++i) {
    v << "  wire q" << i << " = q[" << i << "];\n";
  }
  for (std::size_t i = 0; i < c.input_bits; ++i) {
    v << "  wire x" << i << " = x[" << i << "];\n";
  }
  for (std::size_t i = 0; i < k; ++i) {
    if (c.flip_flop == FlipFlop::JK) {
      v << "  wire j" << i << " = " << sop_verilog(c.excite[2 * i], c.vars) << ";\n";
      v << "  wire k" << i << " = " << sop_verilog(c.excite[2 * i + 1], c.vars) << ";\n";
    } else {
      v << "  wire e" << i << " = " << sop_verilog(c.excite[i], c.vars) << ";\n";
    }
  }
  for (std::size_t o = 0; o < c.output_bits; ++o) {
    v << "  assign z[" << o << "] = " << sop_verilog(c.outputs[o], c.vars) << ";\n";
  }
  v << "  always @(posedge clk) begin\n    if (rst) q <= " << k << "'d" << c.reset_code << ";\n    else begin\n";
  for (std::size_t i = 0; i < k; ++i) {
    switch (c.flip_flop) {
      case FlipFlop::D:
        v << "      q[" << i << "] <= e" << i << ";\n";
        break;
      case FlipFlop::T:
        v << "      q[" << i << "] <= q" << i << " ^ e" << i << ";\n";
        break;
      case FlipFlop::JK:
        v << "      q[" << i << "] <= (j" << i << " & ~q" << i << ") | (~k" << i << " & q" << i << ");\n";
        break;
    }
  }
  v << "    end\n  end\nendmodule\n";
  return v.str();
}

}  // namespace nexus::logic
