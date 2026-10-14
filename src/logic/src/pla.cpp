#include "nexus/logic/pla.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

namespace nexus::logic {

namespace {

struct Target {
  std::vector<bool> on;       // minterms that must be covered
  std::vector<bool> allowed;  // on-set plus don't cares: a usable term stays inside it
};

std::vector<Target> targets_for(std::size_t n, const std::vector<MultiOutput>& outputs,
                                const std::vector<bool>& complemented) {
  const std::size_t size = std::size_t{1} << n;
  std::vector<Target> out;
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    std::vector<bool> one(size, false), dc(size, false);
    for (const auto m : outputs[i].ones) one.at(m) = true;
    for (const auto m : outputs[i].dont_cares) dc.at(m) = true;
    Target t;
    t.on.assign(size, false);
    t.allowed.assign(size, false);
    for (std::size_t m = 0; m < size; ++m) {
      const bool value = complemented[i] ? (!one[m] && !dc[m]) : one[m];
      t.on[m] = value;
      t.allowed[m] = value || dc[m];
    }
    out.push_back(std::move(t));
  }
  return out;
}

bool inside(const Cube& c, const std::vector<bool>& allowed) {
  for (std::size_t m = 0; m < allowed.size(); ++m) {
    if (c.covers(static_cast<std::uint32_t>(m)) && !allowed[m]) return false;
  }
  return true;
}

struct Search {
  std::vector<std::vector<std::size_t>> covers_of_element;  // element -> candidate terms
  std::vector<std::vector<std::size_t>> elements_of_term;
  std::vector<std::size_t> best;
  std::size_t nodes = 0;
  std::size_t node_limit = 2000000;
  bool exact = true;

  void run(std::vector<std::size_t>& chosen, std::vector<int>& covered_count) {
    if (++nodes > node_limit) {
      exact = false;
      return;
    }
    if (!best.empty() && chosen.size() >= best.size()) {
      return;  // cannot beat the best cover found so far
    }
    // the uncovered element with the fewest candidates
    std::size_t pick = covers_of_element.size();
    for (std::size_t e = 0; e < covers_of_element.size(); ++e) {
      if (covered_count[e] == 0 &&
          (pick == covers_of_element.size() || covers_of_element[e].size() < covers_of_element[pick].size())) {
        pick = e;
      }
    }
    if (pick == covers_of_element.size()) {
      if (best.empty() || chosen.size() < best.size()) best = chosen;
      return;
    }
    for (const auto t : covers_of_element[pick]) {
      chosen.push_back(t);
      for (const auto e : elements_of_term[t]) ++covered_count[e];
      run(chosen, covered_count);
      for (const auto e : elements_of_term[t]) --covered_count[e];
      chosen.pop_back();
      if (!exact) return;
    }
  }
};

Pla solve(std::size_t n, const std::vector<MultiOutput>& outputs, const std::vector<bool>& complemented) {
  const auto targets = targets_for(n, outputs, complemented);
  const std::size_t k = targets.size();
  const std::size_t size = std::size_t{1} << n;
  // candidates: primes of the product of every non-empty subset of outputs
  std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
  std::vector<Cube> candidates;
  for (std::uint32_t subset = 1; subset < (1U << k); ++subset) {
    std::vector<std::uint32_t> ones;
    for (std::size_t m = 0; m < size; ++m) {
      bool all = true;
      for (std::size_t i = 0; i < k && all; ++i) {
        if (((subset >> i) & 1U) != 0) all = targets[i].allowed[m];
      }
      if (all) ones.push_back(static_cast<std::uint32_t>(m));
    }
    if (ones.empty()) continue;
    for (const auto& p : quine_mccluskey(n, ones).primes) {
      if (seen.insert({p.value, p.care}).second) candidates.push_back(p);
    }
  }
  // elements: (output, minterm) pairs that must be covered
  std::vector<std::pair<std::size_t, std::size_t>> elements;
  for (std::size_t i = 0; i < k; ++i) {
    for (std::size_t m = 0; m < size; ++m) {
      if (targets[i].on[m]) elements.emplace_back(i, m);
    }
  }
  std::vector<std::vector<bool>> usable(candidates.size(), std::vector<bool>(k, false));
  Search s;
  s.covers_of_element.assign(elements.size(), {});
  s.elements_of_term.assign(candidates.size(), {});
  for (std::size_t t = 0; t < candidates.size(); ++t) {
    for (std::size_t i = 0; i < k; ++i) usable[t][i] = inside(candidates[t], targets[i].allowed);
    for (std::size_t e = 0; e < elements.size(); ++e) {
      const auto [i, m] = elements[e];
      if (usable[t][i] && candidates[t].covers(static_cast<std::uint32_t>(m))) {
        s.covers_of_element[e].push_back(t);
        s.elements_of_term[t].push_back(e);
      }
    }
  }
  std::vector<std::size_t> chosen;
  std::vector<int> count(elements.size(), 0);
  s.run(chosen, count);
  Pla pla;
  pla.inputs = n;
  pla.complemented = complemented;
  pla.exact = s.exact;
  // OR array: connect each output to the chosen rows it needs; drop rows an output can do without
  for (const auto t : s.best) {
    PlaTerm row;
    row.cube = candidates[t];
    row.outputs.assign(k, false);
    pla.terms.push_back(row);
  }
  for (std::size_t i = 0; i < k; ++i) {
    std::vector<int> cov(size, 0);
    for (std::size_t r = 0; r < pla.terms.size(); ++r) {
      if (usable[s.best[r]][i]) {
        pla.terms[r].outputs[i] = true;
        for (std::size_t m = 0; m < size; ++m) cov[m] += pla.terms[r].cube.covers(static_cast<std::uint32_t>(m)) ? 1 : 0;
      }
    }
    for (std::size_t r = 0; r < pla.terms.size(); ++r) {
      if (!pla.terms[r].outputs[i]) continue;
      bool needed = false;
      for (std::size_t m = 0; m < size && !needed; ++m) {
        needed = targets[i].on[m] && cov[m] == 1 && pla.terms[r].cube.covers(static_cast<std::uint32_t>(m));
      }
      if (!needed) {
        pla.terms[r].outputs[i] = false;
        for (std::size_t m = 0; m < size; ++m) cov[m] -= pla.terms[r].cube.covers(static_cast<std::uint32_t>(m)) ? 1 : 0;
      }
    }
  }
  for (const auto& row : pla.terms) {
    pla.connections += static_cast<std::size_t>(std::count(row.outputs.begin(), row.outputs.end(), true));
  }
  return pla;
}

}  // namespace

