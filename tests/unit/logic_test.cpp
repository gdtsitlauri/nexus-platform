// EY121 Logic Design: every algorithm is checked against an independent computation (truth tables, brute force).
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <iostream>
#include <random>
#include <set>
#include <string>

#include "nexus/logic/bdd.hpp"
#include "nexus/logic/boolean.hpp"
#include "nexus/logic/codes.hpp"
#include "nexus/logic/fsm.hpp"
#include "nexus/logic/gatesim.hpp"
#include "nexus/logic/minimize.hpp"
#include "nexus/logic/pla.hpp"
#include "nexus/logic/sat.hpp"

namespace lg = nexus::logic;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::mt19937 rng(121);

// random expression over n variables with the given depth
lg::ExprPtr random_expr(std::size_t n, int depth) {
  std::uniform_int_distribution<int> pick(0, depth <= 0 ? 1 : 5);
  const int k = pick(rng);
  if (k <= 1) {
    std::uniform_int_distribution<std::size_t> var(0, n - 1);
    lg::ExprPtr v = lg::make_var(var(rng));
    return k == 0 ? v : lg::make_not(v);
  }
  auto a = random_expr(n, depth - 1);
  auto b = random_expr(n, depth - 1);
  switch (k) {
    case 2:
      return lg::make_and(a, b);
    case 3:
      return lg::make_or(a, b);
    case 4:
      return lg::make_xor(a, b);
    default:
      return lg::make_not(lg::make_and(a, b));
  }
}

std::vector<std::string> names(std::size_t n) {
  std::vector<std::string> v;
  for (std::size_t i = 0; i < n; ++i) {
    v.push_back(std::string(1, static_cast<char>('a' + i)));
  }
  return v;
}

std::vector<std::uint8_t> table_of(const lg::ExprPtr& e, std::size_t n) {
  std::vector<std::uint8_t> t(std::size_t{1} << n);
  for (std::uint64_t m = 0; m < t.size(); ++m) {
    t[m] = lg::evaluate(*e, n, m) ? 1 : 0;
  }
  return t;
}

void boolean_algebra() {
  const auto f = lg::parse("a b' + c");
  expect(f.vars == std::vector<std::string>({"a", "b", "c"}), "variable order by appearance");
  const auto t = lg::truth_table(f);
  const std::vector<std::uint8_t> want{0, 1, 0, 1, 1, 1, 0, 1};
  expect(t == want, "truth table of a b' + c");
  expect(lg::minterms(t) == std::vector<std::uint64_t>({1, 3, 4, 5, 7}), "minterms");
  // canonical forms describe the same function
  const auto sop = lg::parse(lg::canonical_sop(f.vars, lg::minterms(t)), f.vars);
  const auto pos = lg::parse(lg::canonical_pos(f.vars, lg::maxterms(t)), f.vars);
  expect(lg::truth_table(sop) == t && lg::truth_table(pos) == t, "canonical SOP and POS");
  // De Morgan, absorption, consensus
  expect(lg::equivalent(lg::parse("(a b)'"), lg::parse("a' + b'")), "De Morgan 1");
  expect(lg::equivalent(lg::parse("(a + b)'"), lg::parse("a' b'")), "De Morgan 2");
  expect(lg::equivalent(lg::parse("a + a b"), lg::parse("a + 0 b")), "absorption");
  expect(lg::equivalent(lg::parse("a b + a' c + b c"), lg::parse("a b + a' c")), "consensus theorem");
  expect(lg::equivalent(lg::parse("a ^ b"), lg::parse("a b' + a' b")), "xor");
  // NAND-only and NOR-only rewriting of random expressions
  for (int trial = 0; trial < 300; ++trial) {
    const std::size_t n = 4;
    const auto e = random_expr(n, 4);
    const auto nand = lg::to_nand_only(e);
    const auto nor = lg::to_nor_only(e);
    expect(table_of(nand, n) == table_of(e, n) && lg::uses_only(*nand, true), "nand-only rewrite");
    expect(table_of(nor, n) == table_of(e, n) && lg::uses_only(*nor, false), "nor-only rewrite");
    // printing and parsing round-trips
    const auto again = lg::parse(lg::to_string(*e, names(n)), names(n));
    expect(table_of(again.expr, n) == table_of(e, n), "print/parse round trip");
  }
  bool threw = false;
  try {
    lg::parse("a + (b");
  } catch (const std::exception&) {
    threw = true;
  }
  expect(threw, "syntax error is reported");
}

