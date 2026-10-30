#include "nexus/compiler/passes/modulo_schedule.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

namespace nexus::compiler::passes {

namespace {

constexpr std::int64_t kUnset = std::numeric_limits<std::int64_t>::min();

std::string strip(std::string line) {
  if (const auto c = line.find('#'); c != std::string::npos) line.erase(c);
  for (auto& ch : line) {
    if (ch == ',') ch = ' ';
  }
  return line;
}

std::int64_t ceil_div(std::int64_t a, std::int64_t b) { return (a + b - 1) / b; }

// Longest-path closure with weights latency(from) - ii * distance; true when some cycle has positive weight.
bool positive_cycle(const Ddg& ddg, std::int64_t ii) {
  const std::size_t n = ddg.operations.size();
  constexpr std::int64_t none = std::numeric_limits<std::int64_t>::min() / 4;
  std::vector<std::vector<std::int64_t>> d(n, std::vector<std::int64_t>(n, none));
  for (const auto& e : ddg.edges) {
    d[e.from][e.to] = std::max(d[e.from][e.to], ddg.operations[e.from].latency - ii * e.distance);
  }
  for (std::size_t k = 0; k < n; ++k) {
    for (std::size_t i = 0; i < n; ++i) {
      if (d[i][k] == none) continue;
      for (std::size_t j = 0; j < n; ++j) {
        if (d[k][j] == none) continue;
        d[i][j] = std::max(d[i][j], d[i][k] + d[k][j]);
      }
    }
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (d[i][i] > 0) return true;
  }
  return false;
}

}  // namespace

DdgParse parse_ddg(const std::string& text) {
  DdgParse parse;
  Ddg ddg;
  std::map<std::string, std::size_t> index;
  std::istringstream lines(text);
  std::string line;
  std::size_t number = 0;
  auto fail = [&](const std::string& message) {
    parse.error = "line " + std::to_string(number) + ": " + message;
    return parse;
  };
  while (std::getline(lines, line)) {
    ++number;
    std::istringstream words(strip(line));
    std::string head;
    if (!(words >> head)) continue;
    if (head == "resources") {
      std::string name;
      std::int64_t units = 0;
      while (words >> name >> units) {
        if (units <= 0) return fail("a resource needs at least one unit");
        ddg.resources[name] = units;
      }
    } else if (head == "op") {
      DdgOperation op;
      std::string keyword;
      if (!(words >> op.name >> op.resource)) return fail("op <name> <resource> [latency N]");
      if (words >> keyword) {
        if (keyword != "latency" || !(words >> op.latency) || op.latency < 1) return fail("latency must be >= 1");
      }
      if (!ddg.resources.count(op.resource)) return fail("unknown resource '" + op.resource + "'");
      if (index.count(op.name)) return fail("duplicate operation '" + op.name + "'");
      index[op.name] = ddg.operations.size();
      ddg.operations.push_back(op);
    } else if (head == "edge") {
      std::string from;
      std::string arrow;
      std::string to;
      std::string keyword;
      if (!(words >> from >> arrow >> to) || arrow != "->") return fail("edge <a> -> <b> [distance d]");
      if (!index.count(from) || !index.count(to)) return fail("edge between unknown operations");
      DdgEdge edge{index[from], index[to], 0};
      if (words >> keyword) {
        if (keyword != "distance" || !(words >> edge.distance) || edge.distance < 0) return fail("bad distance");
      }
      if (edge.from == edge.to && edge.distance == 0) return fail("an operation cannot depend on itself in the same iteration");
      ddg.edges.push_back(edge);
    } else {
      return fail("unknown keyword '" + head + "'");
    }
  }
  if (ddg.operations.empty()) {
    parse.error = "no operations";
    return parse;
  }
  if (positive_cycle(ddg, std::numeric_limits<std::int32_t>::max())) {
    parse.error = "a dependence cycle with distance 0 can never be scheduled";
    return parse;
  }
  parse.ddg = std::move(ddg);
  return parse;
}

std::int64_t res_mii(const Ddg& ddg) {
  std::map<std::string, std::int64_t> uses;
  for (const auto& op : ddg.operations) ++uses[op.resource];
  std::int64_t mii = 1;
  for (const auto& [resource, count] : uses) mii = std::max(mii, ceil_div(count, ddg.resources.at(resource)));
  return mii;
}

std::int64_t rec_mii(const Ddg& ddg) {
  std::int64_t total = 1;
  for (const auto& op : ddg.operations) total += op.latency;
  for (std::int64_t ii = 1; ii <= total; ++ii) {
    if (!positive_cycle(ddg, ii)) return ii;
  }
  return total;
}

std::string verify_schedule(const Ddg& ddg, const ModuloSchedule& s) {
  if (s.ii <= 0 || s.time.size() != ddg.operations.size()) return "no schedule";
  for (const auto& e : ddg.edges) {
    const auto need = s.time[e.from] + ddg.operations[e.from].latency - s.ii * e.distance;
    if (s.time[e.to] < need) {
      return "dependence " + ddg.operations[e.from].name + " -> " + ddg.operations[e.to].name + " violated";
    }
  }
  std::map<std::pair<std::string, std::int64_t>, std::int64_t> used;
  for (std::size_t v = 0; v < ddg.operations.size(); ++v) {
    const auto key = std::pair{ddg.operations[v].resource, s.time[v] % s.ii};
    if (++used[key] > ddg.resources.at(key.first)) {
      return "resource " + key.first + " oversubscribed in slot " + std::to_string(key.second);
    }
  }
  return {};
}

ModuloSchedule modulo_schedule(const Ddg& ddg, std::int64_t max_ii_extra) {
  ModuloSchedule s;
  const std::size_t n = ddg.operations.size();
  s.res_mii = res_mii(ddg);
  s.rec_mii = rec_mii(ddg);
  s.mii = std::max(s.res_mii, s.rec_mii);
  std::vector<std::vector<std::size_t>> preds(n);
  std::vector<std::vector<std::size_t>> succs(n);
  for (std::size_t k = 0; k < ddg.edges.size(); ++k) {
    preds[ddg.edges[k].to].push_back(k);
    succs[ddg.edges[k].from].push_back(k);
  }
  for (std::int64_t ii = s.mii; ii <= s.mii + max_ii_extra; ++ii) {
    // height-based priority: longest path to the end of the iteration
    std::vector<std::int64_t> height(n, 0);
    for (std::size_t round = 0; round < n + 1; ++round) {
      for (const auto& e : ddg.edges) {
        height[e.from] = std::max(height[e.from], height[e.to] + ddg.operations[e.from].latency - ii * e.distance);
      }
    }
    std::vector<std::int64_t> time(n, kUnset);
    std::vector<std::int64_t> previous(n, kUnset);
    std::map<std::pair<std::string, std::int64_t>, std::vector<std::size_t>> table;  // modulo reservation table
    auto unplace = [&](std::size_t v) {
      auto& slot = table[{ddg.operations[v].resource, time[v] % ii}];
      slot.erase(std::find(slot.begin(), slot.end(), v));
      time[v] = kUnset;
      ++s.evictions;
    };
    std::size_t budget = 8 * n + 8;
    std::size_t placed = 0;
    while (placed < n && budget-- > 0) {
      std::size_t v = n;
      for (std::size_t u = 0; u < n; ++u) {
        if (time[u] == kUnset && (v == n || height[u] > height[v])) v = u;
      }
      std::int64_t estart = 0;
      for (const auto k : preds[v]) {
        const auto& e = ddg.edges[k];
        if (time[e.from] != kUnset) {
          estart = std::max(estart, time[e.from] + ddg.operations[e.from].latency - ii * e.distance);
        }
      }
      const auto& resource = ddg.operations[v].resource;
      const auto units = ddg.resources.at(resource);
      std::int64_t chosen = kUnset;
      for (std::int64_t t = estart; t < estart + ii; ++t) {
        if (static_cast<std::int64_t>(table[{resource, t % ii}].size()) < units) {
          chosen = t;
          break;
        }
      }
      if (chosen == kUnset) {
        chosen = (previous[v] == kUnset || estart > previous[v]) ? estart : previous[v] + 1;
        auto& slot = table[{resource, chosen % ii}];
        if (static_cast<std::int64_t>(slot.size()) >= units) {
          const auto victim = slot.front();
          unplace(victim);
          --placed;
        }
      }
      time[v] = chosen;
      previous[v] = chosen;
      table[{resource, chosen % ii}].push_back(v);
      ++placed;
      ++s.attempts;
      // successors whose dependence is now violated go back to the queue
      for (const auto k : succs[v]) {
        const auto& e = ddg.edges[k];
        if (e.to != v && time[e.to] != kUnset && time[e.to] < chosen + ddg.operations[v].latency - ii * e.distance) {
          unplace(e.to);
          --placed;
        }
      }
    }
    if (placed < n) continue;
    const auto earliest = *std::min_element(time.begin(), time.end());
    for (auto& t : time) t -= earliest;
    s.ii = ii;
    s.time = time;
    if (!verify_schedule(ddg, s).empty()) continue;
    std::int64_t last_issue = 0;
    std::int64_t last_result = 0;
    for (std::size_t v = 0; v < n; ++v) {
      last_issue = std::max(last_issue, time[v]);
      last_result = std::max(last_result, time[v] + ddg.operations[v].latency);
    }
    s.stages = last_issue / ii + 1;
    s.iteration_length = last_result;
    s.success = true;
    return s;
  }
  s.error = "no modulo schedule up to II = " + std::to_string(s.mii + max_ii_extra);
  return s;
}

std::string print_schedule(const Ddg& ddg, const ModuloSchedule& s) {
  std::ostringstream out;
  out << "ResMII " << s.res_mii << ", RecMII " << s.rec_mii << ", MII " << s.mii << '\n';
  if (!s.success) {
    out << s.error << '\n';
    return out.str();
  }
  out << "II " << s.ii << (s.ii == s.mii ? " (= MII)" : "") << ", " << s.stages << " stages, " << s.attempts
      << " placements, " << s.evictions << " evictions\n";
  out << "schedule of one iteration:\n";
  for (std::size_t v = 0; v < ddg.operations.size(); ++v) {
    out << "  " << ddg.operations[v].name << "  cycle " << s.time[v] << "  stage " << s.time[v] / s.ii << "  ("
        << ddg.operations[v].resource << ", latency " << ddg.operations[v].latency << ")\n";
  }
  out << "kernel (one row per cycle of the steady state; [k] = operation of the iteration started k stages ago):\n";
  for (std::int64_t row = 0; row < s.ii; ++row) {
    out << "  " << row << ':';
    for (std::size_t v = 0; v < ddg.operations.size(); ++v) {
      if (s.time[v] % s.ii == row) out << ' ' << ddg.operations[v].name << '[' << s.time[v] / s.ii << ']';
    }
    out << '\n';
  }
  out << "prologue and epilogue: " << s.stages - 1 << " stage(s) each\n";
  out << "one iteration takes " << s.iteration_length << " cycles; software pipelining starts a new one every " << s.ii
      << " cycle(s), " << static_cast<double>(s.iteration_length) / static_cast<double>(s.ii)
      << "x the rate of running iterations back to back\n";
  return out.str();
}

}  // namespace nexus::compiler::passes
