// nexus-eda: logic design (EY121) and VLSI / EDA algorithms (NEY705) from the command line.
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nexus/logic/bdd.hpp"
#include "nexus/logic/boolean.hpp"
#include "nexus/logic/codes.hpp"
#include "nexus/logic/fsm.hpp"
#include "nexus/logic/gatesim.hpp"
#include "nexus/logic/minimize.hpp"
#include "nexus/logic/pla.hpp"
#include "nexus/logic/sat.hpp"
#include "nexus/vlsi/cli.hpp"

namespace lg = nexus::logic;

namespace {

void usage() {
  std::cout
      << "nexus-eda: logic design and VLSI/EDA tools\n"
      << "Logic design (EY121):\n"
      << "  nexus-eda truth \"<expr>\"                     truth table, minterms, canonical SOP and POS\n"
      << "  nexus-eda minimize \"<expr>\" [--dc \"<expr>\"] [--pos]\n"
      << "  nexus-eda minimize -n <vars> -m 1,3,7 [-d 2,5] [--pos]   Quine-McCluskey table, primes, cover, K-map\n"
      << "  nexus-eda nand|nor \"<expr>\"                  NAND-only / NOR-only form\n"
      << "  nexus-eda bdd \"<expr>\" [--order a,b,c] [--dot]\n"
      << "  nexus-eda equiv \"<expr1>\" \"<expr2>\"          truth table, BDD and SAT verdicts\n"
      << "  nexus-eda cnf \"<expr>\"                       Tseitin CNF in DIMACS\n"
      << "  nexus-eda sat <file.cnf>                      CDCL SAT solver\n"
      << "  nexus-eda pla -n 3 -m \"0,1,2,4;0,5,6,7\" [-d \"...;...\"] [--rom]\n"
      << "                                               PLA (shared terms, output inversion), PAL, ROM\n"
      << "  nexus-eda gray <n> | hamming <bits>           codes\n"
      << "  nexus-eda fsm table|minimize|synth|verilog|simulate <file.fsm> [--encoding binary|gray|onehot]\n"
      << "            [--ff D|T|JK] [--inputs 0,1,1,0]\n"
      << "  nexus-eda analyze <file.seq>                  state table of a sequential circuit from its equations\n"
      << "  nexus-eda latch sr|d|msff                     gate-level waveforms of latches and flip-flops\n"
      << nexus::vlsi::cli_usage();
}

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open " + path);
  }
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

std::map<std::string, std::string> options(int argc, char** argv, int start, std::vector<std::string>& positional) {
  std::map<std::string, std::string> opt;
  for (int i = start; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.size() > 1 && a[0] == '-' && !(a.size() > 1 && std::isdigit(static_cast<unsigned char>(a[1])) != 0)) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        opt[a] = argv[++i];
      } else {
        opt[a] = "1";
      }
    } else {
      positional.push_back(a);
    }
  }
  return opt;
}

std::vector<std::uint32_t> int_list(const std::string& s) {
  std::vector<std::uint32_t> out;
  std::stringstream in(s);
  std::string item;
  while (std::getline(in, item, ',')) {
    if (!item.empty()) {
      out.push_back(static_cast<std::uint32_t>(std::stoul(item)));
    }
  }
  return out;
}

std::vector<std::string> name_list(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream in(s);
  std::string item;
  while (std::getline(in, item, ',')) {
    out.push_back(item);
  }
  return out;
}

int cmd_truth(const std::vector<std::string>& pos) {
  const auto f = lg::parse(pos.at(0));
  const auto t = lg::truth_table(f);
  std::cout << lg::format_truth_table(f);
  std::cout << "minterms: m(";
  const auto mins = lg::minterms(t);
  for (std::size_t i = 0; i < mins.size(); ++i) {
    std::cout << (i > 0 ? "," : "") << mins[i];
  }
  std::cout << ")\ncanonical SOP: " << lg::canonical_sop(f.vars, mins) << '\n';
  std::cout << "canonical POS: " << lg::canonical_pos(f.vars, lg::maxterms(t)) << '\n';
  return 0;
}

