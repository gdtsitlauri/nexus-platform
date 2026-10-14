#include "nexus/vlsi/cli.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "nexus/logic/boolean.hpp"
#include "nexus/vlsi/aig.hpp"
#include "nexus/vlsi/analysis.hpp"
#include "nexus/vlsi/cmos.hpp"
#include "nexus/vlsi/logical_effort.hpp"
#include "nexus/vlsi/netlist.hpp"
#include "nexus/vlsi/physical.hpp"

namespace nexus::vlsi {

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open " + path);
  }
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

std::string opt_or(const std::map<std::string, std::string>& o, const std::string& k, const std::string& d) {
  const auto it = o.find(k);
  return it == o.end() ? d : it->second;
}

std::vector<std::string> split_csv(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream in(s);
  std::string item;
  while (std::getline(in, item, ',')) {
    if (!item.empty()) {
      out.push_back(item);
    }
  }
  return out;
}

const char* logic_char(Logic v) { return v == Logic::One ? "1" : (v == Logic::Zero ? "0" : "X"); }

// a design from a BLIF file or a Boolean expression (single output "f")
Aig load_design(const std::string& arg) {
  if (arg.size() > 5 && arg.substr(arg.size() - 5) == ".blif") {
    return read_blif(read_file(arg));
  }
  const auto f = nexus::logic::parse(arg);
  Aig aig;
  std::vector<Lit> vars;
  for (const auto& v : f.vars) {
    vars.push_back(aig.add_input(v));
  }
  aig.add_output("f", aig.from_expr(*f.expr, vars));
  return aig;
}

int cmd_cmos(const std::string& expr) {
  const CmosGate g = cmos_gate(expr, "gate");
  const auto e = euler_ordering(g);
  std::cout << stick_diagram(g, e);
  const std::size_t bad = verify_complementary(g);
  std::cout << "switch-level check: " << (bad == 0 ? "complementary for every input" : std::to_string(bad) + " failing inputs")
            << " (" << to_switch_network(g).transistor_count() << " transistors)\n";
  const auto rects = layout_cell(g, e);
  const auto v = drc(rects);
  std::cout << "layout: " << rects.size() << " rectangles, cell width " << cell_width(rects) << " lambda, DRC "
            << (v.empty() ? "clean" : std::to_string(v.size()) + " violation(s)") << '\n';
  return bad == 0 && v.empty() ? 0 : 2;
}

int cmd_sram() {
  // 6T cell: cross-coupled inverters (weak pull-ups) and two access transistors on the word line
  SwitchNetwork s;
  s.pmos("QB", "VDD", "Q", 2);
  s.nmos("QB", "Q", "GND");
  s.pmos("Q", "VDD", "QB", 2);
  s.nmos("Q", "QB", "GND");
  s.nmos("WL", "BL", "Q");
  s.nmos("WL", "BLB", "QB");
  std::cout << "6T SRAM cell at switch level (pull-ups weak, access transistors normal strength)\n";
  auto show = [&](const char* step) {
    s.settle();
    std::cout << "  " << step << ": Q=" << logic_char(s.value("Q")) << " QB=" << logic_char(s.value("QB"))
              << " BL=" << logic_char(s.value("BL")) << " BLB=" << logic_char(s.value("BLB")) << '\n';
  };
  s.set_input("WL", Logic::One);
  s.set_input("BL", Logic::One);
  s.set_input("BLB", Logic::Zero);
  show("write 1 (WL=1, BL=1, BLB=0)");
  s.set_input("WL", Logic::Zero);
  s.release("BL");
  s.release("BLB");
  show("hold (WL=0)");
  s.set_input("BL", Logic::One);
  s.set_input("BLB", Logic::One);
  s.settle();
  s.release("BL");
  s.release("BLB");
  s.set_input("WL", Logic::One);
  show("read (bit lines precharged to 1, then floating)");
  s.set_input("WL", Logic::Zero);
  s.set_input("BL", Logic::Zero);
  s.set_input("BLB", Logic::One);
  s.set_input("WL", Logic::One);
  show("write 0");
  return 0;
}

int cmd_effort(const std::vector<std::string>& pos, const std::map<std::string, std::string>& opt) {
  std::vector<Stage> stages;
  const auto branches = split_csv(opt_or(opt, "--branch", ""));
  const auto gates = split_csv(pos.at(0));
  for (std::size_t i = 0; i < gates.size(); ++i) {
    Stage s;
    s.gate = lookup_gate(gates[i]);
    s.branching = i < branches.size() ? std::stod(branches[i]) : 1.0;
    stages.push_back(s);
  }
  const double cin = std::stod(opt_or(opt, "--cin", "1"));
  const double cout = std::stod(opt_or(opt, "--cout", "64"));
  const auto a = analyze_path(stages, cin, cout);
  std::cout << format_path(stages, a);
  return 0;
}

