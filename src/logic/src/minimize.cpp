#include "nexus/logic/minimize.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace nexus::logic {

namespace {

std::uint32_t full_mask(std::size_t n) { return n >= 32 ? 0xFFFFFFFFU : ((1U << n) - 1U); }

struct Cost {
  std::size_t cubes = 0;
  std::size_t literals = 0;
  bool operator<(const Cost& o) const { return cubes != o.cubes ? cubes < o.cubes : literals < o.literals; }
};

Cost cost_of(const std::vector<Cube>& cubes) {
  Cost c{cubes.size(), 0};
  for (const auto& cube : cubes) {
    c.literals += literal_count(cube);
  }
  return c;
}

}  // namespace

std::size_t literal_count(const Cube& c) { return static_cast<std::size_t>(std::popcount(c.care)); }

std::string cube_to_string(const Cube& c, const std::vector<std::string>& vars) {
  const std::size_t n = vars.size();
  std::string out;
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint32_t bit = 1U << (n - 1 - i);
    if ((c.care & bit) == 0) {
      continue;
    }
    if (!out.empty()) {
      out += ' ';
    }
    out += vars[i];
    if ((c.value & bit) == 0) {
      out += '\'';
    }
  }
  return out.empty() ? "1" : out;
}

std::string cube_to_pattern(const Cube& c, std::size_t n) {
  std::string out;
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint32_t bit = 1U << (n - 1 - i);
    out += (c.care & bit) == 0 ? '-' : ((c.value & bit) != 0 ? '1' : '0');
  }
  return out;
}

std::string sop_to_string(const std::vector<Cube>& cover, const std::vector<std::string>& vars) {
  if (cover.empty()) {
    return "0";
  }
  std::string out;
  for (std::size_t i = 0; i < cover.size(); ++i) {
    if (i > 0) {
      out += " + ";
    }
    out += cube_to_string(cover[i], vars);
  }
  return out;
}

std::string pos_to_string(const std::vector<Cube>& zeros_cover, const std::vector<std::string>& vars) {
  if (zeros_cover.empty()) {
    return "1";
  }
  const std::size_t n = vars.size();
  std::string out;
  for (const auto& c : zeros_cover) {
    if (c.care == 0) {
      return "0";
    }
    std::string term;
    for (std::size_t i = 0; i < n; ++i) {
      const std::uint32_t bit = 1U << (n - 1 - i);
      if ((c.care & bit) == 0) {
        continue;
      }
      if (!term.empty()) {
        term += " + ";
      }
      term += vars[i];
      if ((c.value & bit) != 0) {
        term += '\'';  // the sum term is the complement of the product that covers the zeros
      }
    }
    out += "(" + term + ")";
  }
  return out;
}

QmResult quine_mccluskey(std::size_t n, const std::vector<std::uint32_t>& ones,
                         const std::vector<std::uint32_t>& dont_cares) {
  if (n > 16) {
    throw std::runtime_error("quine_mccluskey: at most 16 variables");
  }
  const std::uint32_t mask = full_mask(n);
  std::set<std::uint32_t> seed(ones.begin(), ones.end());
  seed.insert(dont_cares.begin(), dont_cares.end());

  QmResult result;
  std::vector<Cube> current;
  for (const auto m : seed) {
    current.push_back(Cube{m & mask, mask});
  }
  std::set<std::pair<std::uint32_t, std::uint32_t>> primes;
  while (!current.empty()) {
    QmColumn column;
    column.groups.resize(n + 1);
    for (const auto& c : current) {
      column.groups[static_cast<std::size_t>(std::popcount(c.value))].push_back(c);
    }
    result.columns.push_back(column);

    std::vector<bool> used(current.size(), false);
    std::set<std::pair<std::uint32_t, std::uint32_t>> next;
    for (std::size_t i = 0; i < current.size(); ++i) {
      for (std::size_t j = i + 1; j < current.size(); ++j) {
        const Cube& a = current[i];
        const Cube& b = current[j];
        if (a.care != b.care) {
          continue;
        }
        const std::uint32_t diff = a.value ^ b.value;
        if (std::popcount(diff) != 1) {
          continue;
        }
        used[i] = used[j] = true;
        next.insert({a.value & ~diff, a.care & ~diff});
      }
    }
    for (std::size_t i = 0; i < current.size(); ++i) {
      if (!used[i]) {
        primes.insert({current[i].value, current[i].care});
      }
    }
    current.clear();
    for (const auto& [v, c] : next) {
      current.push_back(Cube{v, c});
    }
  }
  for (const auto& [v, c] : primes) {
    const Cube cube{v, c};
    // a prime that covers only don't cares is never needed
    if (std::any_of(ones.begin(), ones.end(), [&](std::uint32_t m) { return cube.covers(m); })) {
      result.primes.push_back(cube);
    }
  }
  return result;
}

