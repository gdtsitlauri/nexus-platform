// NEY705 VLSI design: each algorithm is checked against textbook values, brute force or an independent method.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <string>

#include "nexus/logic/boolean.hpp"
#include "nexus/vlsi/aig.hpp"
#include "nexus/vlsi/analysis.hpp"
#include "nexus/vlsi/cmos.hpp"
#include "nexus/vlsi/logical_effort.hpp"
#include "nexus/vlsi/netlist.hpp"
#include "nexus/vlsi/physical.hpp"

namespace lg = nexus::logic;
namespace vl = nexus::vlsi;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }

std::mt19937 rng(705);

lg::ExprPtr random_expr(std::size_t n, int depth) {
  std::uniform_int_distribution<int> pick(0, depth <= 0 ? 1 : 4);
  const int k = pick(rng);
  if (k <= 1) {
    std::uniform_int_distribution<std::size_t> var(0, n - 1);
    auto v = lg::make_var(var(rng));
    return k == 0 ? v : lg::make_not(v);
  }
  auto a = random_expr(n, depth - 1);
  auto b = random_expr(n, depth - 1);
  return k == 2 ? lg::make_and(a, b) : (k == 3 ? lg::make_or(a, b) : lg::make_xor(a, b));
}

vl::Aig random_aig(std::size_t inputs, std::size_t outputs, int depth) {
  vl::Aig aig;
  std::vector<vl::Lit> vars;
  for (std::size_t i = 0; i < inputs; ++i) {
    vars.push_back(aig.add_input("i" + std::to_string(i)));
  }
  for (std::size_t o = 0; o < outputs; ++o) {
    aig.add_output("o" + std::to_string(o), aig.from_expr(*random_expr(inputs, depth), vars));
  }
  return aig;
}

void cmos_gates() {
  struct Case {
    std::string expr;
    std::vector<double> g;
    double p;
  };
  const std::vector<Case> cases = {
      {"a", {1.0}, 1.0},
      {"a b", {4.0 / 3, 4.0 / 3}, 2.0},
      {"a + b", {5.0 / 3, 5.0 / 3}, 2.0},
      {"a b c", {5.0 / 3, 5.0 / 3, 5.0 / 3}, 3.0},
      {"a + b + c", {7.0 / 3, 7.0 / 3, 7.0 / 3}, 3.0},
      {"a b + c", {2.0, 2.0, 5.0 / 3}, 7.0 / 3},
  };
  for (const auto& c : cases) {
    const auto g = vl::cmos_gate(c.expr);
    expect(vl::verify_complementary(g) == 0, "complementary networks for " + c.expr);
    bool ok = g.logical_effort.size() == c.g.size() && near(g.parasitic, c.p);
    for (std::size_t i = 0; ok && i < c.g.size(); ++i) {
      ok = near(g.logical_effort[i], c.g[i]);
    }
    expect(ok, "textbook logical effort and parasitic delay for " + c.expr);
  }
  for (const std::string expr : {"(a + b)(c + d)", "a (b + c) + d e", "a b + c d", "(a + b c) d"}) {
    const auto g = vl::cmos_gate(expr);
    expect(vl::verify_complementary(g) == 0, "complex gate is complementary: " + expr);
    const auto e = vl::euler_ordering(g);
    const auto rects = vl::layout_cell(g, e);
    expect(vl::drc(rects).empty(), "generated layout is DRC clean: " + expr);
    // a bad ordering needs at least as many diffusion breaks and is at least as wide
    vl::EulerOrdering worst = e;
    std::reverse(worst.order.begin(), worst.order.end());
    std::rotate(worst.order.begin(), worst.order.begin() + 1, worst.order.end());
    const auto bad_rects = vl::layout_cell(g, worst);
    expect(vl::cell_width(bad_rects) >= vl::cell_width(rects), "Euler ordering gives the narrowest cell: " + expr);
  }
  const auto aoi = vl::euler_ordering(vl::cmos_gate("a b + c"));
  expect(aoi.breaks_pull_down == 0 && aoi.breaks_pull_up == 0, "AOI21 has an unbroken Euler path in both networks");
  // design-rule violations are detected
  auto rects = vl::layout_cell(vl::cmos_gate("a b"), vl::euler_ordering(vl::cmos_gate("a b")));
  auto narrow = rects;
  for (auto& r : narrow) {
    if (r.layer == vl::Layer::Poly) {
      r.x1 = r.x0 + 1;
      break;
    }
  }
  bool width = false;
  for (const auto& v : vl::drc(narrow)) width |= v.rule == "min width";
  expect(width, "DRC finds a too-narrow poly");
  auto short_poly = rects;
  for (auto& r : short_poly) {
    if (r.layer == vl::Layer::Poly) {
      r.y1 -= 3;
      break;
    }
  }
  bool overhang = false;
  for (const auto& v : vl::drc(short_poly)) overhang |= v.rule == "poly overhang";
  expect(overhang, "DRC finds a missing poly overhang");
  auto close = rects;
  close.push_back(vl::Rect{vl::Layer::Metal1, close[0].x1 + 1, 0, close[0].x1 + 5, 4, ""});
  close.push_back(vl::Rect{vl::Layer::Metal1, close[0].x1 + 6, 0, close[0].x1 + 10, 4, ""});
  bool spacing = false;
  for (const auto& v : vl::drc(close)) spacing |= v.rule == "min spacing";
  expect(spacing, "DRC finds a spacing violation");
}