// every implicant (cube that never covers a 0) is contained in some prime; every prime is maximal
void check_primes(std::size_t n, const std::vector<lg::Cube>& primes, const std::vector<std::uint32_t>& ones,
                  const std::vector<std::uint32_t>& dcs) {
  const std::set<std::uint32_t> on(ones.begin(), ones.end());
  const std::set<std::uint32_t> dc(dcs.begin(), dcs.end());
  const std::uint32_t full = (1U << n) - 1U;
  auto implicant = [&](const lg::Cube& c) {
    for (std::uint32_t m = 0; m <= full; ++m) {
      if (c.covers(m) && on.count(m) == 0 && dc.count(m) == 0) {
        return false;
      }
    }
    return true;
  };
  for (const auto& p : primes) {
    expect(implicant(p), "prime is an implicant");
    for (std::size_t i = 0; i < n; ++i) {
      const std::uint32_t bit = 1U << i;
      if ((p.care & bit) != 0) {
        expect(!implicant(lg::Cube{p.value & ~bit, p.care & ~bit}), "prime cannot be expanded");
      }
    }
  }
  // enumerate all 3^n cubes
  std::vector<std::uint32_t> digits(n, 0);
  std::function<void(std::size_t, std::uint32_t, std::uint32_t)> walk = [&](std::size_t i, std::uint32_t v,
                                                                            std::uint32_t c) {
    if (i == n) {
      const lg::Cube cube{v, c};
      bool covers_one = false;
      for (const auto m : ones) {
        covers_one |= cube.covers(m);
      }
      if (covers_one && implicant(cube)) {
        const bool contained = std::any_of(primes.begin(), primes.end(), [&](const lg::Cube& p) {
          return (p.care & cube.care) == p.care && (cube.value & p.care) == p.value;
        });
        expect(contained, "every implicant lies in a prime");
      }
      return;
    }
    const std::uint32_t bit = 1U << i;
    walk(i + 1, v, c);
    walk(i + 1, v, c | bit);
    walk(i + 1, v | bit, c | bit);
  };
  walk(0, 0, 0);
}

std::pair<std::size_t, std::size_t> cost(const std::vector<lg::Cube>& cubes) {
  std::size_t lits = 0;
  for (const auto& c : cubes) {
    lits += lg::literal_count(c);
  }
  return {cubes.size(), lits};
}