struct Flow {
  Aig aig;
  Netlist net;
  AigEquivalence eq;
};

Flow synthesize_design(const std::string& arg, const std::map<std::string, std::string>& opt, bool verbose) {
  Flow f;
  f.aig = load_design(arg).cleanup();
  Aig work = f.aig;
  if (opt.count("--balance") != 0) {
    work = work.balance();
  }
  const MapGoal goal = opt_or(opt, "--goal", "area") == "delay" ? MapGoal::Delay : MapGoal::Area;
  MapStats st;
  f.net = tech_map(work, default_library(), goal, &st);
  const std::size_t buffer_limit = std::stoul(opt_or(opt, "--buffer", "0"));
  double unbuffered_delay = 0.0;
  if (buffer_limit >= 2) {
    unbuffered_delay = static_timing(f.net).critical_delay;
    f.net = buffer_fanout(f.net, buffer_limit);
  }
  f.eq = aig_equivalent(f.aig, f.net.to_aig());
  if (verbose) {
    std::cout << "AIG: " << f.aig.num_inputs() << " inputs, " << f.aig.outputs().size() << " outputs, "
              << f.aig.num_ands() << " AND nodes, depth " << f.aig.depth() << '\n';
    if (opt.count("--balance") != 0) {
      std::cout << "balanced AIG: " << work.num_ands() << " AND nodes, depth " << work.depth() << '\n';
    }
    std::cout << "technology mapping (" << (goal == MapGoal::Area ? "area" : "delay") << "): " << f.net.cells.size()
              << " cells, area " << f.net.area() << " transistors (" << st.cuts_enumerated << " cuts, "
              << st.matches_tried << " matches tried)\n";
    std::map<std::string, int> count;
    for (const auto& c : f.net.cells) {
      ++count[f.net.library[c.type].name];
    }
    std::cout << "cells:";
    for (const auto& [name, k] : count) {
      std::cout << ' ' << name << 'x' << k;
    }
    if (buffer_limit >= 2) {
      std::cout << "\nbuffer insertion (fanout <= " << buffer_limit << "): critical delay " << unbuffered_delay
                << " -> " << static_timing(f.net).critical_delay << " tau";
    }
    std::cout << "\nequivalence (simulation + SAT miter) of the mapped netlist with the source: "
              << (f.eq.equivalent ? "PROVED" : "FAILED: " + f.eq.mismatch.value_or("?")) << " (" << f.eq.conflicts
              << " conflicts)\n";
  }
  return f;
}

int cmd_synth(const std::vector<std::string>& pos, const std::map<std::string, std::string>& opt) {
  const Flow f = synthesize_design(pos.at(0), opt, true);
  const auto t = static_timing(f.net);
  std::cout << format_timing(f.net, t);
  const auto p = power_analysis(f.net);
  std::cout << "dynamic power (alpha C, V^2 f = 1): simulated " << p.dynamic_sim << ", probabilistic "
            << p.dynamic_prob << '\n';
  if (opt.count("--verilog") != 0) {
    std::ofstream out(opt.at("--verilog"));
    out << to_verilog(f.net, opt_or(opt, "--module", "mapped"));
    std::cout << "wrote " << opt.at("--verilog") << '\n';
  }
  if (opt.count("--blif") != 0) {
    std::ofstream out(opt.at("--blif"));
    out << write_blif(f.aig, "source");
  }
  return f.eq.equivalent ? 0 : 3;
}

std::size_t grid_side(std::size_t cells) {
  std::size_t side = 1;
  while (side * side < cells * 5 / 4 + 1) {
    ++side;
  }
  return side;
}