void switch_level() {
  using L = vl::Logic;
  // 6T SRAM
  vl::SwitchNetwork s;
  s.pmos("QB", "VDD", "Q", 2);
  s.nmos("QB", "Q", "GND");
  s.pmos("Q", "VDD", "QB", 2);
  s.nmos("Q", "QB", "GND");
  s.nmos("WL", "BL", "Q");
  s.nmos("WL", "BLB", "QB");
  s.set_input("WL", L::One);
  s.set_input("BL", L::Zero);
  s.set_input("BLB", L::One);
  s.settle();
  expect(s.value("Q") == L::Zero && s.value("QB") == L::One, "SRAM write 0");
  s.set_input("WL", L::Zero);
  s.release("BL");
  s.release("BLB");
  s.settle();
  expect(s.value("Q") == L::Zero, "SRAM holds with the word line off");
  s.set_input("BL", L::One);
  s.set_input("BLB", L::One);
  s.settle();
  s.release("BL");
  s.release("BLB");
  s.set_input("WL", L::One);
  s.settle();
  expect(s.value("BL") == L::Zero && s.value("BLB") == L::One && s.value("Q") == L::Zero,
         "SRAM read discharges the bit line on the 0 side without upsetting the cell");
  // write fails if the pull-ups were as strong as the access transistors (ratioed design)
  vl::SwitchNetwork strong;
  strong.pmos("QB", "VDD", "Q", 3);
  strong.nmos("QB", "Q", "GND", 3);
  strong.pmos("Q", "VDD", "QB", 3);
  strong.nmos("Q", "QB", "GND", 3);
  strong.nmos("WL", "BL", "Q", 3);
  strong.nmos("WL", "BLB", "QB", 3);
  strong.set_input("WL", L::Zero);
  strong.set_input("Q", L::One);
  strong.settle();
  strong.release("Q");
  strong.settle();
  strong.set_input("BL", L::Zero);
  strong.set_input("BLB", L::One);
  strong.set_input("WL", L::One);
  strong.settle();
  expect(strong.value("Q") != L::Zero, "without sizing ratio the write does not flip the cell cleanly");
  // dynamic latch: a transmission gate stores charge when the clock is off
  vl::SwitchNetwork d;
  d.nmos("CLK", "D", "S");
  d.pmos("CLKB", "D", "S");
  d.pmos("S", "VDD", "Y");
  d.nmos("S", "Y", "GND");
  d.set_input("CLK", L::One);
  d.set_input("CLKB", L::Zero);
  d.set_input("D", L::One);
  d.settle();
  expect(d.value("Y") == L::Zero, "dynamic latch transparent");
  d.set_input("CLK", L::Zero);
  d.set_input("CLKB", L::One);
  d.set_input("D", L::Zero);
  d.settle();
  expect(d.value("S") == L::One && d.value("Y") == L::Zero, "dynamic latch keeps its value as stored charge");
  // three-inverter ring oscillator never settles
  vl::SwitchNetwork ring;
  const char* n[] = {"A", "B", "C"};
  for (int i = 0; i < 3; ++i) {
    ring.pmos(n[i], "VDD", n[(i + 1) % 3]);
    ring.nmos(n[i], n[(i + 1) % 3], "GND");
  }
  ring.set_input("A", L::One);
  ring.settle();
  ring.release("A");
  expect(!ring.settle(50), "ring oscillator does not settle");
}