namespace {

struct Reduced {
  std::vector<Cube> essential;
  std::vector<Cube> rest;           // non-essential primes still useful
  std::vector<std::uint32_t> left;  // minterms not covered by the essential primes
};

Reduced take_essentials(const std::vector<Cube>& primes, const std::vector<std::uint32_t>& ones) {
  Reduced r;
  std::set<std::size_t> essential_idx;
  for (const auto m : ones) {
    std::size_t count = 0;
    std::size_t last = 0;
    for (std::size_t p = 0; p < primes.size(); ++p) {
      if (primes[p].covers(m)) {
        ++count;
        last = p;
      }
    }
    if (count == 0) {
      throw std::runtime_error("minimum_cover: a minterm is covered by no prime");
    }
    if (count == 1) {
      essential_idx.insert(last);
    }
  }
  for (const auto idx : essential_idx) {
    r.essential.push_back(primes[idx]);
  }
  for (const auto m : ones) {
    if (std::none_of(r.essential.begin(), r.essential.end(), [&](const Cube& c) { return c.covers(m); })) {
      r.left.push_back(m);
    }
  }
  for (std::size_t p = 0; p < primes.size(); ++p) {
    if (essential_idx.count(p) != 0) {
      continue;
    }
    if (std::any_of(r.left.begin(), r.left.end(), [&](std::uint32_t m) { return primes[p].covers(m); })) {
      r.rest.push_back(primes[p]);
    }
  }
  return r;
}

std::vector<Cube> petrick_core(const std::vector<Cube>& rest, const std::vector<std::uint32_t>& left) {
  if (left.empty()) {
    return {};
  }
  if (rest.size() > 30) {
    throw std::runtime_error("petrick: too many primes");
  }
  // product of sums over minterms; each product term is a set of primes (bitmask); absorption keeps it small
  std::vector<std::uint32_t> product{0};
  for (const auto m : left) {
    std::vector<std::uint32_t> next;
    for (const auto term : product) {
      for (std::size_t p = 0; p < rest.size(); ++p) {
        if (rest[p].covers(m)) {
          next.push_back(term | (1U << p));
        }
      }
    }
    std::sort(next.begin(), next.end());
    next.erase(std::unique(next.begin(), next.end()), next.end());
    std::vector<std::uint32_t> absorbed;
    for (const auto t : next) {
      const bool dominated = std::any_of(next.begin(), next.end(), [&](std::uint32_t o) {
        return o != t && (o & t) == o;  // o is a proper subset of t: t is absorbed
      });
      if (!dominated) {
        absorbed.push_back(t);
      }
    }
    product = std::move(absorbed);
  }
  std::vector<Cube> best;
  Cost best_cost{~std::size_t{0}, 0};
  for (const auto t : product) {
    std::vector<Cube> cand;
    for (std::size_t p = 0; p < rest.size(); ++p) {
      if ((t & (1U << p)) != 0) {
        cand.push_back(rest[p]);
      }
    }
    const Cost c = cost_of(cand);
    if (c < best_cost) {
      best_cost = c;
      best = cand;
    }
  }
  return best;
}

std::vector<Cube> bnb_core(const std::vector<Cube>& rest, const std::vector<std::uint32_t>& left) {
  std::vector<Cube> best;
  Cost best_cost{~std::size_t{0}, 0};
  std::vector<Cube> chosen;
  std::function<void(const std::vector<std::uint32_t>&)> search = [&](const std::vector<std::uint32_t>& todo) {
    const Cost here = cost_of(chosen);
    if (todo.empty()) {
      if (here < best_cost) {
        best_cost = here;
        best = chosen;
      }
      return;
    }
    // lower bound: at least one more cube
    if (!(Cost{here.cubes + 1, here.literals} < best_cost)) {
      return;
    }
    // branch on the minterm with the fewest covering primes
    std::size_t pick = 0;
    std::size_t fewest = ~std::size_t{0};
    for (std::size_t i = 0; i < todo.size(); ++i) {
      const auto k = static_cast<std::size_t>(
          std::count_if(rest.begin(), rest.end(), [&](const Cube& c) { return c.covers(todo[i]); }));
      if (k < fewest) {
        fewest = k;
        pick = i;
      }
    }
    for (const auto& c : rest) {
      if (!c.covers(todo[pick])) {
        continue;
      }
      std::vector<std::uint32_t> remaining;
      for (const auto m : todo) {
        if (!c.covers(m)) {
          remaining.push_back(m);
        }
      }
      chosen.push_back(c);
      search(remaining);
      chosen.pop_back();
    }
  };
  search(left);
  return best;
}

Cover assemble(const Reduced& r, std::vector<Cube> extra, CoverMethod method) {
  Cover cover;
  cover.method = method;
  cover.essential = r.essential;
  cover.cubes = r.essential;
  cover.cubes.insert(cover.cubes.end(), extra.begin(), extra.end());
  cover.literals = cost_of(cover.cubes).literals;
  return cover;
}

}  // namespace