void minimisation() {
  // classic textbook example: f = sum m(4,8,10,11,12,15) + d(9,14) = b c' d' + a b' + a c
  const auto r = lg::minimize_sop(4, {4, 8, 10, 11, 12, 15}, {9, 14});
  expect(cost(r.cover.cubes) == std::make_pair(std::size_t{3}, std::size_t{7}), "textbook cover cost (3 terms, 7 literals)");
  expect(lg::cover_implements(4, r.cover.cubes, {4, 8, 10, 11, 12, 15}, {9, 14}), "textbook cover implements f");
  const auto map = lg::karnaugh_map(names(4), [] {
    std::vector<std::uint8_t> v(16, 0);
    for (int m : {4, 8, 10, 11, 12, 15}) v[static_cast<std::size_t>(m)] = 1;
    for (int m : {9, 14}) v[static_cast<std::size_t>(m)] = 2;
    return v;
  }(), r.cover.cubes);
  expect(map.find("A = ") != std::string::npos && map.find("cells") != std::string::npos, "Karnaugh map rendering");

  std::uniform_int_distribution<int> value(0, 5);
  for (int trial = 0; trial < 250; ++trial) {
    const std::size_t n = 3 + static_cast<std::size_t>(trial % 3);
    std::vector<std::uint32_t> ones, dcs;
    for (std::uint32_t m = 0; m < (1U << n); ++m) {
      const int v = value(rng);
      if (v <= 1) {
        ones.push_back(m);
      } else if (v == 2) {
        dcs.push_back(m);
      }
    }
    if (ones.empty()) {
      continue;
    }
    const auto qm = lg::quine_mccluskey(n, ones, dcs);
    check_primes(n, qm.primes, ones, dcs);
    const auto p = lg::petrick(n, qm.primes, ones);
    const auto b = lg::branch_and_bound(n, qm.primes, ones);
    expect(lg::cover_implements(n, p.cubes, ones, dcs), "Petrick cover implements f");
    expect(lg::cover_implements(n, b.cubes, ones, dcs), "branch-and-bound cover implements f");
    expect(cost(p.cubes) == cost(b.cubes), "Petrick and branch-and-bound agree on the optimum");
    // exhaustive optimum over subsets of the primes
    if (qm.primes.size() <= 16) {
      std::pair<std::size_t, std::size_t> best{99, 99};
      for (std::uint32_t s = 1; s < (1U << qm.primes.size()); ++s) {
        std::vector<lg::Cube> sub;
        for (std::size_t i = 0; i < qm.primes.size(); ++i) {
          if ((s >> i) & 1U) sub.push_back(qm.primes[i]);
        }
        if (lg::cover_implements(n, sub, ones, dcs)) {
          best = std::min(best, cost(sub));
        }
      }
      expect(cost(p.cubes) == best, "minimum cover equals the exhaustive optimum");
    }
    // product of sums
    const auto pos = lg::minimize_pos(n, ones, dcs);
    std::vector<std::uint32_t> zeros;
    for (std::uint32_t m = 0; m < (1U << n); ++m) {
      if (std::find(ones.begin(), ones.end(), m) == ones.end() && std::find(dcs.begin(), dcs.end(), m) == dcs.end())
        zeros.push_back(m);
    }
    if (!zeros.empty()) {
      const auto f = lg::parse(lg::pos_to_string(pos.cover.cubes, names(n)), names(n));
      const auto t = lg::truth_table(f);
      bool ok = true;
      for (const auto m : ones) ok &= t[m] == 1;
      for (const auto m : zeros) ok &= t[m] == 0;
      expect(ok, "minimal POS implements f");
    }
  }
}

void bdds() {
  for (int trial = 0; trial < 200; ++trial) {
    const std::size_t n = 5;
    const auto e = random_expr(n, 5);
    lg::Bdd bdd(n);
    const auto a = bdd.from_expr(*e);
    const auto b = bdd.from_expr(*lg::to_nand_only(e));
    expect(a == b, "equivalent expressions give the same BDD node (canonicity)");
    const auto t = table_of(e, n);
    expect(static_cast<std::size_t>(bdd.sat_count(a)) == lg::minterms(t).size(), "BDD satisfying-assignment count");
    bool same = true;
    for (std::uint64_t m = 0; m < t.size(); ++m) same &= bdd.evaluate(a, m) == (t[m] != 0);
    expect(same, "BDD evaluation equals the truth table");
  }
  // variable order: a1 b1 + a2 b2 + a3 b3
  const auto f = lg::parse("a1 b1 + a2 b2 + a3 b3", {"a1", "a2", "a3", "b1", "b2", "b3"});
  lg::Bdd interleaved(6, {0, 3, 1, 4, 2, 5});
  lg::Bdd separated(6, {0, 1, 2, 3, 4, 5});
  const auto si = interleaved.size(interleaved.from_expr(*f.expr));
  const auto ss = separated.size(separated.from_expr(*f.expr));
  expect(si == 6 && ss == 14, "variable order: interleaved 6 nodes, separated 14 (got " + std::to_string(si) + ", " +
                                  std::to_string(ss) + ")");
}