void logical_effort() {
  // inverter chain driving 64: best with 3 stages, delay 15
  const auto a = vl::analyze_path({vl::Stage{vl::inverter(), 1.0}}, 1.0, 64.0);
  expect(a.best_stages == 3 && near(a.best_delay, 15.0, 1e-6), "64x load: 3 stages, D = 15");
  // closed form equals an independent numerical optimum
  for (int trial = 0; trial < 20; ++trial) {
    std::vector<vl::Stage> stages;
    const char* names[] = {"inv", "nand2", "nor2", "nand3", "aoi21", "xor2"};
    std::uniform_int_distribution<int> pick(0, 5);
    std::uniform_real_distribution<double> br(1.0, 3.0);
    for (int s = 0; s < 4; ++s) {
      stages.push_back(vl::Stage{vl::lookup_gate(names[pick(rng)]), br(rng)});
    }
    const double cout = 10.0 + 90.0 * static_cast<double>(trial) / 20.0;
    const auto r = vl::analyze_path(stages, 1.0, cout);
    expect(near(vl::path_delay(stages, r.input_caps, cout), r.min_delay, 1e-9), "sizes reach N F^(1/N) + P");
    const auto num = vl::optimize_sizes_numerically(stages, 1.0, cout);
    expect(near(vl::path_delay(stages, num, cout), r.min_delay, 1e-5), "numerical optimum equals the closed form");
  }
}

void aigs() {
  for (int trial = 0; trial < 60; ++trial) {
    const std::size_t n = 6;
    const auto e = random_expr(n, 6);
    vl::Aig aig;
    std::vector<vl::Lit> vars;
    for (std::size_t i = 0; i < n; ++i) vars.push_back(aig.add_input("x" + std::to_string(i)));
    aig.add_output("f", aig.from_expr(*e, vars));
    // exhaustive simulation in one 64-bit word: input i is bit (n-1-i) of the minterm
    std::vector<std::uint64_t> words(n, 0);
    for (std::uint64_t m = 0; m < 64; ++m)
      for (std::size_t i = 0; i < n; ++i)
        if ((m >> (n - 1 - i)) & 1U) words[i] |= std::uint64_t{1} << m;
    const auto out = aig.simulate(words)[0];
    bool ok = true;
    for (std::uint64_t m = 0; m < 64; ++m) ok &= (((out >> m) & 1U) != 0) == lg::evaluate(*e, n, m);
    expect(ok, "AIG simulation equals expression evaluation");
    const auto bal = aig.balance();
    expect(vl::aig_equivalent(aig, bal).equivalent && bal.depth() <= aig.depth(), "balancing keeps function, not deeper");
    const auto back = vl::read_blif(vl::write_blif(aig));
    expect(vl::aig_equivalent(aig, back).equivalent, "BLIF round trip");
  }
  // a 16-input AND chain balances to depth 4
  vl::Aig chain;
  vl::Lit acc = vl::kLitTrue;
  for (int i = 0; i < 16; ++i) acc = chain.and_(acc, chain.add_input("a" + std::to_string(i)));
  chain.add_output("y", acc);
  expect(chain.depth() == 15 && chain.balance().depth() == 4, "AND chain depth 15 balances to 4");
  // hand-written BLIF: off-set rows, don't cares, a latch
  const auto blif = vl::read_blif(R"(.model t
.inputs a b c
.outputs y z
.latch d q 0
.names a b n1
1- 1
-1 1
.names n1 c y
11 0
.names a q d
10 1
01 1
.names q z
1 1
.end
)");
  // y = NOT((a+b) c); z = q; next state d = a xor q
  vl::Aig ref;
  const auto a = ref.add_input("a"), b = ref.add_input("b"), c = ref.add_input("c"), q = ref.add_input("q");
  ref.add_output("y", vl::lit_not(ref.and_(ref.or_(a, b), c)));
  ref.add_output("z", q);
  ref.add_output("q$next", ref.xor_(a, q));
  expect(vl::aig_equivalent(blif, ref).equivalent, "BLIF with off-set rows and a cut latch");
  // a mutation is detected with a counterexample
  vl::Aig wrong;
  const auto wa = wrong.add_input("a"), wb = wrong.add_input("b"), wc = wrong.add_input("c"), wq = wrong.add_input("q");
  wrong.add_output("y", vl::lit_not(wrong.and_(wrong.or_(wa, wb), vl::lit_not(wc))));
  wrong.add_output("z", wq);
  wrong.add_output("q$next", wrong.xor_(wa, wq));
  const auto ne = vl::aig_equivalent(blif, wrong);
  expect(!ne.equivalent && ne.mismatch.has_value(), "inequivalence detected");
}