int cmd_place(const std::vector<std::string>& pos, const std::map<std::string, std::string>& opt, bool route) {
  const Flow f = synthesize_design(pos.at(0), opt, true);
  std::vector<std::vector<std::size_t>> pad_cells;
  const Hypergraph h = to_hypergraph(f.net, &pad_cells);
  const std::size_t side = grid_side(h.num_cells);
  const auto pads = pads_on_boundary(pad_cells, static_cast<double>(side), static_cast<double>(side));
  const auto seed = static_cast<std::uint32_t>(std::stoul(opt_or(opt, "--seed", "1")));
  std::cout << "placement on a " << side << " x " << side << " slot grid, " << h.num_cells << " cells, " << h.nets.size()
            << " nets, " << pad_cells.size() << " pads\n";
  // partitioning
  const auto init = random_bisection(h.num_cells, seed);
  const auto kl = kernighan_lin(h, init);
  const auto fm = fiduccia_mattheyses(h, init);
  std::cout << "bisection cut: random " << cut_size(h, init) << ", Kernighan-Lin " << cut_size(h, kl)
            << ", Fiduccia-Mattheyses " << cut_size(h, fm) << '\n';
  const auto rnd = random_placement(h.num_cells, side, side, seed);
  std::size_t cg = 0;
  double res = 0.0;
  const auto quad = quadratic_placement(h, pads, &cg, &res);
  const auto quad_legal = legalize(quad, side, side);
  const auto mincut = mincut_placement(h, side, side, seed);
  const auto annealed = anneal_placement(h, quad_legal, side, side, &pads, seed, 40000);
  std::cout << "HPWL: random " << hpwl(h, rnd, &pads) << ", quadratic (global, " << cg << " CG iterations, residual "
            << res << ") " << hpwl(h, quad, &pads) << ", legalised " << hpwl(h, quad_legal, &pads) << ", min-cut "
            << hpwl(h, mincut, &pads) << ", quadratic + annealing " << hpwl(h, annealed, &pads) << '\n';
  if (!route) {
    return 0;
  }
  if (h.num_cells > 1500) {
    std::cout << "routing skipped: " << h.num_cells << " cells exceed the 1500-cell limit of the teaching router\n";
    return f.eq.equivalent ? 0 : 4;
  }
  // global routing of every net on a grid three times finer than the slot grid
  const int scale = 3;
  RoutingGrid grid(static_cast<int>(side) * scale + 1, static_cast<int>(side) * scale + 1);
  grid.capacity = std::stoi(opt_or(opt, "--capacity", "6"));  // tracks per global-routing cell
  std::vector<NetRequest> nets;
  auto pin = [&](std::size_t cell) {
    return Point{static_cast<int>(annealed.x[cell] * scale), static_cast<int>(annealed.y[cell] * scale)};
  };
  for (std::size_t e = 0; e < h.nets.size(); ++e) {
    NetRequest r;
    r.name = "net" + std::to_string(e);
    for (const auto c : h.nets[e]) {
      const Point p = pin(c);
      if (std::find(r.pins.begin(), r.pins.end(), p) == r.pins.end()) {
        r.pins.push_back(p);
      }
    }
    nets.push_back(r);
  }
  const auto rr = negotiated_route(grid, nets, 40);
  std::cout << "routing: " << nets.size() << " nets, " << rr.iterations << " negotiation iterations, overflow "
            << rr.overflow << ", wirelength " << rr.wirelength << ", vias " << rr.vias << '\n';
  // routed wire length per net feeds back into timing (0.2 unit capacitance per grid step)
  std::vector<std::size_t> net_ids;
  to_hypergraph(f.net, nullptr, &net_ids);
  TimingOptions to;
  to.wire_cap_per_fanout = 0.0;
  to.extra_net_cap.assign(f.net.nets.size(), 0.0);
  for (std::size_t e = 0; e < rr.nets.size(); ++e) {
    double len = 0.0;
    for (const auto& path : rr.nets[e]) {
      len += static_cast<double>(path.cells.size());
    }
    to.extra_net_cap[net_ids[e]] = 0.2 * len;
  }
  const auto t0 = static_timing(f.net);
  const auto t1 = static_timing(f.net, to);
  std::cout << "critical delay: " << t0.critical_delay << " tau with the wire-load model, " << t1.critical_delay
            << " tau with routed wire capacitance\n";
  return rr.overflow == 0 && f.eq.equivalent ? 0 : 4;
}

int cmd_floorplan(const std::map<std::string, std::string>& opt) {
  std::vector<Block> blocks = {{"cpu", 4, 3, true},  {"ram", 3, 3, true},  {"rom", 2, 1, true},
                               {"uart", 1, 1, true}, {"timer", 2, 2, true}, {"dma", 3, 1, true}};
  if (opt.count("--blocks") != 0) {
    blocks.clear();
    for (const auto& item : split_csv(opt.at("--blocks"))) {
      const auto x = item.find('x');
      blocks.push_back(Block{"b" + std::to_string(blocks.size()), std::stod(item.substr(0, x)),
                             std::stod(item.substr(x + 1)), true});
    }
  }
  double total = 0.0;
  for (const auto& b : blocks) {
    total += b.w * b.h;
  }
  const auto sa = anneal_floorplan(blocks, 1, 20000);
  std::cout << "blocks: " << blocks.size() << ", total block area " << total << '\n';
  std::cout << "simulated annealing (Wong-Liu): " << format_polish(sa.expression, blocks) << "  -> " << sa.width
            << " x " << sa.height << " = " << sa.area << " (dead space " << 100.0 * (1.0 - total / sa.area) << "%)\n";
  if (blocks.size() <= 8) {
    const auto ex = exhaustive_floorplan(blocks);
    std::cout << "exact optimum over all slicing floorplans: " << ex.area << '\n';
  }
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    std::cout << "  " << blocks[i].name << " at (" << sa.x[i] << ", " << sa.y[i] << ") size " << sa.w[i] << " x "
              << sa.h[i] << '\n';
  }
  return 0;
}