void sat() {
  std::uniform_int_distribution<int> var(1, 12);
  std::uniform_int_distribution<int> sign(0, 1);
  int sat_count = 0;
  for (int trial = 0; trial < 300; ++trial) {
    std::vector<lg::Clause> cnf;
    for (int c = 0; c < 51; ++c) {
      lg::Clause cl;
      for (int k = 0; k < 3; ++k) cl.push_back(sign(rng) ? var(rng) : -var(rng));
      cnf.push_back(cl);
    }
    lg::SatSolver s;
    for (int v = 0; v < 12; ++v) s.new_var();
    for (const auto& c : cnf) s.add_clause(c);
    const bool got = s.solve() == lg::SatSolver::Result::Sat;
    expect(got == lg::brute_force_sat(cnf, 12), "CDCL agrees with brute force on random 3-SAT");
    if (got) {
      ++sat_count;
      bool model_ok = true;
      for (const auto& c : cnf) {
        bool any = false;
        for (const int l : c) any |= s.value(std::abs(l)) == (l > 0);
        model_ok &= any;
      }
      expect(model_ok, "returned model satisfies every clause");
    }
  }
  expect(sat_count > 20 && sat_count < 280, "random instances near the threshold mix SAT and UNSAT");
  // pigeonhole: 5 pigeons, 4 holes is unsatisfiable
  lg::SatSolver php;
  auto p = [](int i, int h) { return i * 4 + h + 1; };
  for (int i = 0; i < 5; ++i) php.add_clause({p(i, 0), p(i, 1), p(i, 2), p(i, 3)});
  for (int h = 0; h < 4; ++h)
    for (int i = 0; i < 5; ++i)
      for (int j = i + 1; j < 5; ++j) php.add_clause({-p(i, h), -p(j, h)});
  expect(php.solve() == lg::SatSolver::Result::Unsat && php.conflicts() > 0, "pigeonhole 5->4 is UNSAT");
  // DIMACS round trip and SAT-based equivalence
  int nv = 0;
  const auto parsed = lg::parse_dimacs(lg::to_dimacs({{1, -2}, {2, 3}}, 3), nv);
  expect(parsed.size() == 2 && nv == 3, "DIMACS round trip");
  for (int trial = 0; trial < 100; ++trial) {
    const std::size_t n = 6;
    const auto e = random_expr(n, 5);
    const auto eq = lg::sat_equivalent(*e, *lg::to_nor_only(e), n);
    expect(eq.equivalent, "SAT miter proves a rewrite equivalent");
    const auto other = lg::make_xor(e, lg::make_and(lg::make_var(0), lg::make_var(1)));
    const auto ne = lg::sat_equivalent(*e, *other, n);
    expect(!ne.equivalent && ne.counterexample.has_value() &&
               lg::evaluate(*e, n, *ne.counterexample) != lg::evaluate(*other, n, *ne.counterexample),
           "SAT miter finds a real counterexample");
  }
}

void codes() {
  bool ok = true;
  for (std::uint32_t v = 0; v < 65536; ++v) {
    ok &= lg::gray_decode(lg::gray_encode(v)) == v;
    ok &= std::popcount(lg::gray_encode(v) ^ lg::gray_encode(v + 1)) == 1;
  }
  expect(ok, "Gray code is invertible and adjacent codes differ in one bit");
  expect(lg::even_parity_bit(0b1011) && !lg::even_parity_bit(0b1001), "even parity bit");
  for (const std::size_t k : {4U, 11U, 26U}) {
    expect(lg::hamming_parity_bits(k) == (k == 4 ? 3U : (k == 11 ? 4U : 5U)), "Hamming parity bit count");
    std::uniform_int_distribution<int> bit(0, 1);
    for (int trial = 0; trial < 40; ++trial) {
      std::vector<std::uint8_t> data(k);
      for (auto& d : data) d = static_cast<std::uint8_t>(bit(rng));
      const auto code = lg::hamming_encode(data);
      expect(lg::hamming_decode(code, k).syndrome == 0, "clean codeword has zero syndrome");
      for (std::size_t pos = 0; pos < code.size(); ++pos) {
        auto bad = code;
        bad[pos] ^= 1U;
        const auto d = lg::hamming_decode(bad, k);
        expect(d.syndrome == pos + 1 && d.data == data, "single-bit error located and corrected");
      }
    }
  }
}