int cmd_minimize(const std::vector<std::string>& pos, const std::map<std::string, std::string>& opt) {
  std::vector<std::string> vars;
  std::vector<std::uint32_t> ones, dcs;
  std::size_t n = 0;
  if (opt.count("-n") != 0) {
    n = std::stoul(opt.at("-n"));
    for (std::size_t i = 0; i < n; ++i) {
      vars.push_back(std::string(1, static_cast<char>('a' + i)));
    }
    ones = int_list(opt.count("-m") != 0 ? opt.at("-m") : "");
    dcs = int_list(opt.count("-d") != 0 ? opt.at("-d") : "");
  } else {
    const auto f = lg::parse(pos.at(0));
    vars = f.vars;
    lg::Function dc;
    if (opt.count("--dc") != 0) {
      dc = lg::parse(opt.at("--dc"), vars);
    }
    n = vars.size();
    for (const auto m : lg::minterms(lg::truth_table(f))) {
      ones.push_back(static_cast<std::uint32_t>(m));
    }
    if (dc.expr) {
      for (const auto m : lg::minterms(lg::truth_table(dc))) {
        dcs.push_back(static_cast<std::uint32_t>(m));
      }
    }
  }
  const bool want_pos = opt.count("--pos") != 0;
  const auto r = want_pos ? lg::minimize_pos(n, ones, dcs) : lg::minimize_sop(n, ones, dcs);
  std::cout << "Quine-McCluskey tabulation" << (want_pos ? " (of the zeros, for the POS form)" : "") << ":\n";
  for (std::size_t c = 0; c < r.qm.columns.size(); ++c) {
    std::cout << "  column " << c << ":";
    for (std::size_t g = 0; g < r.qm.columns[c].groups.size(); ++g) {
      if (r.qm.columns[c].groups[g].empty()) {
        continue;
      }
      std::cout << "  [" << g << " ones]";
      for (const auto& cube : r.qm.columns[c].groups[g]) {
        std::cout << ' ' << lg::cube_to_pattern(cube, n);
      }
    }
    std::cout << '\n';
  }
  std::cout << "prime implicants:";
  for (const auto& p : r.qm.primes) {
    std::cout << "  " << lg::cube_to_string(p, vars);
  }
  std::cout << "\nessential primes:";
  for (const auto& p : r.cover.essential) {
    std::cout << "  " << lg::cube_to_string(p, vars);
  }
  std::cout << "\ncover method: " << (r.cover.method == lg::CoverMethod::Petrick ? "Petrick" : "branch and bound")
            << '\n';
  if (want_pos) {
    std::cout << "minimal POS: " << lg::pos_to_string(r.cover.cubes, vars) << '\n';
  } else {
    std::cout << "minimal SOP: " << lg::sop_to_string(r.cover.cubes, vars) << '\n';
  }
  std::cout << "terms: " << r.cover.cubes.size() << ", literals: " << r.cover.literals << '\n';
  if (n >= 2 && n <= 4 && !want_pos) {
    std::vector<std::uint8_t> values(std::size_t{1} << n, 0);
    for (const auto m : ones) {
      values[m] = 1;
    }
    for (const auto m : dcs) {
      values[m] = 2;
    }
    std::cout << '\n' << lg::karnaugh_map(vars, values, r.cover.cubes);
  }
  return 0;
}

std::vector<std::vector<std::uint32_t>> output_lists(const std::string& s) {
  std::vector<std::vector<std::uint32_t>> out;
  std::stringstream in(s);
  std::string item;
  while (std::getline(in, item, ';')) {
    out.push_back(int_list(item));
  }
  return out;
}