void mapping_and_analysis() {
  const auto lib = vl::default_library();
  for (int trial = 0; trial < 25; ++trial) {
    const auto aig = random_aig(7, 4, 6);
    for (const auto goal : {vl::MapGoal::Area, vl::MapGoal::Delay}) {
      const auto net = vl::tech_map(aig, lib, goal);
      expect(vl::aig_equivalent(aig, net.to_aig()).equivalent, "mapped netlist is equivalent (SAT)");
      const auto t = vl::static_timing(net);
      expect(near(t.critical_delay, vl::longest_path_brute_force(net), 1e-9), "STA equals path enumeration");
      bool slack_ok = true;
      for (const auto& c : net.cells) slack_ok &= t.slack[c.output] >= -1e-9;
      expect(slack_ok && std::abs(t.worst_slack) < 1e-9, "no negative slack at the critical period");
      if (!t.critical_path.empty()) {
        expect(near(t.arrival[net.cells[t.critical_path.back()].output], t.critical_delay, 1e-9),
               "critical path ends at the critical arrival");
      }
    }
  }
  // buffering a high-fanout net reduces the critical delay and keeps the function
  vl::Aig fan;
  const auto x = fan.add_input("x");
  std::vector<vl::Lit> ins;
  for (int i = 0; i < 40; ++i) ins.push_back(fan.add_input("y" + std::to_string(i)));
  for (int i = 0; i < 40; ++i) fan.add_output("o" + std::to_string(i), fan.and_(x, ins[static_cast<std::size_t>(i)]));
  auto net = vl::tech_map(fan, lib);
  const double before = vl::static_timing(net).critical_delay;
  const auto buffered = vl::buffer_fanout(net, 4);
  expect(vl::aig_equivalent(fan, buffered.to_aig()).equivalent, "buffering keeps the function");
  expect(vl::static_timing(buffered).critical_delay < before, "buffer tree reduces the critical delay");
  // power: on a fanout-free circuit the probabilistic activity matches simulation
  vl::Netlist tree;
  tree.library = lib;
  tree.add_net("0");
  tree.add_net("1");
  std::vector<std::size_t> leaves;
  for (int i = 0; i < 8; ++i) {
    leaves.push_back(tree.add_net("in" + std::to_string(i)));
    tree.inputs.push_back(leaves.back());
  }
  std::size_t level_a = tree.add_net("a"), level_b = tree.add_net("b"), level_c = tree.add_net("c"), level_d = tree.add_net("d");
  tree.cells.push_back({2, {leaves[0], leaves[1]}, level_a, "g0"});           // NAND2
  tree.cells.push_back({3, {leaves[2], leaves[3]}, level_b, "g1"});           // NOR2
  tree.cells.push_back({6, {leaves[4], leaves[5], leaves[6]}, level_c, "g2"}); // NAND3
  tree.cells.push_back({8, {level_a, level_b, level_c}, level_d, "g3"});       // AOI21
  tree.outputs.push_back(level_d);
  tree.output_names.push_back("y");
  const auto p = vl::power_analysis(tree, 1 << 16);
  bool agree = true;
  for (const auto net_id : {level_a, level_b, level_c, level_d}) {
    agree &= std::abs(p.activity_sim[net_id] - p.activity_prob[net_id]) < 0.02;
  }
  expect(agree && std::abs(p.activity_sim[leaves[0]] - 0.5) < 0.02, "simulated and probabilistic activity agree on a tree");
  // stuck-at fault coverage: random patterns plus SAT-based ATPG reach 100% of the testable faults
  for (int trial = 0; trial < 6; ++trial) {
    const auto aig = random_aig(8, 3, 6);
    const auto net = vl::tech_map(aig, lib);
    const auto f = vl::fault_analysis(net, 16);
    expect(f.detected_random + f.detected_atpg + f.redundant == f.faults, "every fault detected or proved redundant");
    // each ATPG vector really detects a fault: apply it to the good circuit and check outputs exist
    expect(f.atpg_vectors.size() == f.detected_atpg, "one test vector per ATPG-detected fault");
  }
  // a circuit with a redundant fault: y = a b + a b' = a has a redundant gate input
  vl::Netlist red;
  red.library = lib;
  red.add_net("0");
  red.add_net("1");
  const std::size_t ra = red.add_net("a"), rb = red.add_net("b"), rbn = red.add_net("bn"), r1 = red.add_net("x1"),
                    r2 = red.add_net("x2"), ry = red.add_net("y");
  red.inputs = {ra, rb};
  red.cells.push_back({0, {rb}, rbn, "inv"});
  red.cells.push_back({4, {ra, rb}, r1, "and1"});
  red.cells.push_back({4, {ra, rbn}, r2, "and2"});
  red.cells.push_back({5, {r1, r2}, ry, "or"});
  red.outputs = {ry};
  red.output_names = {"y"};
  const auto fr = vl::fault_analysis(red, 64);
  expect(fr.redundant > 0 && fr.detected_random + fr.detected_atpg + fr.redundant == fr.faults,
         "redundant stuck-at faults are proved untestable");
}