lg::Fsm random_fsm(std::size_t states, std::size_t in_bits, std::size_t out_bits, bool moore) {
  lg::Fsm f;
  f.moore = moore;
  f.input_bits = in_bits;
  f.output_bits = out_bits;
  std::uniform_int_distribution<std::size_t> st(0, states - 1);
  std::uniform_int_distribution<std::uint32_t> out(0, (1U << out_bits) - 1U);
  for (std::size_t s = 0; s < states; ++s) {
    f.states.push_back("S" + std::to_string(s));
    std::vector<std::size_t> row;
    std::vector<std::uint32_t> outs;
    const std::uint32_t mo = out(rng);
    for (std::size_t v = 0; v < (std::size_t{1} << in_bits); ++v) {
      row.push_back(st(rng));
      outs.push_back(moore ? mo : out(rng));
    }
    f.next.push_back(row);
    f.output.push_back(outs);
  }
  return f;
}

void fsms() {
  // overlapping "1011" detector (Mealy)
  const std::string text = R"(
fsm det1011 mealy inputs=1 outputs=1 reset=A
A 0 -> A / 0
A 1 -> B / 0
B 0 -> C / 0
B 1 -> B / 0
C 0 -> A / 0
C 1 -> D / 0
D 0 -> C / 0
D 1 -> B / 1
)";
  const auto det = lg::parse_fsm(text);
  const std::vector<std::uint32_t> in{1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1};
  const std::vector<std::uint32_t> want{0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1};
  expect(lg::simulate(det, in) == want, "1011 detector output");
  expect(lg::fsm_equivalent(det, lg::parse_fsm(lg::format_fsm(det))), "FSM text round trip");
  // a redundant machine: states E and F duplicate C and D
  const auto red = lg::parse_fsm(text + "\n");
  lg::Fsm dup = red;
  dup.states.push_back("E");
  dup.states.push_back("F");
  dup.next.push_back({0, 5});
  dup.next.push_back({4, 1});
  dup.output.push_back({0, 0});
  dup.output.push_back({0, 1});
  dup.next[3][0] = 4;  // D goes to the copy E instead of C, so both C and its copy stay reachable
  std::vector<std::size_t> blocks;
  const auto m = lg::minimize_fsm(dup, &blocks);
  expect(m.states.size() == 4 && lg::fsm_equivalent(m, dup), "minimisation merges the duplicated states");
  expect(blocks[2] == blocks[4] && blocks[3] == blocks[5], "duplicates map to the same block");

  for (int trial = 0; trial < 120; ++trial) {
    const bool moore = trial % 2 == 0;
    const auto f = random_fsm(2 + static_cast<std::size_t>(trial % 6), 1 + static_cast<std::size_t>(trial % 2),
                              1 + static_cast<std::size_t>(trial % 2), moore);
    const auto mf = lg::minimize_fsm(f);
    expect(lg::fsm_equivalent(f, mf), "minimised FSM is equivalent");
    // no two states of the minimised machine are equivalent
    bool distinct = true;
    for (std::size_t s = 0; s < mf.states.size(); ++s) {
      for (std::size_t t = s + 1; t < mf.states.size(); ++t) {
        lg::Fsm a = mf, b = mf;
        a.reset = s;
        b.reset = t;
        distinct &= !lg::fsm_equivalent(a, b);
      }
    }
    expect(distinct, "minimised states are pairwise distinguishable");
    std::uniform_int_distribution<std::uint32_t> inp(0, (1U << f.input_bits) - 1U);
    std::vector<std::uint32_t> seq(60);
    for (auto& x : seq) x = inp(rng);
    for (const auto enc : {lg::Encoding::Binary, lg::Encoding::Gray, lg::Encoding::OneHot}) {
      for (const auto ff : {lg::FlipFlop::D, lg::FlipFlop::T, lg::FlipFlop::JK}) {
        if (enc == lg::Encoding::OneHot && ff != lg::FlipFlop::D) continue;
        const auto c = lg::synthesize(mf, enc, ff);
        expect(lg::simulate(c, seq) == lg::simulate(mf, seq), "synthesised circuit behaves like the FSM");
        expect(lg::fsm_equivalent(lg::analyze(c), mf), "analysis of the synthesised circuit recovers the FSM");
      }
    }
  }
  // analysis of a circuit given by equations (two D flip-flops A, B, input x, output y)
  const auto c = lg::parse_circuit(R"(
flipflop D
state A B
inputs x
reset 00
D A = A x + B x
D B = A' x
out y = (A + B) x'
)");
  const auto table = lg::analyze(c);
  // from 00: x=0 -> 00 / 0, x=1 -> 01 / 0; from 01: x=1 -> 11 / 0; from 11: x=0 -> 00 / 1
  expect(table.states[table.next[0][1]] == "S01" && table.output[0][1] == 0, "analysis: 00 --1--> 01");
  const std::size_t s01 = table.next[0][1];
  expect(table.states[table.next[s01][1]] == "S11", "analysis: 01 --1--> 11");
  const std::size_t s11 = table.next[s01][1];
  expect(table.states[table.next[s11][0]] == "S00" && table.output[s11][0] == 1, "analysis: 11 --0--> 00 / 1");
  expect(lg::to_verilog_structural(c, "seq").find("always @(posedge clk)") != std::string::npos, "Verilog export");
}

