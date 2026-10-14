#include "nexus/vlsi/cmos.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace nexus::vlsi {

namespace lg = nexus::logic;

// ---------------------------------------------------------------- switch-level simulation

std::size_t SwitchNetwork::node(const std::string& name) {
  const auto it = std::find(names_.begin(), names_.end(), name);
  if (it != names_.end()) {
    return static_cast<std::size_t>(it - names_.begin());
  }
  names_.push_back(name);
  values_.push_back(Logic::X);
  drive_.push_back(0);
  driven_value_.push_back(Logic::X);
  if (name == "VDD") {
    drive_.back() = 4;
    driven_value_.back() = values_.back() = Logic::One;
  } else if (name == "GND") {
    drive_.back() = 4;
    driven_value_.back() = values_.back() = Logic::Zero;
  }
  return names_.size() - 1;
}

std::size_t SwitchNetwork::find(const std::string& name) const {
  const auto it = std::find(names_.begin(), names_.end(), name);
  if (it == names_.end()) {
    throw std::runtime_error("switch network: unknown node " + name);
  }
  return static_cast<std::size_t>(it - names_.begin());
}

void SwitchNetwork::nmos(const std::string& gate, const std::string& a, const std::string& b, int strength) {
  transistors_.push_back(Transistor{false, node(gate), node(a), node(b), strength});
}

void SwitchNetwork::pmos(const std::string& gate, const std::string& a, const std::string& b, int strength) {
  transistors_.push_back(Transistor{true, node(gate), node(a), node(b), strength});
}

void SwitchNetwork::set_input(const std::string& name, Logic v) {
  const auto n = node(name);
  drive_[n] = 4;
  driven_value_[n] = v;
  values_[n] = v;
}

void SwitchNetwork::release(const std::string& name) { drive_[node(name)] = 0; }

bool SwitchNetwork::settle(std::size_t max_iterations) {
  const std::size_t n = names_.size();
  // Strongest source reaching every node through conducting transistors. Transistors whose gate is X are handled
  // as in MOSSIM: the network is solved twice, with them off and with them on, and a node gets a definite value
  // only when both solutions agree.
  auto solve = [&](bool unknown_on) {
    std::vector<int> strength(n, 0);
    std::vector<Logic> value(n, Logic::X);
    for (std::size_t i = 0; i < n; ++i) {
      if (drive_[i] > 0) {
        strength[i] = drive_[i];
        value[i] = driven_value_[i];
      } else {
        strength[i] = 1;  // stored charge
        value[i] = values_[i];
      }
    }
    bool changed = true;
    for (std::size_t pass = 0; changed && pass < 4 * n + 4; ++pass) {
      changed = false;
      for (const auto& t : transistors_) {
        const Logic g = values_[t.gate];
        const bool on = g == Logic::X ? unknown_on : ((g == Logic::One) != t.p);
        if (!on) {
          continue;
        }
        for (int dir = 0; dir < 2; ++dir) {
          const std::size_t from = dir == 0 ? t.a : t.b;
          const std::size_t to = dir == 0 ? t.b : t.a;
          if (drive_[to] > 0) {
            continue;
          }
          const int st = std::min(strength[from], t.strength);
          const Logic v = value[from];
          if (st > strength[to]) {
            strength[to] = st;
            value[to] = v;
            changed = true;
          } else if (st == strength[to] && v != value[to] && value[to] != Logic::X) {
            value[to] = Logic::X;  // equal-strength fight
            changed = true;
          }
        }
      }
    }
    return value;
  };
  for (std::size_t iter = 0; iter < max_iterations; ++iter) {
    const auto off = solve(false);
    const auto on = solve(true);
    std::vector<Logic> value(n, Logic::X);
    for (std::size_t i = 0; i < n; ++i) {
      value[i] = off[i] == on[i] ? off[i] : Logic::X;
    }
    if (value == values_) {
      return true;
    }
    values_ = value;
  }
  return false;
}

Logic SwitchNetwork::value(const std::string& name) const { return values_[find(name)]; }

// ---------------------------------------------------------------- complex gates