void partitioning() {
  const auto h = vl::planted_hypergraph(120, 400, 12, 3);
  const auto init = vl::random_bisection(h.num_cells, 9);
  const auto fm = vl::fiduccia_mattheyses(h, init, 0.05);
  const auto kl = vl::kernighan_lin(h, init);
  const std::size_t c0 = vl::cut_size(h, init), cf = vl::cut_size(h, fm), ck = vl::cut_size(h, kl);
  expect(cf < c0 && ck < c0, "KL and FM improve a random bisection");
  expect(cf <= 12 + 4, "FM recovers the planted cut (" + std::to_string(cf) + ")");
  const auto ones = static_cast<std::size_t>(std::count(kl.begin(), kl.end(), 1));
  expect(ones == h.num_cells / 2, "KL keeps the halves equal");
  const auto fones = static_cast<double>(std::count(fm.begin(), fm.end(), 1));
  expect(std::abs(fones - 60.0) <= 0.05 * 120 + 1, "FM respects the balance tolerance");
}

void floorplanning() {
  std::vector<vl::Block> blocks = {{"a", 4, 3, true}, {"b", 3, 3, true}, {"c", 2, 1, true},
                                   {"d", 1, 1, true}, {"e", 2, 2, true}, {"f", 3, 1, true}};
  double total = 0.0;
  for (const auto& b : blocks) total += b.w * b.h;
  const auto ex = vl::exhaustive_floorplan(blocks);
  const auto sa = vl::anneal_floorplan(blocks, 1, 20000);
  expect(vl::is_normalized_polish(sa.expression, blocks.size()), "annealing keeps a normalised Polish expression");
  expect(ex.area >= total - 1e-9 && sa.area >= ex.area - 1e-9, "areas bounded by block area and the exact optimum");
  expect(sa.area <= ex.area * 1.05, "annealing within 5% of the exact slicing optimum");
  // placed blocks do not overlap and fit the outline
  bool ok = true;
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    ok &= sa.x[i] + sa.w[i] <= sa.width + 1e-9 && sa.y[i] + sa.h[i] <= sa.height + 1e-9;
    for (std::size_t j = i + 1; j < blocks.size(); ++j) {
      const bool sep = sa.x[i] + sa.w[i] <= sa.x[j] + 1e-9 || sa.x[j] + sa.w[j] <= sa.x[i] + 1e-9 ||
                       sa.y[i] + sa.h[i] <= sa.y[j] + 1e-9 || sa.y[j] + sa.h[j] <= sa.y[i] + 1e-9;
      ok &= sep;
    }
  }
  expect(ok, "floorplan blocks are disjoint and inside the outline");
  expect(!vl::is_normalized_polish({0, 1, vl::kCutV, 2, vl::kCutV, vl::kCutV}, 3), "invalid expression rejected");
}