int cmd_pla(const std::map<std::string, std::string>& opt) {
  if (opt.count("-n") == 0 || opt.count("-m") == 0) {
    throw std::runtime_error("pla: give -n <inputs> and -m \"<minterms of F1>;<minterms of F2>;...\"");
  }
  const std::size_t n = std::stoul(opt.at("-n"));
  const auto ones = output_lists(opt.at("-m"));
  const auto dcs = output_lists(opt.count("-d") != 0 ? opt.at("-d") : "");
  std::vector<lg::MultiOutput> outs(ones.size());
  for (std::size_t i = 0; i < outs.size(); ++i) {
    outs[i].ones = ones[i];
    if (i < dcs.size()) outs[i].dont_cares = dcs[i];
  }
  std::vector<std::string> vars;
  for (std::size_t i = 0; i < n; ++i) vars.push_back(std::string(1, static_cast<char>('A' + i)));
  const auto pla = lg::minimize_pla(n, outs, true);
  const auto plain = lg::minimize_pla(n, outs, false);
  std::cout << "PLA with output inversion: " << pla.terms.size() << " product terms, " << pla.connections
            << " OR connections" << (pla.exact ? "" : " (search stopped at its node limit)") << '\n'
            << lg::pla_table(pla, vars);
  std::cout << "PLA without output inversion: " << plain.terms.size() << " product terms\n";
  const auto pal = lg::pal_terms(n, outs);
  std::size_t pal_total = 0;
  std::cout << "PAL (fixed OR array, no sharing): terms per output";
  for (const auto t : pal) {
    std::cout << ' ' << t;
    pal_total += t;
  }
  std::cout << " = " << pal_total << " AND gates\n";
  std::cout << "ROM: " << (std::size_t{1} << n) << " x " << outs.size() << " bits\n";
  if (opt.count("--rom") != 0) std::cout << lg::rom_table(n, outs);
  const bool ok = lg::pla_implements(pla, outs) && lg::pla_implements(plain, outs);
  std::cout << "check over all inputs: " << (ok ? "every output correct" : "MISMATCH") << '\n';
  return ok ? 0 : 1;
}

int cmd_bdd(const std::vector<std::string>& pos, const std::map<std::string, std::string>& opt) {
  const auto f = lg::parse(pos.at(0));
  std::vector<std::size_t> order;
  if (opt.count("--order") != 0) {
    for (const auto& name : name_list(opt.at("--order"))) {
      const auto it = std::find(f.vars.begin(), f.vars.end(), name);
      if (it == f.vars.end()) {
        throw std::runtime_error("--order: unknown variable " + name);
      }
      order.push_back(static_cast<std::size_t>(it - f.vars.begin()));
    }
  }
  lg::Bdd bdd(f.vars.size(), order);
  const auto node = bdd.from_expr(*f.expr);
  if (opt.count("--dot") != 0) {
    std::cout << bdd.to_dot(node, f.vars);
    return 0;
  }
  std::cout << "BDD nodes: " << bdd.size(node) << ", satisfying assignments: " << bdd.sat_count(node) << '\n';
  return 0;
}

int cmd_equiv(const std::vector<std::string>& pos) {
  const auto f = lg::parse(pos.at(0));
  auto names = f.vars;
  const auto g0 = lg::parse(pos.at(1));
  for (const auto& v : g0.vars) {
    if (std::find(names.begin(), names.end(), v) == names.end()) {
      names.push_back(v);
    }
  }
  const auto a = lg::parse(pos.at(0), names);
  const auto b = lg::parse(pos.at(1), names);
  const bool tt = lg::truth_table(a) == lg::truth_table(b);
  lg::Bdd bdd(names.size());
  const bool same_node = bdd.from_expr(*a.expr) == bdd.from_expr(*b.expr);
  const auto sat = lg::sat_equivalent(*a.expr, *b.expr, names.size());
  std::cout << "truth table: " << (tt ? "equivalent" : "different") << '\n';
  std::cout << "BDD: " << (same_node ? "same node" : "different nodes") << '\n';
  std::cout << "SAT miter: " << (sat.equivalent ? "UNSAT (equivalent)" : "SAT (different)") << '\n';
  if (sat.counterexample) {
    std::cout << "counterexample:";
    for (std::size_t i = 0; i < names.size(); ++i) {
      std::cout << ' ' << names[i] << '=' << ((*sat.counterexample >> (names.size() - 1 - i)) & 1U);
    }
    std::cout << '\n';
  }
  return tt == same_node && tt == sat.equivalent ? 0 : 2;
}

