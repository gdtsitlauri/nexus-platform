#include "nexus/os/resources.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
#include <set>
#include <stdexcept>

namespace nexus::os {

// ---------------------------------------------------------------- deadlock

Matrix BankerState::need() const {
  Matrix n = max;
  for (std::size_t i = 0; i < n.size(); ++i) {
    for (std::size_t j = 0; j < n[i].size(); ++j) {
      n[i][j] -= allocation[i][j];
    }
  }
  return n;
}

namespace {

bool fits(const std::vector<int>& a, const std::vector<int>& b) {  // a <= b elementwise
  for (std::size_t j = 0; j < a.size(); ++j) {
    if (a[j] > b[j]) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<std::vector<std::size_t>> safe_sequence(const BankerState& s) {
  const auto need = s.need();
  std::vector<int> work = s.available;
  std::vector<bool> finished(s.max.size(), false);
  std::vector<std::size_t> seq;
  for (bool progress = true; progress;) {
    progress = false;
    for (std::size_t i = 0; i < s.max.size(); ++i) {
      if (!finished[i] && fits(need[i], work)) {
        for (std::size_t j = 0; j < work.size(); ++j) {
          work[j] += s.allocation[i][j];  // the process finishes and releases its allocation
        }
        finished[i] = true;
        seq.push_back(i);
        progress = true;
      }
    }
  }
  if (seq.size() != s.max.size()) {
    return std::nullopt;
  }
  return seq;
}

bool request_resources(BankerState& s, std::size_t p, const std::vector<int>& request, std::string* reason) {
  const auto need = s.need();
  if (!fits(request, need[p])) {
    if (reason != nullptr) *reason = "request exceeds the declared maximum";
    return false;
  }
  if (!fits(request, s.available)) {
    if (reason != nullptr) *reason = "not enough available: the process must wait";
    return false;
  }
  BankerState trial = s;
  for (std::size_t j = 0; j < request.size(); ++j) {
    trial.available[j] -= request[j];
    trial.allocation[p][j] += request[j];
  }
  if (!safe_sequence(trial)) {
    if (reason != nullptr) *reason = "granting would leave an unsafe state";
    return false;
  }
  s = trial;
  if (reason != nullptr) *reason = "granted";
  return true;
}

bool safe_by_enumeration(const BankerState& s) {
  if (s.max.size() > 8) {
    throw std::runtime_error("safe_by_enumeration: at most 8 processes");
  }
  const auto need = s.need();
  std::vector<std::size_t> order(s.max.size());
  std::iota(order.begin(), order.end(), 0);
  do {
    std::vector<int> work = s.available;
    bool ok = true;
    for (const auto i : order) {
      if (!fits(need[i], work)) {
        ok = false;
        break;
      }
      for (std::size_t j = 0; j < work.size(); ++j) {
        work[j] += s.allocation[i][j];
      }
    }
    if (ok) {
      return true;
    }
  } while (std::next_permutation(order.begin(), order.end()));
  return false;
}

std::vector<std::size_t> detect_deadlock(const std::vector<int>& available, const Matrix& allocation,
                                         const Matrix& request) {
  std::vector<int> work = available;
  std::vector<bool> finished(allocation.size(), false);
  for (std::size_t i = 0; i < allocation.size(); ++i) {
    finished[i] = std::all_of(allocation[i].begin(), allocation[i].end(), [](int x) { return x == 0; });
  }
  for (bool progress = true; progress;) {
    progress = false;
    for (std::size_t i = 0; i < allocation.size(); ++i) {
      if (!finished[i] && fits(request[i], work)) {
        for (std::size_t j = 0; j < work.size(); ++j) {
          work[j] += allocation[i][j];
        }
        finished[i] = true;
        progress = true;
      }
    }
  }
  std::vector<std::size_t> dead;
  for (std::size_t i = 0; i < finished.size(); ++i) {
    if (!finished[i]) {
      dead.push_back(i);
    }
  }
  return dead;
}

std::optional<std::vector<std::size_t>> wait_for_cycle(const std::vector<std::vector<std::size_t>>& g) {
  std::vector<int> color(g.size(), 0);
  std::vector<std::size_t> stack;
  std::optional<std::vector<std::size_t>> cycle;
  std::function<bool(std::size_t)> dfs = [&](std::size_t u) {
    color[u] = 1;
    stack.push_back(u);
    for (const auto v : g[u]) {
      if (color[v] == 1) {
        const auto it = std::find(stack.begin(), stack.end(), v);
        cycle = std::vector<std::size_t>(it, stack.end());
        return true;
      }
      if (color[v] == 0 && dfs(v)) {
        return true;
      }
    }
    stack.pop_back();
    color[u] = 2;
    return false;
  };
  for (std::size_t u = 0; u < g.size(); ++u) {
    if (color[u] == 0 && dfs(u)) {
      return cycle;
    }
  }
  return std::nullopt;
}

// ---------------------------------------------------------------- contiguous allocation

PartitionAllocator::PartitionAllocator(std::size_t memory, Fit fit) : memory_(memory), fit_(fit) {
  blocks_.push_back(Block{0, memory, -1});
}

std::optional<std::size_t> PartitionAllocator::allocate(std::size_t size, int owner) {
  if (size == 0) {
    return std::nullopt;
  }
  std::size_t pick = blocks_.size();
  auto better = [&](std::size_t i) {
    if (pick == blocks_.size()) {
      return true;
    }
    if (fit_ == Fit::Best) {
      return blocks_[i].size < blocks_[pick].size;
    }
    if (fit_ == Fit::Worst) {
      return blocks_[i].size > blocks_[pick].size;
    }
    return false;
  };
  if (fit_ == Fit::Next) {
    // first hole at or after the end of the previous allocation, wrapping around
    for (int pass = 0; pass < 2 && pick == blocks_.size(); ++pass) {
      for (std::size_t i = 0; i < blocks_.size(); ++i) {
        const bool after = pass == 0 ? blocks_[i].start >= next_start_ : blocks_[i].start < next_start_;
        if (after && blocks_[i].owner < 0 && blocks_[i].size >= size) {
          pick = i;
          break;
        }
      }
    }
  } else {
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
      if (blocks_[i].owner < 0 && blocks_[i].size >= size && better(i)) {
        pick = i;
        if (fit_ == Fit::First) {
          break;
        }
      }
    }
  }
  if (pick == blocks_.size()) {
    return std::nullopt;
  }
  const std::size_t start = blocks_[pick].start;
  if (blocks_[pick].size > size) {
    blocks_.insert(blocks_.begin() + static_cast<long>(pick) + 1,
                   Block{start + size, blocks_[pick].size - size, -1});
  }
  blocks_[pick].size = size;
  blocks_[pick].owner = owner;
  next_start_ = start + size;
  return start;
}

void PartitionAllocator::release(int owner) {
  for (auto& b : blocks_) {
    if (b.owner == owner) {
      b.owner = -1;
    }
  }
  // coalesce adjacent holes
  std::vector<Block> merged;
  for (const auto& b : blocks_) {
    if (!merged.empty() && merged.back().owner < 0 && b.owner < 0) {
      merged.back().size += b.size;
    } else {
      merged.push_back(b);
    }
  }
  blocks_ = merged;
}

void PartitionAllocator::compact() {
  std::vector<Block> out;
  std::size_t addr = 0;
  for (const auto& b : blocks_) {
    if (b.owner >= 0) {
      out.push_back(Block{addr, b.size, b.owner});
      addr += b.size;
    }
  }
  if (addr < memory_) {
    out.push_back(Block{addr, memory_ - addr, -1});
  }
  blocks_ = out;
}

std::size_t PartitionAllocator::free_total() const {
  std::size_t f = 0;
  for (const auto& b : blocks_) {
    f += b.owner < 0 ? b.size : 0;
  }
  return f;
}

std::size_t PartitionAllocator::largest_hole() const {
  std::size_t f = 0;
  for (const auto& b : blocks_) {
    f = b.owner < 0 ? std::max(f, b.size) : f;
  }
  return f;
}

std::size_t PartitionAllocator::holes() const {
  return static_cast<std::size_t>(std::count_if(blocks_.begin(), blocks_.end(), [](const Block& b) { return b.owner < 0; }));
}

double PartitionAllocator::external_fragmentation() const {
  const auto f = free_total();
  return f == 0 ? 0.0 : 1.0 - static_cast<double>(largest_hole()) / static_cast<double>(f);
}

BuddyAllocator::BuddyAllocator(std::size_t order) : order_(order), free_(order + 1) { free_[order].push_back(0); }

std::optional<std::size_t> BuddyAllocator::allocate(std::size_t size) {
  std::size_t k = 0;
  while ((std::size_t{1} << k) < size) {
    ++k;
  }
  std::size_t j = k;
  while (j <= order_ && free_[j].empty()) {
    ++j;
  }
  if (j > order_) {
    return std::nullopt;
  }
  std::size_t addr = free_[j].back();
  free_[j].pop_back();
  while (j > k) {  // split, keeping the lower half
    --j;
    free_[j].push_back(addr + (std::size_t{1} << j));
  }
  used_.emplace_back(addr, k);
  requested_.emplace_back(addr, size);
  return addr;
}

void BuddyAllocator::release(std::size_t address) {
  const auto it = std::find_if(used_.begin(), used_.end(), [&](const auto& u) { return u.first == address; });
  if (it == used_.end()) {
    throw std::runtime_error("buddy: release of an unallocated address");
  }
  std::size_t k = it->second;
  std::size_t addr = address;
  used_.erase(it);
  requested_.erase(std::find_if(requested_.begin(), requested_.end(), [&](const auto& r) { return r.first == address; }));
  while (k < order_) {  // coalesce with the buddy while it is free
    const std::size_t buddy = addr ^ (std::size_t{1} << k);
    auto& list = free_[k];
    const auto b = std::find(list.begin(), list.end(), buddy);
    if (b == list.end()) {
      break;
    }
    list.erase(b);
    addr = std::min(addr, buddy);
    ++k;
  }
  free_[k].push_back(addr);
}

std::size_t BuddyAllocator::free_units() const {
  std::size_t f = 0;
  for (std::size_t k = 0; k <= order_; ++k) {
    f += free_[k].size() << k;
  }
  return f;
}

bool BuddyAllocator::check_invariants() const {
  std::vector<std::pair<std::size_t, std::size_t>> all;  // (start, end)
  for (std::size_t k = 0; k <= order_; ++k) {
    for (const auto a : free_[k]) {
      if (a % (std::size_t{1} << k) != 0) {
        return false;  // misaligned
      }
      if (k < order_ && std::find(free_[k].begin(), free_[k].end(), a ^ (std::size_t{1} << k)) != free_[k].end()) {
        return false;  // two free buddies not coalesced
      }
      all.emplace_back(a, a + (std::size_t{1} << k));
    }
  }
  for (const auto& [a, k] : used_) {
    all.emplace_back(a, a + (std::size_t{1} << k));
  }
  std::sort(all.begin(), all.end());
  std::size_t covered = 0;
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (i > 0 && all[i].first < all[i - 1].second) {
      return false;  // overlap
    }
    covered += all[i].second - all[i].first;
  }
  return covered == (std::size_t{1} << order_);
}

std::size_t BuddyAllocator::internal_fragmentation() const {
  std::size_t waste = 0;
  for (std::size_t i = 0; i < used_.size(); ++i) {
    waste += (std::size_t{1} << used_[i].second) - requested_[i].second;
  }
  return waste;
}

// ---------------------------------------------------------------- paging

TwoLevelPaging::TwoLevelPaging(std::size_t tlb_entries) : tables_(1024), tlb_capacity_(tlb_entries) {}

void TwoLevelPaging::map(std::uint32_t vpage, std::uint32_t frame) {
  auto& table = tables_[(vpage >> 10U) & 0x3FFU];
  if (table.empty()) {
    table.resize(1024);
  }
  table[vpage & 0x3FFU] = PageTableEntry{true, frame};
}

std::optional<std::uint32_t> TwoLevelPaging::translate(std::uint32_t va) {
  const std::uint32_t vpage = va >> 12U;
  const std::uint32_t offset = va & 0xFFFU;
  const auto it = std::find_if(tlb_.begin(), tlb_.end(), [&](const auto& e) { return e.first == vpage; });
  if (it != tlb_.end()) {
    ++hits_;
    const auto entry = *it;
    tlb_.erase(it);
    tlb_.push_back(entry);  // most recently used
    return (entry.second << 12U) | offset;
  }
  ++misses_;
  const auto& table = tables_[(vpage >> 10U) & 0x3FFU];
  if (table.empty() || !table[vpage & 0x3FFU].present) {
    ++faults_;
    return std::nullopt;
  }
  const std::uint32_t frame = table[vpage & 0x3FFU].frame;
  if (tlb_capacity_ > 0) {
    if (tlb_.size() == tlb_capacity_) {
      tlb_.erase(tlb_.begin());  // evict the least recently used
    }
    tlb_.emplace_back(vpage, frame);
  }
  return (frame << 12U) | offset;
}

std::size_t TwoLevelPaging::page_table_bytes() const {
  std::size_t bytes = 1024 * 4;
  for (const auto& t : tables_) {
    bytes += t.empty() ? 0 : 1024 * 4;
  }
  return bytes;
}

double TwoLevelPaging::effective_access_time(double m, double t) const {
  const double total = static_cast<double>(hits_ + misses_);
  const double h = total > 0 ? static_cast<double>(hits_) / total : 0.0;
  return h * (t + m) + (1.0 - h) * (t + 3.0 * m);  // a miss walks two table levels, then the access
}

// ---------------------------------------------------------------- page replacement

const char* replacement_name(Replacement r) {
  switch (r) {
    case Replacement::FIFO:
      return "FIFO";
    case Replacement::LRU:
      return "LRU";
    case Replacement::OPT:
      return "OPT";
    case Replacement::Clock:
      return "Clock";
  }
  return "?";
}

ReplacementTrace simulate_replacement(const std::vector<int>& refs, std::size_t frames, Replacement r) {
  ReplacementTrace t;
  std::vector<int> frame(frames, -1);
  std::vector<std::size_t> loaded_at(frames, 0), last_use(frames, 0);
  std::vector<bool> use_bit(frames, false);
  std::size_t hand = 0;
  for (std::size_t i = 0; i < refs.size(); ++i) {
    const int page = refs[i];
    const auto it = std::find(frame.begin(), frame.end(), page);
    if (it != frame.end()) {
      const auto f = static_cast<std::size_t>(it - frame.begin());
      last_use[f] = i;
      use_bit[f] = true;
      t.fault.push_back(false);
      t.frames_after.push_back(frame);
      continue;
    }
    ++t.faults;
    t.fault.push_back(true);
    std::size_t victim = frames;
    const auto empty = std::find(frame.begin(), frame.end(), -1);
    if (empty != frame.end()) {
      victim = static_cast<std::size_t>(empty - frame.begin());
      if (r == Replacement::Clock) {
        hand = (victim + 1) % frames;
      }
    } else {
      switch (r) {
        case Replacement::FIFO:
          victim = static_cast<std::size_t>(std::min_element(loaded_at.begin(), loaded_at.end()) - loaded_at.begin());
          break;
        case Replacement::LRU:
          victim = static_cast<std::size_t>(std::min_element(last_use.begin(), last_use.end()) - last_use.begin());
          break;
        case Replacement::OPT: {
          // the page whose next use is farthest in the future (or never)
          std::size_t farthest = 0;
          for (std::size_t f = 0; f < frames; ++f) {
            std::size_t next = refs.size();
            for (std::size_t k = i + 1; k < refs.size(); ++k) {
              if (refs[k] == frame[f]) {
                next = k;
                break;
              }
            }
            if (victim == frames || next > farthest) {
              farthest = next;
              victim = f;
            }
          }
          break;
        }
        case Replacement::Clock:
          while (use_bit[hand]) {
            use_bit[hand] = false;  // second chance
            hand = (hand + 1) % frames;
          }
          victim = hand;
          hand = (hand + 1) % frames;
          break;
      }
    }
    frame[victim] = page;
    loaded_at[victim] = i;
    last_use[victim] = i;
    use_bit[victim] = true;
    t.frames_after.push_back(frame);
  }
  return t;
}

std::vector<std::size_t> working_set_sizes(const std::vector<int>& refs, std::size_t delta) {
  std::vector<std::size_t> out;
  for (std::size_t t = 0; t < refs.size(); ++t) {
    std::set<int> pages;
    for (std::size_t k = t + 1 > delta ? t + 1 - delta : 0; k <= t; ++k) {
      pages.insert(refs[k]);
    }
    out.push_back(pages.size());
  }
  return out;
}

}  // namespace nexus::os