namespace {

SpNode from_expr(const lg::Expr& e, const std::vector<std::string>& vars) {
  SpNode n;
  switch (e.kind) {
    case lg::Expr::Kind::Var:
      n.input = vars[e.var];
      return n;
    case lg::Expr::Kind::Not:
      if (e.kids[0]->kind != lg::Expr::Kind::Var) {
        throw std::runtime_error("cmos_gate: only inputs may be complemented in the pull-down expression");
      }
      n.input = vars[e.kids[0]->var] + "'";
      return n;
    case lg::Expr::Kind::And:
    case lg::Expr::Kind::Or: {
      n.kind = e.kind == lg::Expr::Kind::And ? SpNode::Kind::Series : SpNode::Kind::Parallel;
      for (const auto& k : e.kids) {
        SpNode c = from_expr(*k, vars);
        if (c.kind == n.kind) {
          for (auto& g : c.kids) {
            n.kids.push_back(g);
          }
        } else {
          n.kids.push_back(c);
        }
      }
      return n;
    }
    default:
      throw std::runtime_error("cmos_gate: the pull-down expression must use AND, OR and input literals");
  }
}

SpNode dual(const SpNode& n) {
  SpNode d = n;
  if (n.kind != SpNode::Kind::Leaf) {
    d.kind = n.kind == SpNode::Kind::Series ? SpNode::Kind::Parallel : SpNode::Kind::Series;
    d.kids.clear();
    for (const auto& k : n.kids) {
      d.kids.push_back(dual(k));
    }
  }
  return d;
}

double depth(const SpNode& n) {
  if (n.kind == SpNode::Kind::Leaf) {
    return 1.0;
  }
  double total = 0.0;
  for (const auto& k : n.kids) {
    total = n.kind == SpNode::Kind::Series ? total + depth(k) : std::max(total, depth(k));
  }
  return total;
}

// widths so that the worst conducting path has the resistance of a unit transistor of the same type
void size(const SpNode& n, double budget, bool p, bool at_output, bool output_first,
          std::vector<SizedTransistor>& out) {
  if (n.kind == SpNode::Kind::Leaf) {
    out.push_back(SizedTransistor{n.input, p, (p ? 2.0 : 1.0) / budget, at_output});
    return;
  }
  const double d = depth(n);
  for (std::size_t i = 0; i < n.kids.size(); ++i) {
    const auto& k = n.kids[i];
    if (n.kind == SpNode::Kind::Parallel) {
      size(k, budget, p, at_output, output_first, out);
    } else {
      const bool touches = output_first ? i == 0 : i + 1 == n.kids.size();
      size(k, budget * depth(k) / d, p, at_output && touches, output_first, out);
    }
  }
}

void collect_inputs(const SpNode& n, std::vector<std::string>& out) {
  if (n.kind == SpNode::Kind::Leaf) {
    if (std::find(out.begin(), out.end(), n.input) == out.end()) {
      out.push_back(n.input);
    }
    return;
  }
  for (const auto& k : n.kids) {
    collect_inputs(k, out);
  }
}

bool conducts(const SpNode& n, const std::map<std::string, bool>& in, bool p) {
  if (n.kind == SpNode::Kind::Leaf) {
    const bool v = in.at(n.input);
    return p ? !v : v;
  }
  if (n.kind == SpNode::Kind::Series) {
    return std::all_of(n.kids.begin(), n.kids.end(), [&](const SpNode& k) { return conducts(k, in, p); });
  }
  return std::any_of(n.kids.begin(), n.kids.end(), [&](const SpNode& k) { return conducts(k, in, p); });
}

std::string base_name(const std::string& in) { return in.back() == '\'' ? in.substr(0, in.size() - 1) : in; }

void build_switches(const SpNode& n, const std::string& top, const std::string& bottom, bool p, SwitchNetwork& net,
                    std::size_t& counter) {
  if (n.kind == SpNode::Kind::Leaf) {
    if (p) {
      net.pmos(n.input, top, bottom);
    } else {
      net.nmos(n.input, top, bottom);
    }
    return;
  }
  if (n.kind == SpNode::Kind::Parallel) {
    for (const auto& k : n.kids) {
      build_switches(k, top, bottom, p, net, counter);
    }
    return;
  }
  std::string upper = top;
  for (std::size_t i = 0; i < n.kids.size(); ++i) {
    const std::string lower = i + 1 == n.kids.size() ? bottom : "n" + std::to_string(counter++);
    build_switches(n.kids[i], upper, lower, p, net, counter);
    upper = lower;
  }
}

struct Edge {
  std::string input;
  std::size_t u, v;
};

void graph_edges(const SpNode& n, std::size_t top, std::size_t bottom, std::size_t& next_node, std::vector<Edge>& out) {
  if (n.kind == SpNode::Kind::Leaf) {
    out.push_back(Edge{n.input, top, bottom});
    return;
  }
  if (n.kind == SpNode::Kind::Parallel) {
    for (const auto& k : n.kids) {
      graph_edges(k, top, bottom, next_node, out);
    }
    return;
  }
  std::size_t upper = top;
  for (std::size_t i = 0; i < n.kids.size(); ++i) {
    const std::size_t lower = i + 1 == n.kids.size() ? bottom : next_node++;
    graph_edges(n.kids[i], upper, lower, next_node, out);
    upper = lower;
  }
}

// diffusion breaks when the transistors are laid out in `order`: brk[i] is true when column i cannot continue the
// strip of column i-1 (dynamic programming over the orientation of each transistor, with traceback)
std::vector<bool> break_positions(const std::vector<Edge>& edges, const std::vector<std::string>& order) {
  std::vector<const Edge*> seq;
  for (const auto& name : order) {
    for (const auto& e : edges) {
      if (e.input == name) {
        seq.push_back(&e);
      }
    }
  }
  const std::size_t n = seq.size();
  std::vector<bool> brk(n, false);
  if (n == 0) {
    return brk;
  }
  // cost[i][o]: fewest breaks for columns 0..i with column i oriented o (0: u->v, ends at v; 1: v->u, ends at u)
  std::vector<std::array<std::size_t, 2>> cost(n), from(n);
  cost[0] = {0, 0};
  auto end_of = [&](std::size_t i, int o) { return o == 0 ? seq[i]->v : seq[i]->u; };
  auto start_of = [&](std::size_t i, int o) { return o == 0 ? seq[i]->u : seq[i]->v; };
  for (std::size_t i = 1; i < n; ++i) {
    for (int o = 0; o < 2; ++o) {
      std::size_t best = ~std::size_t{0};
      for (int po = 0; po < 2; ++po) {
        const std::size_t c = cost[i - 1][static_cast<std::size_t>(po)] + (end_of(i - 1, po) == start_of(i, o) ? 0 : 1);
        if (c < best) {
          best = c;
          from[i][static_cast<std::size_t>(o)] = static_cast<std::size_t>(po);
        }
      }
      cost[i][static_cast<std::size_t>(o)] = best;
    }
  }
  int o = cost[n - 1][0] <= cost[n - 1][1] ? 0 : 1;
  for (std::size_t i = n - 1; i > 0; --i) {
    const int po = static_cast<int>(from[i][static_cast<std::size_t>(o)]);
    brk[i] = end_of(i - 1, po) != start_of(i, o);
    o = po;
  }
  return brk;
}

std::size_t breaks_for(const std::vector<Edge>& edges, const std::vector<std::string>& order) {
  const auto b = break_positions(edges, order);
  return static_cast<std::size_t>(std::count(b.begin(), b.end(), true));
}

}  // namespace