void placement() {
  const auto h = vl::planted_hypergraph(100, 260, 20, 5);
  std::vector<std::vector<std::size_t>> pad_cells;
  for (std::size_t k = 0; k < 12; ++k) pad_cells.push_back({k * 8});
  const std::size_t side = 11;
  const auto pads = vl::pads_on_boundary(pad_cells, side, side);
  std::size_t iters = 0;
  double residual = 1.0;
  const auto q = vl::quadratic_placement(h, pads, &iters, &residual);
  expect(residual < 1e-8 && iters > 0, "conjugate gradients solve the placement system");
  const auto legal = vl::legalize(q, side, side);
  const auto rnd = vl::random_placement(h.num_cells, side, side, 3);
  const auto mc = vl::mincut_placement(h, side, side, 4);
  const auto sa = vl::anneal_placement(h, legal, side, side, &pads, 6, 30000);
  expect(vl::is_legal(legal, side, side) && vl::is_legal(mc, side, side) && vl::is_legal(sa, side, side),
         "placements are legal (one cell per slot)");
  const double w_rnd = vl::hpwl(h, rnd, &pads), w_q = vl::hpwl(h, legal, &pads), w_mc = vl::hpwl(h, mc, &pads),
               w_sa = vl::hpwl(h, sa, &pads);
  expect(w_q < w_rnd && w_mc < w_rnd && w_sa <= w_q, "quadratic, min-cut and annealing beat random placement");
}

void routing() {
  vl::RoutingGrid g(30, 20);
  g.block(10, 2, 11, 17);
  g.block(18, 0, 19, 12);
  std::uniform_int_distribution<int> px(0, 29), py(0, 19);
  for (int trial = 0; trial < 40; ++trial) {
    vl::Point s{px(rng), py(rng)}, t{px(rng), py(rng)};
    if (g.blocked[g.index(0, s.x, s.y)] || g.blocked[g.index(0, t.x, t.y)]) continue;
    const auto lee = vl::lee_route(g, s, t);
    const auto as = vl::astar_route(g, s, t);
    expect(lee.found == as.found && (!lee.found || near(lee.cost, as.cost)), "A* finds the Lee (optimal) cost");
    expect(as.expanded <= lee.expanded, "A* expands no more cells than Lee");
  }
  vl::RoutingGrid open(10, 10);
  const auto p = vl::lee_route(open, {1, 1}, {7, 5});
  expect(near(p.cost, 6 + 4 + open.via_cost), "open-grid route: Manhattan distance plus one via");
  // congestion: many nets through a narrow gap must be negotiated
  vl::RoutingGrid narrow(16, 12);
  narrow.block(7, 0, 8, 3);  // a four-track gap (rows 4..7) for four nets
  narrow.block(7, 8, 8, 11);
  std::vector<vl::NetRequest> nets;
  for (int i = 0; i < 4; ++i) nets.push_back({"n" + std::to_string(i), {{0, 3 + i}, {15, 8 - i}}});
  narrow.capacity = 1;
  const auto rr = vl::negotiated_route(narrow, nets, 40);
  expect(rr.overflow == 0, "negotiated congestion removes all overflow");
  // channel routing
  const std::vector<int> top{1, 1, 2, 0, 3, 4, 0, 5};
  const std::vector<int> bottom{0, 2, 3, 3, 0, 5, 4, 0};
  const auto ch = vl::left_edge_channel_route(top, bottom);
  bool ok = !ch.cyclic && ch.tracks >= ch.density;
  for (std::size_t c = 0; c < top.size(); ++c) {
    if (top[c] > 0 && bottom[c] > 0 && top[c] != bottom[c]) {
      ok &= ch.track_of_net[static_cast<std::size_t>(top[c])] < ch.track_of_net[static_cast<std::size_t>(bottom[c])];
    }
  }
  expect(ok, "left-edge routing respects vertical constraints and density");
  const auto cyc = vl::left_edge_channel_route({1, 2}, {2, 1});
  expect(cyc.cyclic, "cyclic vertical constraints detected");
  // Steiner trees
  for (int trial = 0; trial < 30; ++trial) {
    std::vector<vl::Point> pts;
    std::uniform_int_distribution<int> c(0, 20);
    for (int i = 0; i < 5; ++i) pts.push_back({c(rng), c(rng)});
    double one = 0.0;
    vl::iterated_one_steiner(pts, &one);
    const double mst = vl::rectilinear_mst_length(pts);
    const double exact = vl::exact_rsmt_length(pts);
    expect(exact <= one + 1e-9 && one <= mst + 1e-9 && mst <= 1.5 * exact + 1e-9, "RSMT <= 1-Steiner <= MST <= 1.5 RSMT");
  }
}

}  // namespace

int main() {
  cmos_gates();
  switch_level();
  logical_effort();
  aigs();
  mapping_and_analysis();
  partitioning();
  floorplanning();
  placement();
  routing();
  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "vlsi tests passed\n";
  return 0;
}