int cmd_fsm(const std::vector<std::string>& pos, const std::map<std::string, std::string>& opt) {
  const std::string action = pos.at(0);
  const auto f = lg::parse_fsm(read_file(pos.at(1)));
  const std::string enc_name = opt.count("--encoding") != 0 ? opt.at("--encoding") : "binary";
  const std::string ff_name = opt.count("--ff") != 0 ? opt.at("--ff") : "D";
  const auto enc = enc_name == "gray" ? lg::Encoding::Gray : (enc_name == "onehot" ? lg::Encoding::OneHot : lg::Encoding::Binary);
  const auto ff = ff_name == "T" ? lg::FlipFlop::T : (ff_name == "JK" ? lg::FlipFlop::JK : lg::FlipFlop::D);
  if (action == "table") {
    std::cout << lg::format_state_table(f);
  } else if (action == "minimize") {
    const auto m = lg::minimize_fsm(f);
    std::cout << "states: " << f.states.size() << " -> " << m.states.size() << "\n" << lg::format_state_table(m);
  } else if (action == "synth") {
    const auto m = lg::minimize_fsm(f);
    std::cout << lg::synthesize(m, enc, ff).equations();
  } else if (action == "verilog") {
    const auto m = lg::minimize_fsm(f);
    // behavioural model of the original machine, structural model synthesised from the minimised one
    std::cout << lg::to_verilog_behavioural(f) << '\n'
              << lg::to_verilog_structural(lg::synthesize(m, enc, ff), m.name + "_struct");
  } else if (action == "simulate") {
    const auto inputs = int_list(opt.count("--inputs") != 0 ? opt.at("--inputs") : "");
    const auto fsm_out = lg::simulate(f, inputs);
    const auto circ_out = lg::simulate(lg::synthesize(lg::minimize_fsm(f), enc, ff), inputs);
    for (std::size_t i = 0; i < fsm_out.size(); ++i) {
      std::cout << (i > 0 ? "," : "") << fsm_out[i];
    }
    std::cout << '\n';
    return fsm_out == circ_out ? 0 : 3;
  } else {
    throw std::runtime_error("fsm: unknown action " + action);
  }
  return 0;
}