int cmd_faults(const std::vector<std::string>& pos, const std::map<std::string, std::string>& opt) {
  const Flow f = synthesize_design(pos.at(0), opt, false);
  const auto patterns = std::stoul(opt_or(opt, "--patterns", "64"));
  const auto r = fault_analysis(f.net, patterns);
  std::cout << "single stuck-at faults: " << r.faults << "\n  detected by " << patterns << " random patterns: "
            << r.detected_random << "\n  detected by SAT-based ATPG: " << r.detected_atpg
            << "\n  redundant (untestable): " << r.redundant << "\n  fault coverage: " << 100.0 * r.coverage()
            << "%\n";
  return 0;
}

int cmd_channel(const std::vector<std::string>& pos) {
  std::vector<int> top, bottom;
  for (const auto& s : split_csv(pos.at(0))) top.push_back(std::stoi(s));
  for (const auto& s : split_csv(pos.at(1))) bottom.push_back(std::stoi(s));
  const auto r = left_edge_channel_route(top, bottom);
  std::cout << "density " << r.density << ", tracks " << r.tracks << (r.cyclic ? " (cyclic vertical constraints)" : "")
            << '\n';
  for (std::size_t v = 1; v < r.track_of_net.size(); ++v) {
    if (r.track_of_net[v] >= 0) {
      std::cout << "  net " << v << " -> track " << r.track_of_net[v] << '\n';
    }
  }
  return r.cyclic ? 2 : 0;
}

int cmd_steiner(const std::vector<std::string>& pos) {
  std::vector<Point> pts;
  for (const auto& s : pos) {
    const auto c = s.find(',');
    pts.push_back(Point{std::stoi(s.substr(0, c)), std::stoi(s.substr(c + 1))});
  }
  double one = 0.0;
  const auto tree = iterated_one_steiner(pts, &one);
  std::cout << "rectilinear MST: " << rectilinear_mst_length(pts) << ", iterated 1-Steiner: " << one << " ("
            << tree.size() - pts.size() << " Steiner points)";
  if (pts.size() <= 5) {
    std::cout << ", exact RSMT: " << exact_rsmt_length(pts);
  }
  std::cout << '\n';
  return 0;
}

}  // namespace

std::string cli_usage() {
  return "VLSI design and EDA (NEY705):\n"
         "  nexus-eda cmos \"<pull-down expr>\"           complex CMOS gate: sizing, logical effort, Euler path,\n"
         "                                              switch-level check, layout and DRC (e.g. \"a b + c\" = AOI21)\n"
         "  nexus-eda sram                               6T SRAM write/read at switch level\n"
         "  nexus-eda effort nand2,nor2,inv --cin 1 --cout 64 [--branch 1,2,1]   logical-effort path sizing\n"
         "  nexus-eda synth <file.blif|\"<expr>\"> [--goal area|delay] [--balance] [--verilog out.v]\n"
         "                                              AIG, technology mapping, SAT equivalence, STA, power\n"
         "  nexus-eda place <file.blif|\"<expr>\">        partitioning (KL, FM) and placement (quadratic, min-cut, annealing)\n"
         "  nexus-eda flow <file.blif|\"<expr>\">         synthesis -> placement -> negotiated-congestion routing\n"
         "  nexus-eda floorplan [--blocks 4x3,3x3,...]   slicing floorplan by simulated annealing vs exact optimum\n"
         "  nexus-eda channel 1,0,2,3 2,1,0,3           left-edge channel routing (top and bottom pin rows)\n"
         "  nexus-eda steiner 0,0 4,3 2,5 ...            rectilinear MST, iterated 1-Steiner, exact RSMT\n"
         "  nexus-eda faults <file.blif|\"<expr>\"> [--patterns 64]   stuck-at fault simulation and SAT-based ATPG\n";
}

bool cli_handles(const std::string& c) {
  static const std::set<std::string> cmds{"cmos",     "sram",      "effort",  "synth",  "place",
                                          "flow",     "floorplan", "channel", "steiner", "faults"};
  return cmds.count(c) != 0;
}

int cli_run(const std::string& c, const std::vector<std::string>& pos, const std::map<std::string, std::string>& opt) {
  if (c == "cmos") return cmd_cmos(pos.at(0));
  if (c == "sram") return cmd_sram();
  if (c == "effort") return cmd_effort(pos, opt);
  if (c == "synth") return cmd_synth(pos, opt);
  if (c == "place") return cmd_place(pos, opt, false);
  if (c == "flow") return cmd_place(pos, opt, true);
  if (c == "floorplan") return cmd_floorplan(opt);
  if (c == "channel") return cmd_channel(pos);
  if (c == "steiner") return cmd_steiner(pos);
  if (c == "faults") return cmd_faults(pos, opt);
  return 1;
}

}  // namespace nexus::vlsi