void gate_level() {
  using V = lg::Value;
  auto latch = lg::sr_nor_latch();
  latch.drive("S", V::One, 0);
  latch.drive("R", V::Zero, 0);
  latch.run(10);
  expect(latch.value("Q") == V::One && latch.value("QN") == V::Zero, "SR latch set");
  latch.drive("S", V::Zero, 11);
  latch.run(20);
  expect(latch.value("Q") == V::One, "SR latch holds");
  latch.drive("R", V::One, 21);
  latch.run(30);
  expect(latch.value("Q") == V::Zero && latch.value("QN") == V::One, "SR latch reset");
  latch.drive("S", V::One, 31);
  latch.run(40);
  expect(latch.value("Q") == V::Zero && latch.value("QN") == V::Zero, "S = R = 1 forces Q = QN = 0");
  latch.drive("S", V::Zero, 41);
  latch.drive("R", V::Zero, 41);
  expect(!latch.run(1000, 200), "releasing S and R together oscillates with equal gate delays (race)");

  auto d = lg::gated_d_latch();
  d.drive("EN", V::One, 0);
  d.drive("D", V::One, 0);
  d.run(10);
  expect(d.value("Q") == V::One, "D latch is transparent when enabled");
  d.drive("EN", V::Zero, 11);
  d.drive("D", V::Zero, 15);
  d.run(25);
  expect(d.value("Q") == V::One, "D latch holds when disabled");

  auto ff = lg::master_slave_d_flip_flop();
  ff.drive("CLK", V::Zero, 0);
  ff.drive("D", V::Zero, 0);
  ff.drive("CLK", V::One, 10);  // rising edge captures 0
  ff.drive("CLK", V::Zero, 20);
  ff.drive("D", V::One, 22);    // D changes while the clock is low: Q must not change
  ff.run(29);
  expect(ff.value("Q") == V::Zero, "master-slave flip-flop ignores D while the clock is low");
  ff.drive("CLK", V::One, 30);  // rising edge captures 1
  ff.drive("D", V::Zero, 35);   // change while high: ignored until the next edge
  ff.run(45);
  expect(ff.value("Q") == V::One, "master-slave flip-flop captures D at the rising edge");
  expect(!ff.waveform({"CLK", "D", "Q"}, 45).empty(), "waveform rendering");
}