int cmd_latch(const std::string& which) {
  using V = lg::Value;
  if (which == "sr") {
    auto g = lg::sr_nor_latch();
    g.drive("S", V::Zero, 0);
    g.drive("R", V::One, 0);
    g.drive("R", V::Zero, 6);
    g.drive("S", V::One, 12);
    g.drive("S", V::Zero, 18);
    g.drive("R", V::One, 24);
    g.drive("R", V::Zero, 30);
    g.run(36);
    std::cout << g.waveform({"S", "R", "Q", "QN"}, 36);
  } else if (which == "d") {
    auto g = lg::gated_d_latch();
    g.drive("EN", V::One, 0);
    g.drive("D", V::Zero, 0);
    g.drive("D", V::One, 8);
    g.drive("EN", V::Zero, 16);
    g.drive("D", V::Zero, 20);
    g.drive("EN", V::One, 30);
    g.run(40);
    std::cout << g.waveform({"EN", "D", "Q"}, 40);
  } else {
    auto g = lg::master_slave_d_flip_flop();
    for (std::uint64_t t = 0; t < 60; t += 10) {
      g.drive("CLK", V::Zero, t);
      g.drive("CLK", V::One, t + 5);
    }
    g.drive("D", V::One, 0);
    g.drive("D", V::Zero, 17);
    g.drive("D", V::One, 32);
    g.run(60);
    std::cout << g.waveform({"CLK", "D", "Q"}, 60);
  }
  std::cout << "('#' = 1, '_' = 0, 'x' = unknown; one column per time unit, gate delay 1)\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 0;
  }
  const std::string cmd = argv[1];
  std::vector<std::string> pos;
  const auto opt = options(argc, argv, 2, pos);
  try {
    if (cmd == "truth") {
      return cmd_truth(pos);
    }
    if (cmd == "minimize") {
      return cmd_minimize(pos, opt);
    }
    if (cmd == "nand" || cmd == "nor") {
      const auto f = lg::parse(pos.at(0));
      const auto g = cmd == "nand" ? lg::to_nand_only(f.expr) : lg::to_nor_only(f.expr);
      std::cout << lg::to_string(*g, f.vars) << "\ngates: " << lg::gate_count(*g) << '\n';
      return 0;
    }
    if (cmd == "bdd") {
      return cmd_bdd(pos, opt);
    }
    if (cmd == "equiv") {
      return cmd_equiv(pos);
    }
    if (cmd == "cnf") {
      const auto f = lg::parse(pos.at(0));
      auto t = lg::tseitin(*f.expr, f.vars.size());
      t.clauses.push_back({t.output});
      std::cout << "c variables 1.." << f.vars.size() << " are";
      for (const auto& v : f.vars) {
        std::cout << ' ' << v;
      }
      std::cout << '\n' << lg::to_dimacs(t.clauses, t.num_vars);
      return 0;
    }
    if (cmd == "sat") {
      int nv = 0;
      const auto clauses = lg::parse_dimacs(read_file(pos.at(0)), nv);
      lg::SatSolver s;
      for (int v = 0; v < nv; ++v) {
        s.new_var();
      }
      for (const auto& c : clauses) {
        s.add_clause(c);
      }
      const bool sat = s.solve() == lg::SatSolver::Result::Sat;
      std::cout << (sat ? "s SATISFIABLE" : "s UNSATISFIABLE") << '\n';
      if (sat) {
        std::cout << 'v';
        for (int v = 1; v <= nv; ++v) {
          std::cout << ' ' << (s.value(v) ? v : -v);
        }
        std::cout << " 0\n";
      }
      std::cout << "c conflicts " << s.conflicts() << " decisions " << s.decisions() << " learnt " << s.learnt_clauses()
                << '\n';
      return sat ? 10 : 20;  // SAT-competition exit codes
    }
    if (cmd == "pla") {
      return cmd_pla(opt);
    }
    if (cmd == "gray") {
      const auto n = std::stoul(pos.at(0));
      for (std::uint32_t v = 0; v < (1U << n); ++v) {
        std::cout << v << '\t' << lg::gray_encode(v) << '\n';
      }
      return 0;
    }
    if (cmd == "hamming") {
      std::vector<std::uint8_t> data;
      for (const char c : pos.at(0)) {
        data.push_back(c == '1' ? 1 : 0);
      }
      const auto code = lg::hamming_encode(data);
      std::cout << "codeword (positions 1..n): ";
      for (const auto b : code) {
        std::cout << static_cast<int>(b);
      }
      std::cout << "\nparity bits: " << lg::hamming_parity_bits(data.size()) << '\n';
      return 0;
    }
    if (cmd == "fsm") {
      return cmd_fsm(pos, opt);
    }
    if (cmd == "analyze") {
      std::cout << lg::format_state_table(lg::analyze(lg::parse_circuit(read_file(pos.at(0)))));
      return 0;
    }
    if (cmd == "latch") {
      return cmd_latch(pos.empty() ? "sr" : pos[0]);
    }
    if (nexus::vlsi::cli_handles(cmd)) {
      return nexus::vlsi::cli_run(cmd, pos, opt);
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
  usage();
  return 1;
}