CmosGate cmos_gate(const std::string& pull_down_expr, const std::string& name) {
  const auto f = lg::parse(pull_down_expr);
  CmosGate g;
  g.name = name;
  g.pull_down = from_expr(*f.expr, f.vars);
  g.pull_up = dual(g.pull_down);
  collect_inputs(g.pull_down, g.inputs);
  size(g.pull_down, 1.0, false, true, true, g.transistors);
  size(g.pull_up, 1.0, true, true, false, g.transistors);
  for (const auto& in : g.inputs) {
    double c = 0.0;
    for (const auto& t : g.transistors) {
      if (t.input == in) {
        c += t.width;
      }
    }
    g.logical_effort.push_back(c / 3.0);  // unit inverter: 1 + 2
  }
  double out = 0.0;
  for (const auto& t : g.transistors) {
    if (t.at_output) {
      out += t.width;
    }
  }
  g.parasitic = out / 3.0;
  return g;
}

SwitchNetwork to_switch_network(const CmosGate& g) {
  SwitchNetwork net;
  std::size_t counter = 0;
  build_switches(g.pull_down, "Y", "GND", false, net, counter);
  build_switches(g.pull_up, "VDD", "Y", true, net, counter);
  return net;
}

std::size_t verify_complementary(const CmosGate& g) {
  std::vector<std::string> bases;
  for (const auto& in : g.inputs) {
    const auto b = base_name(in);
    if (std::find(bases.begin(), bases.end(), b) == bases.end()) {
      bases.push_back(b);
    }
  }
  std::size_t failures = 0;
  for (std::uint32_t m = 0; m < (1U << bases.size()); ++m) {
    std::map<std::string, bool> in;
    SwitchNetwork net = to_switch_network(g);
    for (std::size_t i = 0; i < bases.size(); ++i) {
      const bool v = ((m >> i) & 1U) != 0;
      in[bases[i]] = v;
      in[bases[i] + "'"] = !v;
      net.set_input(bases[i], v ? Logic::One : Logic::Zero);
      net.set_input(bases[i] + "'", v ? Logic::Zero : Logic::One);
    }
    const bool down = conducts(g.pull_down, in, false);
    const bool up = conducts(g.pull_up, in, true);
    net.settle();
    const Logic y = net.value("Y");
    const bool ok = (down != up) && y == (down ? Logic::Zero : Logic::One);
    if (!ok) {
      ++failures;
    }
  }
  return failures;
}