// smallest number of shared product terms by brute force over all cubes (3 inputs, true outputs only)
std::size_t brute_force_pla_rows(const std::vector<lg::MultiOutput>& outs) {
  std::vector<lg::Cube> cubes;
  for (std::uint32_t care = 0; care < 8; ++care)
    for (std::uint32_t value = 0; value < 8; ++value)
      if ((value & ~care) == 0) cubes.push_back({value, care});
  auto ok = [&](const std::vector<std::size_t>& pick) {
    for (const auto& o : outs) {
      for (const auto m : o.ones) {
        bool hit = false;
        for (const auto c : pick) {
          bool inside = true;
          for (std::uint32_t x = 0; x < 8 && inside; ++x)
            if (cubes[c].covers(x)) inside = std::find(o.ones.begin(), o.ones.end(), x) != o.ones.end();
          hit = hit || (inside && cubes[c].covers(m));
        }
        if (!hit) return false;
      }
    }
    return true;
  };
  for (std::size_t k = 0; k <= 8; ++k) {
    std::vector<std::size_t> pick(k);
    std::function<bool(std::size_t, std::size_t)> rec = [&](std::size_t i, std::size_t from) {
      if (i == k) return ok(pick);
      for (std::size_t c = from; c < cubes.size(); ++c) {
        pick[i] = c;
        if (rec(i + 1, c + 1)) return true;
      }
      return false;
    };
    if (rec(0, 0)) return k;
  }
  return 99;
}

void programmable() {
  // textbook PLA example: F1 = sum(0,1,2,4), F2 = sum(0,5,6,7); with F1 complemented the PLA needs 4 terms
  const std::vector<lg::MultiOutput> mano = {{{0, 1, 2, 4}, {}}, {{0, 5, 6, 7}, {}}};
  const auto inv = lg::minimize_pla(3, mano, true);
  const auto plain = lg::minimize_pla(3, mano, false);
  expect(inv.terms.size() == 4 && inv.complemented[0] && !inv.complemented[1], "PLA example: 4 terms with F1 inverted");
  expect(plain.terms.size() == 6 && lg::pla_implements(inv, mano) && lg::pla_implements(plain, mano),
         "PLA example: 6 terms without inversion");
  const auto pal = lg::pal_terms(3, mano);
  expect(pal == std::vector<std::size_t>({3, 3}), "PAL example: 3 + 3 terms");
  std::mt19937 rng(121);
  for (int trial = 0; trial < 80; ++trial) {
    const std::size_t n = 3 + static_cast<std::size_t>(trial % 2);
    const std::size_t k = 2 + static_cast<std::size_t>(trial % 3);
    std::vector<lg::MultiOutput> outs(k);
    for (auto& o : outs) {
      for (std::uint32_t m = 0; m < (1U << n); ++m) {
        const auto r = rng() % 6;
        if (r < 2) o.ones.push_back(m);
        else if (r == 2 && trial >= 40) o.dont_cares.push_back(m);
      }
    }
    const auto a = lg::minimize_pla(n, outs, true);
    const auto b = lg::minimize_pla(n, outs, false);
    std::size_t pal_sum = 0, single = 0;
    for (const auto t : lg::pal_terms(n, outs)) {
      pal_sum += t;
      single = std::max(single, t);
    }
    expect(lg::pla_implements(a, outs) && lg::pla_implements(b, outs), "random PLA implements every output");
    expect(a.terms.size() <= b.terms.size() && b.terms.size() <= pal_sum && b.terms.size() >= single,
           "inversion <= plain PLA <= PAL, and no fewer rows than the largest single output needs");
    if (n == 3 && trial < 40) {
      expect(b.terms.size() == brute_force_pla_rows(outs), "PLA rows equal the brute-force minimum");
    }
  }
}

}  // namespace

int main() {
  boolean_algebra();
  minimisation();
  bdds();
  sat();
  codes();
  fsms();
  gate_level();
  programmable();
  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "logic tests passed\n";
  return 0;
}
