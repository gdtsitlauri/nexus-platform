#include "nexus/sim/vector/model.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <set>
#include <sstream>

namespace nexus::sim::vector {

namespace {

enum class Unit { LoadStore = 0, Add = 1, Multiply = 2, Divide = 3, None = 4 };
constexpr std::size_t kStartup[4] = {12, 6, 7, 20};
constexpr const char* kUnitName[4] = {"load/store", "add", "multiply", "divide"};

struct Instruction {
  std::string op;
  std::vector<std::string> args;
  std::size_t line = 0;
};

struct ArrayInfo {
  std::size_t base = 0;
  std::size_t size = 0;
};

std::string upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::vector<std::string> split_args(const std::string& text) {
  std::vector<std::string> out;
  std::string current;
  for (const char c : text) {
    if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
      if (!current.empty()) out.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty()) out.push_back(current);
  return out;
}

Unit unit_of(const std::string& op) {
  if (op == "LV" || op == "LVWS" || op == "SV" || op == "SVWS") return Unit::LoadStore;
  if (op == "ADDVV" || op == "SUBVV" || op == "ADDVS" || op == "SUBVS") return Unit::Add;
  if (op == "MULVV" || op == "MULVS") return Unit::Multiply;
  if (op == "DIVVV" || op == "DIVVS") return Unit::Divide;
  return Unit::None;
}

}  // namespace

std::vector<double> daxpy_reference(double a, const std::vector<double>& x, std::vector<double> y) {
  for (std::size_t i = 0; i < y.size() && i < x.size(); ++i) y[i] = a * x[i] + y[i];
  return y;
}

Result run(const std::string& program_text, const Options& options) {
  Result result;
  auto fail = [&](std::size_t line, const std::string& message) {
    result.error = (line ? "line " + std::to_string(line) + ": " : "") + message;
    return result;
  };
  if (options.mvl == 0 || options.lanes == 0) return fail(0, "MVL and lanes must be positive");

  std::map<std::string, std::int64_t> params = options.params;
  std::map<std::string, ArrayInfo> arrays;
  std::vector<std::string> array_order;
  std::vector<double> memory;
  std::vector<double> f(16, 0.0);
  std::vector<std::int64_t> r(16, 0);
  std::vector<Instruction> program;
  std::map<std::string, std::size_t> labels;

  auto number = [&](const std::string& text) -> std::optional<std::int64_t> {
    if (const auto it = params.find(text); it != params.end()) return it->second;
    try {
      std::size_t used = 0;
      const auto v = std::stoll(text, &used);
      if (used == text.size()) return v;
    } catch (...) {
    }
    return std::nullopt;
  };

  // ---- parse
  std::istringstream lines(program_text);
  std::string raw;
  std::size_t line_no = 0;
  while (std::getline(lines, raw)) {
    ++line_no;
    if (const auto c = raw.find_first_of(";#"); c != std::string::npos) raw.erase(c);
    std::istringstream words(raw);
    std::string head;
    if (!(words >> head)) continue;
    if (head.back() == ':') {
      labels[head.substr(0, head.size() - 1)] = program.size();
      if (!(words >> head)) continue;
    }
    std::string rest;
    std::getline(words, rest);
    const auto args = split_args(rest);
    if (head == ".param") {
      if (args.size() != 2 || !number(args[1])) return fail(line_no, ".param <name> <value>");
      if (!options.params.count(args[0])) params[args[0]] = *number(args[1]);
    } else if (head == ".array") {
      if (args.size() < 3 || !number(args[1]) || *number(args[1]) <= 0) return fail(line_no, ".array <name> <size> <init>");
      const auto size = static_cast<std::size_t>(*number(args[1]));
      ArrayInfo info{memory.size(), size};
      for (std::size_t i = 0; i < size; ++i) {
        double v = 0;
        if (args[2] == "ramp") v = static_cast<double>(i + 1);
        else if (args[2] == "const" && args.size() > 3) v = std::stod(args[3]);
        else if (args[2] != "zero") return fail(line_no, "array initialiser must be ramp, const <v> or zero");
        memory.push_back(v);
      }
      arrays[args[0]] = info;
      array_order.push_back(args[0]);
    } else if (head == ".scalar") {
      if (args.size() != 2 || args[0].size() < 2 || std::toupper(static_cast<unsigned char>(args[0][0])) != 'F') {
        return fail(line_no, ".scalar F<k> <value>");
      }
      const auto k = std::stoul(args[0].substr(1));
      if (k >= f.size()) return fail(line_no, "no such scalar register");
      f[k] = std::stod(args[1]);
      result.scalars[upper(args[0])] = f[k];
    } else {
      program.push_back(Instruction{upper(head), args, line_no});
    }
  }

  auto reg = [&](const std::string& text, char kind, std::size_t limit) -> std::optional<std::size_t> {
    if (text.size() < 2 || std::toupper(static_cast<unsigned char>(text[0])) != kind) return std::nullopt;
    try {
      const auto k = std::stoul(text.substr(1));
      if (k < limit) return k;
    } catch (...) {
    }
    return std::nullopt;
  };

  // ---- execute with the timing model
  std::vector<std::vector<double>> v(8, std::vector<double>(options.mvl, 0.0));
  std::size_t vl = options.mvl;
  std::size_t issue = 0;
  std::size_t unit_free[4] = {0, 0, 0, 0};
  std::vector<std::size_t> first_ready(8, 0);
  std::vector<std::size_t> last_ready(8, 0);
  // convoy bookkeeping
  std::set<int> convoy_units;
  std::set<std::size_t> convoy_writes;
  bool convoy_open = false;
  std::size_t convoy_vl = 0;
  auto close_convoy = [&] {
    if (!convoy_open) return;
    ++result.convoys;
    result.chimes_estimate += convoy_vl;
    convoy_units.clear();
    convoy_writes.clear();
    convoy_open = false;
  };

  std::size_t pc = 0;
  std::size_t executed = 0;
  while (pc < program.size()) {
    if (++executed > options.max_instructions) return fail(0, "instruction limit reached");
    const auto& ins = program[pc];
    const auto& a = ins.args;
    auto bad = [&](const std::string& what) { return fail(ins.line, ins.op + ": " + what); };
    const Unit unit = unit_of(ins.op);
    if (unit == Unit::None) {
      // scalar instruction: one issue slot
      ++result.scalar_instructions;
      const std::size_t t = issue;
      issue = t + 1;
      std::size_t next = pc + 1;
      if (ins.op == "HALT") {
        break;
      } else if (ins.op == "LA") {
        const auto d = a.size() == 2 ? reg(a[0], 'R', 16) : std::nullopt;
        if (!d || !arrays.count(a[1])) return bad("LA Rd, <array>");
        r[*d] = static_cast<std::int64_t>(arrays[a[1]].base);
      } else if (ins.op == "LI") {
        const auto d = a.size() == 2 ? reg(a[0], 'R', 16) : std::nullopt;
        if (!d || !number(a[1])) return bad("LI Rd, <value>");
        r[*d] = *number(a[1]);
      } else if (ins.op == "ADDI") {
        const auto d = a.size() == 3 ? reg(a[0], 'R', 16) : std::nullopt;
        const auto s = a.size() == 3 ? reg(a[1], 'R', 16) : std::nullopt;
        if (!d || !s || !number(a[2])) return bad("ADDI Rd, Rs, imm");
        r[*d] = r[*s] + *number(a[2]);
      } else if (ins.op == "SETVL") {
        const auto s = a.size() == 1 ? reg(a[0], 'R', 16) : std::nullopt;
        if (!s) return bad("SETVL Rs");
        close_convoy();
        vl = static_cast<std::size_t>(std::clamp<std::int64_t>(r[*s], 0, static_cast<std::int64_t>(options.mvl)));
        ++result.strips;
      } else if (ins.op == "ADDVL" || ins.op == "SUBVL") {
        const auto d = a.size() == 1 ? reg(a[0], 'R', 16) : std::nullopt;
        if (!d) return bad(ins.op + " Rd");
        r[*d] += (ins.op == "ADDVL" ? 1 : -1) * static_cast<std::int64_t>(vl);
      } else if (ins.op == "BNEZ") {
        const auto s = a.size() == 2 ? reg(a[0], 'R', 16) : std::nullopt;
        if (!s || !labels.count(a[1])) return bad("BNEZ Rs, label");
        if (r[*s] != 0) next = labels[a[1]];
      } else {
        return bad("unknown instruction");
      }
      if (options.trace) result.trace_lines.push_back("cycle " + std::to_string(t) + ": " + ins.op);
      pc = next;
      continue;
    }

    // vector instruction
    ++result.vector_instructions;
    const auto u = static_cast<std::size_t>(unit);
    std::vector<std::size_t> sources;
    std::optional<std::size_t> dest;
    if (ins.op == "LV" || ins.op == "LVWS" || ins.op == "SV" || ins.op == "SVWS") {
      const bool strided = ins.op.size() == 4;
      const auto vr = a.size() == (strided ? 3U : 2U) ? reg(a[0], 'V', 8) : std::nullopt;
      const auto base = vr ? reg(a[1], 'R', 16) : std::nullopt;
      const auto stride_reg = strided && base ? reg(a[2], 'R', 16) : std::nullopt;
      if (!vr || !base || (strided && !stride_reg)) return bad("operands");
      const std::int64_t stride = strided ? r[*stride_reg] : 1;
      for (std::size_t i = 0; i < vl; ++i) {
        const std::int64_t address = r[*base] + static_cast<std::int64_t>(i) * stride;
        if (address < 0 || static_cast<std::size_t>(address) >= memory.size()) return bad("address out of range");
        if (ins.op[0] == 'L') v[*vr][i] = memory[static_cast<std::size_t>(address)];
        else memory[static_cast<std::size_t>(address)] = v[*vr][i];
      }
      if (ins.op[0] == 'L') dest = *vr;
      else sources.push_back(*vr);
    } else {
      const bool scalar_operand = ins.op.back() == 'S';
      const auto d = a.size() == 3 ? reg(a[0], 'V', 8) : std::nullopt;
      const auto x = d ? reg(a[1], 'V', 8) : std::nullopt;
      const auto y = x ? (scalar_operand ? reg(a[2], 'F', 16) : reg(a[2], 'V', 8)) : std::nullopt;
      if (!d || !x || !y) return bad("operands");
      const std::string kind = ins.op.substr(0, 3);
      for (std::size_t i = 0; i < vl; ++i) {
        const double lhs = v[*x][i];
        const double rhs = scalar_operand ? f[*y] : v[*y][i];
        double out = 0;
        if (kind == "ADD") out = lhs + rhs;
        else if (kind == "SUB") out = lhs - rhs;
        else if (kind == "MUL") out = lhs * rhs;
        else out = lhs / rhs;
        v[*d][i] = out;
      }
      dest = *d;
      sources.push_back(*x);
      if (!scalar_operand) sources.push_back(*y);
    }
    // convoy: a new convoy starts on a structural hazard, or (without chaining) on a dependence inside it
    bool conflict = convoy_units.count(static_cast<int>(u)) != 0;
    if (!options.chaining) {
      for (const auto s : sources) conflict = conflict || convoy_writes.count(s) != 0;
    }
    if (conflict) close_convoy();
    if (!convoy_open) {
      convoy_open = true;
      convoy_vl = vl;
    }
    convoy_units.insert(static_cast<int>(u));
    if (dest) convoy_writes.insert(*dest);
    // cycle-level schedule
    std::size_t start = std::max(issue, unit_free[u]);
    for (const auto s : sources) start = std::max(start, options.chaining ? first_ready[s] : last_ready[s]);
    if (dest) start = std::max(start, first_ready[*dest]);  // keep register writes in order
    const std::size_t busy = (vl + options.lanes - 1) / options.lanes;
    unit_free[u] = start + busy;
    const std::size_t complete = start + kStartup[u] + busy;
    if (dest) {
      first_ready[*dest] = start + kStartup[u];
      last_ready[*dest] = complete;
    }
    result.cycles = std::max(result.cycles, complete);
    result.elements += vl;
    issue = start + 1;
    if (options.trace) {
      result.trace_lines.push_back(ins.op + " VL=" + std::to_string(vl) + " on " + kUnitName[u] + ": start " +
                                   std::to_string(start) + ", first result " + std::to_string(start + kStartup[u]) +
                                   ", done " + std::to_string(complete));
    }
    ++pc;
  }
  close_convoy();
  result.cycles = std::max(result.cycles, issue);
  for (const auto& name : array_order) {
    const auto& info = arrays[name];
    result.arrays[name] = std::vector<double>(memory.begin() + static_cast<std::ptrdiff_t>(info.base),
                                              memory.begin() + static_cast<std::ptrdiff_t>(info.base + info.size));
  }
  result.success = true;
  return result;
}

}  // namespace nexus::sim::vector