EulerOrdering euler_ordering(const CmosGate& g) {
  if (g.inputs.size() > 8) {
    throw std::runtime_error("euler_ordering: at most 8 inputs");
  }
  std::vector<Edge> down, up;
  std::size_t next = 2;  // 0 = output side, 1 = supply side
  graph_edges(g.pull_down, 0, 1, next, down);
  next = 2;
  graph_edges(g.pull_up, 1, 0, next, up);
  std::vector<std::string> order = g.inputs;
  std::sort(order.begin(), order.end());
  EulerOrdering best;
  std::size_t best_total = ~std::size_t{0};
  do {
    const std::size_t bd = breaks_for(down, order);
    const std::size_t bu = breaks_for(up, order);
    if (bd + bu < best_total) {
      best_total = bd + bu;
      best = EulerOrdering{order, bd, bu};
    }
  } while (std::next_permutation(order.begin(), order.end()));
  return best;
}

std::string stick_diagram(const CmosGate& g, const EulerOrdering& e) {
  std::ostringstream out;
  out << g.name << ": Y = NOT(pull-down)\n";
  out << "poly columns: ";
  for (const auto& in : e.order) {
    out << " | " << in;
  }
  out << " |\n";
  out << "pMOS diffusion: " << (e.breaks_pull_up == 0 ? "one continuous strip" : std::to_string(e.breaks_pull_up) + " break(s)")
      << "\nnMOS diffusion: "
      << (e.breaks_pull_down == 0 ? "one continuous strip" : std::to_string(e.breaks_pull_down) + " break(s)") << '\n';
  out << "widths (unit nMOS = 1):";
  for (const auto& t : g.transistors) {
    out << ' ' << (t.p ? "p" : "n") << ':' << t.input << '=' << t.width;
  }
  out << "\nlogical effort:";
  for (std::size_t i = 0; i < g.inputs.size(); ++i) {
    out << ' ' << g.inputs[i] << '=' << g.logical_effort[i];
  }
  out << "  parasitic delay p=" << g.parasitic << '\n';
  return out.str();
}

std::vector<Rect> layout_cell(const CmosGate& g, const EulerOrdering& e, const DrcRules& r) {
  // One vertical poly column per input; an nMOS strip at the bottom, a pMOS strip at the top. Where either network
  // has a diffusion break, both rows leave a gap so that the poly columns stay straight.
  std::vector<Edge> down, up;
  std::size_t next = 2;
  graph_edges(g.pull_down, 0, 1, next, down);
  next = 2;
  graph_edges(g.pull_up, 1, 0, next, up);
  const auto brk_n = break_positions(down, e.order);
  const auto brk_p = break_positions(up, e.order);
  const std::size_t cols = e.order.size();
  const int contact_area = r.contact_size + 2 * r.contact_enclosure + 1;  // contact plus spacing to the gate
  const int pitch = r.poly_width + contact_area;
  std::vector<int> x(cols, contact_area);
  for (std::size_t i = 1; i < cols; ++i) {
    x[i] = x[i - 1] + pitch + ((brk_n[i] || brk_p[i]) ? r.diff_space + contact_area : 0);
  }
  double wn = 1.0, wp = 1.0;
  for (const auto& t : g.transistors) {
    (t.p ? wp : wn) = std::max(t.p ? wp : wn, t.width);
  }
  const int n_y0 = r.poly_overhang + 1;
  const int n_y1 = n_y0 + std::max(contact_area, static_cast<int>(4.0 * wn + 0.5));
  const int p_y0 = n_y1 + 3 * r.diff_space;
  const int p_y1 = p_y0 + std::max(contact_area, static_cast<int>(2.0 * wp + 0.5));
  std::vector<Rect> rects;
  auto strips = [&](const std::vector<bool>& brk, Layer layer, int y0, int y1) {
    std::size_t start = 0;
    for (std::size_t i = 1; i <= cols; ++i) {
      if (i == cols || brk[i]) {
        const int sx0 = x[start] - contact_area;
        const int sx1 = x[i - 1] + r.poly_width + contact_area;
        rects.push_back(Rect{layer, sx0, y0, sx1, y1, ""});
        for (const int cx : {sx0 + r.contact_enclosure, sx1 - r.contact_enclosure - r.contact_size}) {
          const int cy = y0 + r.contact_enclosure;
          rects.push_back(Rect{Layer::Contact, cx, cy, cx + r.contact_size, cy + r.contact_size, ""});
          rects.push_back(Rect{Layer::Metal1, cx - r.contact_enclosure, cy - r.contact_enclosure,
                               cx + r.contact_size + r.contact_enclosure, cy + r.contact_size + r.contact_enclosure, ""});
        }
        start = i;
      }
    }
  };
  if (cols > 0) {
    strips(brk_n, Layer::NDiff, n_y0, n_y1);
    strips(brk_p, Layer::PDiff, p_y0, p_y1);
  }
  for (std::size_t i = 0; i < cols; ++i) {
    rects.push_back(Rect{Layer::Poly, x[i], n_y0 - r.poly_overhang, x[i] + r.poly_width, p_y1 + r.poly_overhang,
                         e.order[i]});
  }
  return rects;
}

