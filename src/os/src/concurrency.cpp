#include "nexus/os/concurrency.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>
#include <set>
#include <sstream>
#include <stdexcept>

namespace nexus::os {

namespace {

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r");
  if (b == std::string::npos) {
    return "";
  }
  return s.substr(b, s.find_last_not_of(" \t\r") - b + 1);
}

}  // namespace

ConcurrentProgram parse_concurrent(const std::string& text) {
  ConcurrentProgram p;
  std::istringstream in(text);
  std::string line;
  std::vector<std::map<std::string, std::size_t>> labels;
  while (std::getline(in, line)) {
    const auto hash = line.find('#');
    if (hash != std::string::npos) {
      line = line.substr(0, hash);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    std::istringstream ls(line);
    std::vector<std::string> tok;
    std::string t;
    while (ls >> t) {
      tok.push_back(t);
    }
    if (tok[0] == "shared") {
      for (std::size_t i = 1; i < tok.size(); ++i) {
        const auto eq = tok[i].find('=');
        p.shared_init[tok[i].substr(0, eq)] = eq == std::string::npos ? 0 : std::stoll(tok[i].substr(eq + 1));
      }
      continue;
    }
    if (tok[0] == "thread") {
      p.thread_names.push_back(tok.at(1));
      p.threads.emplace_back();
      labels.emplace_back();
      continue;
    }
    if (tok[0] == "assert") {
      p.asserts.emplace_back(tok.at(1), std::stoll(tok.at(2)));
      continue;
    }
    if (p.threads.empty()) {
      throw std::runtime_error("concurrent program: instruction outside a thread");
    }
    if (tok[0].back() == ':') {
      labels.back()[tok[0].substr(0, tok[0].size() - 1)] = p.threads.back().size();
      tok.erase(tok.begin());
      if (tok.empty()) {
        continue;
      }
    }
    ConcurrentProgram::Instr ins;
    ins.op = tok[0];
    ins.args.assign(tok.begin() + 1, tok.end());
    p.threads.back().push_back(ins);
  }
  for (std::size_t th = 0; th < p.threads.size(); ++th) {
    for (auto& ins : p.threads[th]) {
      if (ins.op == "jmp" || ins.op == "jeq" || ins.op == "jne") {
        const std::string& l = ins.args.back();
        const auto it = labels[th].find(l);
        if (it == labels[th].end()) {
          throw std::runtime_error("concurrent program: unknown label " + l);
        }
        ins.target = it->second;
      }
    }
    p.threads[th].push_back(ConcurrentProgram::Instr{"halt", {}, 0});  // implicit halt at the end
  }
  return p;
}

namespace {

struct Machine {
  std::vector<std::size_t> pc;
  std::vector<std::map<std::string, std::int64_t>> local;
  std::map<std::string, std::int64_t> shared;
  std::vector<bool> in_cs;
  bool operator<(const Machine& o) const {
    return std::tie(pc, local, shared, in_cs) < std::tie(o.pc, o.local, o.shared, o.in_cs);
  }
};

std::int64_t value_of(const Machine& m, std::size_t th, const std::string& name) {
  if (!name.empty() && name[0] == '%') {
    const auto it = m.local[th].find(name);
    return it == m.local[th].end() ? 0 : it->second;
  }
  if (!name.empty() && (std::isdigit(static_cast<unsigned char>(name[0])) != 0 || name[0] == '-')) {
    return std::stoll(name);
  }
  const auto it = m.shared.find(name);
  return it == m.shared.end() ? 0 : it->second;
}

void assign(Machine& m, std::size_t th, const std::string& name, std::int64_t v) {
  if (!name.empty() && name[0] == '%') {
    m.local[th][name] = v;
  } else {
    m.shared[name] = v;
  }
}

bool halted(const ConcurrentProgram& p, const Machine& m, std::size_t th) {
  return p.threads[th][m.pc[th]].op == "halt";
}

// whether thread `th` can take a step, and the step's effect
bool enabled(const ConcurrentProgram& p, const Machine& m, std::size_t th) {
  const auto& ins = p.threads[th][m.pc[th]];
  if (ins.op == "halt") {
    return false;
  }
  if (ins.op == "await") {
    return value_of(m, th, ins.args[0]) == value_of(m, th, ins.args[1]);
  }
  if (ins.op == "P") {
    return value_of(m, th, ins.args[0]) > 0;
  }
  return true;
}

Machine step(const ConcurrentProgram& p, const Machine& m, std::size_t th) {
  Machine n = m;
  const auto& ins = p.threads[th][m.pc[th]];
  std::size_t next = m.pc[th] + 1;
  const auto& a = ins.args;
  if (ins.op == "set") {
    assign(n, th, a[0], value_of(m, th, a[1]));
  } else if (ins.op == "copy" || ins.op == "load" || ins.op == "store") {
    assign(n, th, a[0], value_of(m, th, a[1]));
  } else if (ins.op == "add") {
    assign(n, th, a[0], value_of(m, th, a[0]) + value_of(m, th, a[1]));
  } else if (ins.op == "jmp") {
    next = ins.target;
  } else if (ins.op == "jeq") {
    if (value_of(m, th, a[0]) == value_of(m, th, a[1])) {
      next = ins.target;
    }
  } else if (ins.op == "jne") {
    if (value_of(m, th, a[0]) != value_of(m, th, a[1])) {
      next = ins.target;
    }
  } else if (ins.op == "await") {
    // enabled only when the condition holds; the step itself does nothing
  } else if (ins.op == "tas") {
    assign(n, th, a[0], value_of(m, th, a[1]));
    assign(n, th, a[1], 1);
  } else if (ins.op == "P") {
    assign(n, th, a[0], value_of(m, th, a[0]) - 1);
  } else if (ins.op == "V") {
    assign(n, th, a[0], value_of(m, th, a[0]) + 1);
  } else if (ins.op == "cs") {
    n.in_cs[th] = true;
  } else if (ins.op == "endcs") {
    n.in_cs[th] = false;
  } else {
    throw std::runtime_error("concurrent program: unknown op " + ins.op);
  }
  n.pc[th] = next;
  return n;
}

std::string describe(const ConcurrentProgram& p, std::size_t th, const Machine& m) {
  const auto& ins = p.threads[th][m.pc[th]];
  std::string s = p.thread_names[th] + ": " + ins.op;
  for (const auto& x : ins.args) {
    s += " " + x;
  }
  return s;
}

}  // namespace

CheckResult model_check(const ConcurrentProgram& p, std::size_t max_states) {
  CheckResult r;
  const std::size_t nt = p.threads.size();
  Machine init;
  init.pc.assign(nt, 0);
  init.local.assign(nt, {});
  init.shared = p.shared_init;
  init.in_cs.assign(nt, false);
  std::set<Machine> visited;
  std::set<std::int64_t> finals;
  struct Frame {
    Machine m;
    std::size_t next_thread;
  };
  std::vector<Frame> stack{{init, 0}};
  std::vector<std::string> path;
  visited.insert(init);
  auto record = [&](const std::string& what) {
    if (r.violation.empty()) {
      r.violation = what;
      r.counterexample = path;
    }
  };
  while (!stack.empty()) {
    Frame& f = stack.back();
    if (f.next_thread == 0) {
      // first visit of this state: check properties
      const auto in_cs = static_cast<std::size_t>(std::count(f.m.in_cs.begin(), f.m.in_cs.end(), true));
      if (in_cs > 1) {
        r.mutual_exclusion = false;
        record("two threads in the critical section");
      }
      bool all_halted = true, any_enabled = false;
      for (std::size_t t = 0; t < nt; ++t) {
        all_halted = all_halted && halted(p, f.m, t);
        any_enabled = any_enabled || enabled(p, f.m, t);
      }
      if (all_halted) {
        for (const auto& [var, want] : p.asserts) {
          if (value_of(f.m, 0, var) != want) {
            r.assertions_hold = false;
            record("assertion " + var + " == " + std::to_string(want) + " fails (value " +
                   std::to_string(value_of(f.m, 0, var)) + ")");
          }
        }
        if (!p.asserts.empty()) {
          finals.insert(value_of(f.m, 0, p.asserts[0].first));
        }
      } else if (!any_enabled) {
        r.deadlock_free = false;
        record("deadlock: no thread can proceed");
      }
    }
    // next successor
    bool pushed = false;
    while (f.next_thread < nt) {
      const std::size_t t = f.next_thread++;
      if (!enabled(p, f.m, t)) {
        continue;
      }
      ++r.transitions;
      Machine n = step(p, f.m, t);
      if (visited.insert(n).second) {
        if (visited.size() > max_states) {
          throw std::runtime_error("model_check: state limit exceeded");
        }
        path.push_back(describe(p, t, f.m));
        stack.push_back(Frame{n, 0});
        pushed = true;
        break;
      }
    }
    if (!pushed) {
      stack.pop_back();
      if (!path.empty() && !stack.empty()) {
        path.pop_back();
      }
    }
  }
  r.states = visited.size();
  r.final_values_seen.assign(finals.begin(), finals.end());
  return r;
}

// ---------------------------------------------------------------- real threads

void PetersonLock::lock(int self) {
  const int other = 1 - self;
  flag_[self].store(true, std::memory_order_seq_cst);
  turn_.store(other, std::memory_order_seq_cst);
  while (flag_[other].load(std::memory_order_seq_cst) && turn_.load(std::memory_order_seq_cst) == other) {
    // busy wait
  }
}

void PetersonLock::unlock(int self) { flag_[self].store(false, std::memory_order_seq_cst); }

void Semaphore::wait() {
  std::unique_lock<std::mutex> lk(m_);
  cv_.wait(lk, [&] { return count_ > 0; });
  --count_;
}

void Semaphore::signal() {
  {
    std::lock_guard<std::mutex> lk(m_);
    ++count_;
  }
  cv_.notify_one();
}

BoundedBuffer::BoundedBuffer(std::size_t capacity)
    : items_(capacity), empty_(static_cast<int>(capacity)), full_(0), mutex_(1) {}

void BoundedBuffer::put(int item) {
  empty_.wait();
  mutex_.wait();
  items_[in_] = item;
  in_ = (in_ + 1) % items_.size();
  mutex_.signal();
  full_.signal();
}

int BoundedBuffer::take() {
  full_.wait();
  mutex_.wait();
  const int item = items_[out_];
  out_ = (out_ + 1) % items_.size();
  mutex_.signal();
  empty_.signal();
  return item;
}

void ReadersWriters::start_read() {
  std::unique_lock<std::mutex> lk(m_);
  cv_.wait(lk, [&] { return !writer_.load() && waiting_writers_ == 0; });
  ++readers_;
}

void ReadersWriters::end_read() {
  std::lock_guard<std::mutex> lk(m_);
  if (--readers_ == 0) {
    cv_.notify_all();
  }
}

void ReadersWriters::start_write() {
  std::unique_lock<std::mutex> lk(m_);
  ++waiting_writers_;
  cv_.wait(lk, [&] { return !writer_.load() && readers_.load() == 0; });
  --waiting_writers_;
  writer_ = true;
}

void ReadersWriters::end_write() {
  std::lock_guard<std::mutex> lk(m_);
  writer_ = false;
  cv_.notify_all();
}

}  // namespace nexus::os