Cover petrick(std::size_t /*n*/, const std::vector<Cube>& primes, const std::vector<std::uint32_t>& ones) {
  const Reduced r = take_essentials(primes, ones);
  return assemble(r, petrick_core(r.rest, r.left), CoverMethod::Petrick);
}

Cover branch_and_bound(std::size_t /*n*/, const std::vector<Cube>& primes, const std::vector<std::uint32_t>& ones) {
  const Reduced r = take_essentials(primes, ones);
  return assemble(r, bnb_core(r.rest, r.left), CoverMethod::BranchAndBound);
}

Cover minimum_cover(std::size_t n, const std::vector<Cube>& primes, const std::vector<std::uint32_t>& ones,
                    std::size_t petrick_limit) {
  if (ones.empty()) {
    return Cover{};
  }
  const Reduced r = take_essentials(primes, ones);
  if (r.rest.size() <= petrick_limit) {
    return petrick(n, primes, ones);
  }
  return branch_and_bound(n, primes, ones);
}

MinimizeResult minimize_sop(std::size_t n, const std::vector<std::uint32_t>& ones,
                            const std::vector<std::uint32_t>& dont_cares) {
  MinimizeResult r;
  r.qm = quine_mccluskey(n, ones, dont_cares);
  r.cover = minimum_cover(n, r.qm.primes, ones);
  return r;
}

MinimizeResult minimize_pos(std::size_t n, const std::vector<std::uint32_t>& ones,
                            const std::vector<std::uint32_t>& dont_cares) {
  std::set<std::uint32_t> taken(ones.begin(), ones.end());
  taken.insert(dont_cares.begin(), dont_cares.end());
  std::vector<std::uint32_t> zeros;
  for (std::uint32_t m = 0; m <= full_mask(n); ++m) {
    if (taken.count(m) == 0) {
      zeros.push_back(m);
    }
    if (m == full_mask(n)) {
      break;
    }
  }
  return minimize_sop(n, zeros, dont_cares);
}