namespace {

int min_width(Layer l, const DrcRules& r) {
  switch (l) {
    case Layer::Poly:
      return r.poly_width;
    case Layer::NDiff:
    case Layer::PDiff:
      return r.diff_width;
    case Layer::Metal1:
      return r.metal_width;
    case Layer::Contact:
      return r.contact_size;
  }
  return 1;
}

int min_space(Layer l, const DrcRules& r) {
  switch (l) {
    case Layer::Poly:
      return r.poly_space;
    case Layer::NDiff:
    case Layer::PDiff:
      return r.diff_space;
    case Layer::Metal1:
      return r.metal_space;
    case Layer::Contact:
      return r.contact_size;
  }
  return 1;
}

bool overlap(const Rect& a, const Rect& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

int gap(const Rect& a, const Rect& b) {
  const int dx = std::max({0, b.x0 - a.x1, a.x0 - b.x1});
  const int dy = std::max({0, b.y0 - a.y1, a.y0 - b.y1});
  return std::max(dx, dy);
}

}  // namespace

std::vector<DrcViolation> drc(const std::vector<Rect>& rects, const DrcRules& r) {
  std::vector<DrcViolation> out;
  for (std::size_t i = 0; i < rects.size(); ++i) {
    const Rect& a = rects[i];
    if (std::min(a.x1 - a.x0, a.y1 - a.y0) < min_width(a.layer, r)) {
      out.push_back({"min width", i, i});
    }
    for (std::size_t j = i + 1; j < rects.size(); ++j) {
      const Rect& b = rects[j];
      const bool same_diff = (a.layer == Layer::NDiff || a.layer == Layer::PDiff) &&
                             (b.layer == Layer::NDiff || b.layer == Layer::PDiff);
      if ((a.layer == b.layer || same_diff) && !overlap(a, b)) {
        const int g = gap(a, b);
        const bool touching = g == 0;
        if (!touching && g < min_space(a.layer, r)) {
          out.push_back({"min spacing", i, j});
        }
      }
    }
    if (a.layer == Layer::Poly) {
      for (std::size_t j = 0; j < rects.size(); ++j) {
        const Rect& d = rects[j];
        if ((d.layer == Layer::NDiff || d.layer == Layer::PDiff) && overlap(a, d)) {
          // a gate: poly must extend beyond the diffusion on both sides
          if (a.y0 > d.y0 - r.poly_overhang || a.y1 < d.y1 + r.poly_overhang) {
            out.push_back({"poly overhang", i, j});
          }
        }
      }
    }
    if (a.layer == Layer::Contact) {
      bool diff_ok = false, metal_ok = false;
      for (const auto& d : rects) {
        const bool encloses = d.x0 <= a.x0 - r.contact_enclosure && d.x1 >= a.x1 + r.contact_enclosure &&
                              d.y0 <= a.y0 - r.contact_enclosure && d.y1 >= a.y1 + r.contact_enclosure;
        if (encloses && (d.layer == Layer::NDiff || d.layer == Layer::PDiff || d.layer == Layer::Poly)) {
          diff_ok = true;
        }
        if (encloses && d.layer == Layer::Metal1) {
          metal_ok = true;
        }
      }
      if (!diff_ok || !metal_ok) {
        out.push_back({"contact enclosure", i, i});
      }
    }
  }
  return out;
}

int cell_width(const std::vector<Rect>& rects) {
  int x0 = 0, x1 = 0;
  bool first = true;
  for (const auto& r : rects) {
    if (first) {
      x0 = r.x0;
      x1 = r.x1;
      first = false;
    }
    x0 = std::min(x0, r.x0);
    x1 = std::max(x1, r.x1);
  }
  return x1 - x0;
}

}  // namespace nexus::vlsi