Pla minimize_pla(std::size_t n, const std::vector<MultiOutput>& outputs, bool allow_complement) {
  if (n == 0 || n > 10 || outputs.empty() || outputs.size() > 6) {
    throw std::invalid_argument("pla: 1-10 inputs and 1-6 outputs");
  }
  const std::size_t k = outputs.size();
  Pla best;
  bool have = false;
  for (std::uint32_t pol = 0; pol < (allow_complement ? (1U << k) : 1U); ++pol) {
    std::vector<bool> complemented(k);
    for (std::size_t i = 0; i < k; ++i) complemented[i] = ((pol >> i) & 1U) != 0;
    auto p = solve(n, outputs, complemented);
    if (!have || p.terms.size() < best.terms.size() ||
        (p.terms.size() == best.terms.size() && p.connections < best.connections)) {
      best = std::move(p);
      have = true;
    }
  }
  return best;
}

std::vector<std::size_t> pal_terms(std::size_t n, const std::vector<MultiOutput>& outputs) {
  std::vector<std::size_t> out;
  for (const auto& o : outputs) out.push_back(minimize_sop(n, o.ones, o.dont_cares).cover.cubes.size());
  return out;
}

bool pla_implements(const Pla& pla, const std::vector<MultiOutput>& outputs) {
  const std::size_t size = std::size_t{1} << pla.inputs;
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    std::vector<int> want(size, 0);  // 0, 1 or 2 (don't care)
    for (const auto m : outputs[i].ones) want[m] = 1;
    for (const auto m : outputs[i].dont_cares) want[m] = 2;
    for (std::size_t m = 0; m < size; ++m) {
      bool sum = false;
      for (const auto& row : pla.terms) sum = sum || (row.outputs[i] && row.cube.covers(static_cast<std::uint32_t>(m)));
      const bool value = pla.complemented[i] ? !sum : sum;
      if (want[m] != 2 && value != (want[m] == 1)) return false;
    }
  }
  return true;
}

std::string pla_table(const Pla& pla, const std::vector<std::string>& vars) {
  std::ostringstream out;
  const std::size_t k = pla.complemented.size();
  out << "row  product term        inputs  outputs\n";
  for (std::size_t r = 0; r < pla.terms.size(); ++r) {
    std::string name = cube_to_string(pla.terms[r].cube, vars);
    name.resize(std::max<std::size_t>(name.size(), 18), ' ');
    out << ' ' << r + 1 << (r + 1 < 10 ? "   " : "  ") << name << "  " << cube_to_pattern(pla.terms[r].cube, pla.inputs)
        << std::string(pla.inputs < 6 ? 6 - pla.inputs : 0, ' ') << "  ";
    for (std::size_t i = 0; i < k; ++i) out << (pla.terms[r].outputs[i] ? '1' : '-');
    out << '\n';
  }
  out << "     T/C" << std::string(19 + std::max<std::size_t>(pla.inputs, 6), ' ');
  for (std::size_t i = 0; i < k; ++i) out << (pla.complemented[i] ? 'C' : 'T');
  out << "\n(T: output taken true; C: complemented by the output XOR)\n";
  return out.str();
}

std::string rom_table(std::size_t n, const std::vector<MultiOutput>& outputs) {
  std::ostringstream out;
  const std::size_t size = std::size_t{1} << n;
  out << "ROM " << size << " x " << outputs.size() << " bits (address = input minterm; - = don't care)\n";
  for (std::size_t m = 0; m < size; ++m) {
    out << "  " << cube_to_pattern(Cube{static_cast<std::uint32_t>(m), static_cast<std::uint32_t>(size - 1)}, n) << "  ";
    for (const auto& o : outputs) {
      const bool one = std::find(o.ones.begin(), o.ones.end(), m) != o.ones.end();
      const bool dc = std::find(o.dont_cares.begin(), o.dont_cares.end(), m) != o.dont_cares.end();
      out << (one ? '1' : (dc ? '-' : '0'));
    }
    out << '\n';
  }
  return out.str();
}

}  // namespace nexus::logic