bool cover_implements(std::size_t n, const std::vector<Cube>& cover, const std::vector<std::uint32_t>& ones,
                      const std::vector<std::uint32_t>& dont_cares) {
  const std::set<std::uint32_t> on(ones.begin(), ones.end());
  const std::set<std::uint32_t> dc(dont_cares.begin(), dont_cares.end());
  for (std::uint32_t m = 0;; ++m) {
    const bool value = std::any_of(cover.begin(), cover.end(), [&](const Cube& c) { return c.covers(m); });
    if (dc.count(m) == 0 && value != (on.count(m) != 0)) {
      return false;
    }
    if (m == full_mask(n)) {
      break;
    }
  }
  return true;
}

std::string karnaugh_map(const std::vector<std::string>& vars, const std::vector<std::uint8_t>& values,
                         const std::vector<Cube>& cover) {
  const std::size_t n = vars.size();
  if (n < 2 || n > 4 || values.size() != (std::size_t{1} << n)) {
    throw std::runtime_error("karnaugh_map: 2 to 4 variables");
  }
  const std::size_t row_bits = n / 2;
  const std::size_t col_bits = n - row_bits;
  auto gray = [](std::size_t bits) {
    std::vector<std::uint32_t> g;
    for (std::uint32_t i = 0; i < (1U << bits); ++i) {
      g.push_back(i ^ (i >> 1));
    }
    return g;
  };
  const auto rows = gray(row_bits);
  const auto cols = gray(col_bits);
  auto bits_str = [](std::uint32_t v, std::size_t width) {
    std::string s;
    for (std::size_t i = 0; i < width; ++i) {
      s += ((v >> (width - 1 - i)) & 1U) != 0 ? '1' : '0';
    }
    return s;
  };
  std::string row_label;
  for (std::size_t i = 0; i < row_bits; ++i) {
    row_label += vars[i];
  }
  std::string col_label;
  for (std::size_t i = row_bits; i < n; ++i) {
    col_label += vars[i];
  }
  std::ostringstream out;
  const std::string pad(std::max<std::size_t>(row_label.size(), 2), ' ');
  out << pad << " \\ " << col_label << '\n' << std::string(pad.size(), ' ') << "   ";
  for (const auto c : cols) {
    out << ' ' << bits_str(c, col_bits) << std::string(col_bits < 2 ? 2 : 1, ' ');
  }
  out << "\n";
  for (const auto r : rows) {
    out << std::string(pad.size() - row_bits, ' ') << bits_str(r, row_bits) << " | ";
    for (const auto c : cols) {
      const std::uint32_t m = (r << col_bits) | c;
      std::string cell(1, values[m] == 2 ? '-' : static_cast<char>('0' + values[m]));
      std::string groups;
      for (std::size_t g = 0; g < cover.size(); ++g) {
        if (cover[g].covers(m)) {
          groups += static_cast<char>('A' + static_cast<char>(g % 26));
        }
      }
      cell += groups.empty() ? std::string(1, ' ') : groups.substr(0, 1);
      out << cell << std::string(col_bits < 2 ? 2 : 2, ' ');
    }
    out << '\n';
  }
  out << "row: " << row_label << ", column: " << col_label << " (Gray order); letter = first group covering the cell\n";
  for (std::size_t g = 0; g < cover.size(); ++g) {
    out << "  " << static_cast<char>('A' + static_cast<char>(g % 26)) << " = " << cube_to_string(cover[g], vars)
        << "  cells";
    for (std::uint32_t m = 0; m < values.size(); ++m) {
      if (cover[g].covers(m)) {
        out << " m" << m;
      }
    }
    out << '\n';
  }
  return out.str();
}

}  // namespace nexus::logic
