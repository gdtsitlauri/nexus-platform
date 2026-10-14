#include "nexus/os/storage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace nexus::os {

// ---------------------------------------------------------------- disk scheduling

const char* disk_policy_name(DiskPolicy p) {
  switch (p) {
    case DiskPolicy::FIFO:
      return "FIFO";
    case DiskPolicy::SSTF:
      return "SSTF";
    case DiskPolicy::SCAN:
      return "SCAN";
    case DiskPolicy::CSCAN:
      return "C-SCAN";
    case DiskPolicy::LOOK:
      return "LOOK";
    case DiskPolicy::CLOOK:
      return "C-LOOK";
  }
  return "?";
}

DiskSchedule schedule_disk(const std::vector<int>& requests, int start, DiskPolicy p, bool increasing, int max_track) {
  DiskSchedule s;
  int head = start;
  auto move_to = [&](int track) {
    s.total_movement += std::abs(track - head);
    head = track;
  };
  std::vector<int> pending = requests;
  if (p == DiskPolicy::FIFO) {
    for (const int r : requests) {
      s.order.push_back(r);
      move_to(r);
    }
  } else if (p == DiskPolicy::SSTF) {
    while (!pending.empty()) {
      auto best = pending.begin();
      for (auto it = pending.begin(); it != pending.end(); ++it) {
        if (std::abs(*it - head) < std::abs(*best - head)) {
          best = it;
        }
      }
      s.order.push_back(*best);
      move_to(*best);
      pending.erase(best);
    }
  } else {
    std::vector<int> up, down;
    for (const int r : pending) {
      ((r >= head) == increasing ? up : down).push_back(r);  // `up` = in the initial direction
    }
    if (increasing) {
      std::sort(up.begin(), up.end());
      std::sort(down.begin(), down.end(), std::greater<>());
    } else {
      std::sort(up.begin(), up.end(), std::greater<>());
      std::sort(down.begin(), down.end());
    }
    for (const int r : up) {
      s.order.push_back(r);
      move_to(r);
    }
    const int edge = increasing ? max_track : 0;
    const int far_edge = increasing ? 0 : max_track;
    if (!down.empty()) {
      if (p == DiskPolicy::SCAN) {
        move_to(edge);  // the arm runs to the end of the disk before reversing
      }
      if (p == DiskPolicy::CSCAN || p == DiskPolicy::CLOOK) {
        if (p == DiskPolicy::CSCAN) {
          move_to(edge);
          move_to(far_edge);  // return sweep (counted as movement)
        }
        // continue in the same direction from the other end
        std::sort(down.begin(), down.end());
        if (!increasing) {
          std::reverse(down.begin(), down.end());
        }
      }
      for (const int r : down) {
        s.order.push_back(r);
        move_to(r);
      }
    }
  }
  s.average_seek = requests.empty() ? 0.0 : static_cast<double>(s.total_movement) / static_cast<double>(requests.size());
  return s;
}

int optimal_disk_movement(const std::vector<int>& requests, int start) {
  if (requests.size() > 9) {
    throw std::runtime_error("optimal_disk_movement: at most 9 requests");
  }
  std::vector<int> r = requests;
  std::sort(r.begin(), r.end());
  int best = std::numeric_limits<int>::max();
  do {
    int head = start, total = 0;
    for (const int x : r) {
      total += std::abs(x - head);
      head = x;
    }
    best = std::min(best, total);
  } while (std::next_permutation(r.begin(), r.end()));
  return best;
}

// ---------------------------------------------------------------- RAID 5

Raid5::Raid5(std::size_t disks) : disks_(disks), store_(disks) {
  if (disks < 3) {
    throw std::runtime_error("raid5: at least three disks");
  }
}

void Raid5::write_stripe(std::size_t stripe, const std::vector<std::uint8_t>& data) {
  if (data.size() != disks_ - 1) {
    throw std::runtime_error("raid5: a stripe holds disks - 1 data blocks");
  }
  const std::size_t pd = parity_disk(stripe);
  std::uint8_t parity = 0;
  std::size_t k = 0;
  for (std::size_t d = 0; d < disks_; ++d) {
    if (d == pd) {
      continue;
    }
    store_[d][stripe] = data[k];
    parity ^= data[k++];
  }
  store_[pd][stripe] = parity;
}

std::vector<std::uint8_t> Raid5::read_stripe(std::size_t stripe) const {
  std::vector<std::uint8_t> out;
  const std::size_t pd = parity_disk(stripe);
  for (std::size_t d = 0; d < disks_; ++d) {
    if (d != pd) {
      out.push_back(store_[d].at(stripe));
    }
  }
  return out;
}

void Raid5::fail_disk(std::size_t d) { failed_ = d; }

std::vector<std::uint8_t> Raid5::read_stripe_degraded(std::size_t stripe) const {
  const std::size_t pd = parity_disk(stripe);
  std::uint8_t missing = 0;
  for (std::size_t d = 0; d < disks_; ++d) {
    if (!failed_ || d != *failed_) {
      missing ^= store_[d].at(stripe);  // XOR of all surviving blocks (data and parity) = the lost block
    }
  }
  std::vector<std::uint8_t> out;
  for (std::size_t d = 0; d < disks_; ++d) {
    if (d == pd) {
      continue;
    }
    out.push_back(failed_ && d == *failed_ ? missing : store_[d].at(stripe));
  }
  return out;
}

// ---------------------------------------------------------------- file system

FileSystem::FileSystem(std::size_t blocks, std::size_t block_size, std::size_t inodes)
    : block_size_(block_size),
      pointers_per_block_(block_size / 8),
      disk_(blocks, std::string(block_size, '\0')),
      bitmap_(blocks, false),
      inodes_(inodes) {
  if (block_size < 16) {
    throw std::runtime_error("filesystem: block size too small");
  }
  inodes_[0].used = true;  // root directory
  inodes_[0].dir = true;
  inodes_[0].links = 1;
}

std::size_t FileSystem::max_file_size() const {
  return (10 + pointers_per_block_ + pointers_per_block_ * pointers_per_block_) * block_size_;
}

std::int64_t FileSystem::alloc_block() {
  for (std::size_t b = 0; b < bitmap_.size(); ++b) {
    if (!bitmap_[b]) {
      bitmap_[b] = true;
      disk_[b].assign(block_size_, '\0');
      return static_cast<std::int64_t>(b);
    }
  }
  return -1;
}

void FileSystem::free_block(std::int64_t b) {
  if (b >= 0) {
    bitmap_[static_cast<std::size_t>(b)] = false;
  }
}

namespace {

std::int64_t get_ptr(const std::string& block, std::size_t i) {
  std::int64_t v = 0;
  for (std::size_t k = 0; k < 8; ++k) {
    v |= static_cast<std::int64_t>(static_cast<unsigned char>(block[i * 8 + k])) << (8 * k);
  }
  return v - 1;  // stored as value + 1 so that zero means "no block"
}

void set_ptr(std::string& block, std::size_t i, std::int64_t b) {
  const std::int64_t v = b + 1;
  for (std::size_t k = 0; k < 8; ++k) {
    block[i * 8 + k] = static_cast<char>((v >> (8 * k)) & 0xFF);
  }
}

}  // namespace

std::int64_t FileSystem::block_for(Inode& node, std::size_t index, bool allocate) {
  auto ensure = [&](std::int64_t& slot) {
    if (slot < 0 && allocate) {
      slot = alloc_block();
    }
    return slot;
  };
  if (index < 10) {
    return ensure(node.direct[index]);
  }
  index -= 10;
  const std::size_t ppb = pointers_per_block_;
  auto through = [&](std::int64_t table, std::size_t i) -> std::int64_t {
    if (table < 0) {
      return -1;
    }
    std::string& blk = disk_[static_cast<std::size_t>(table)];
    std::int64_t b = get_ptr(blk, i);
    if (b < 0 && allocate) {
      b = alloc_block();
      if (b >= 0) {
        set_ptr(disk_[static_cast<std::size_t>(table)], i, b);
      }
    }
    return b;
  };
  if (index < ppb) {
    return through(ensure(node.indirect), index);
  }
  index -= ppb;
  if (index < ppb * ppb) {
    return through(through(ensure(node.double_indirect), index / ppb), index % ppb);
  }
  return -1;  // beyond the maximum file size
}

std::int64_t FileSystem::block_for_read(const Inode& node, std::size_t index) const {
  if (index < 10) {
    return node.direct[index];
  }
  index -= 10;
  const std::size_t ppb = pointers_per_block_;
  auto through = [&](std::int64_t table, std::size_t i) -> std::int64_t {
    return table < 0 ? -1 : get_ptr(disk_[static_cast<std::size_t>(table)], i);
  };
  if (index < ppb) {
    return through(node.indirect, index);
  }
  index -= ppb;
  if (index < ppb * ppb) {
    return through(through(node.double_indirect, index / ppb), index % ppb);
  }
  return -1;
}

std::string FileSystem::read_inode(std::size_t ino) const {
  const Inode& n = inodes_[ino];
  std::string out;
  out.reserve(n.size);
  for (std::size_t pos = 0; pos < n.size; pos += block_size_) {
    const std::int64_t b = block_for_read(n, pos / block_size_);
    const std::size_t len = std::min(block_size_, n.size - pos);
    out += b < 0 ? std::string(len, '\0') : disk_[static_cast<std::size_t>(b)].substr(0, len);
  }
  return out;
}

void FileSystem::write_inode(std::size_t ino, std::size_t offset, const std::string& data) {
  Inode& n = inodes_[ino];
  for (std::size_t i = 0; i < data.size();) {
    const std::size_t pos = offset + i;
    const std::int64_t b = block_for(n, pos / block_size_, true);
    if (b < 0) {
      throw std::runtime_error("filesystem: out of space or file too large");
    }
    const std::size_t within = pos % block_size_;
    const std::size_t len = std::min(block_size_ - within, data.size() - i);
    disk_[static_cast<std::size_t>(b)].replace(within, len, data, i, len);
    i += len;
  }
  n.size = std::max(n.size, offset + data.size());
}

void FileSystem::truncate_free(std::size_t ino) {
  Inode& n = inodes_[ino];
  for (auto& d : n.direct) {
    free_block(d);
    d = -1;
  }
  auto free_table = [&](std::int64_t table, int depth, auto&& self) -> void {
    if (table < 0) {
      return;
    }
    for (std::size_t i = 0; i < pointers_per_block_; ++i) {
      const std::int64_t b = get_ptr(disk_[static_cast<std::size_t>(table)], i);
      if (b >= 0) {
        if (depth > 1) {
          self(b, depth - 1, self);
        } else {
          free_block(b);
        }
      }
    }
    free_block(table);
  };
  free_table(n.indirect, 1, free_table);
  free_table(n.double_indirect, 2, free_table);
  n.indirect = n.double_indirect = -1;
  n.size = 0;
}

std::vector<std::pair<std::string, std::size_t>> FileSystem::entries(std::size_t dir_inode) const {
  std::vector<std::pair<std::string, std::size_t>> out;
  std::istringstream in(read_inode(dir_inode));
  std::string name;
  std::size_t ino = 0;
  while (in >> name >> ino) {
    out.emplace_back(name, ino);
  }
  return out;
}

void FileSystem::set_entries(std::size_t dir_inode, const std::vector<std::pair<std::string, std::size_t>>& e) {
  std::ostringstream out;
  for (const auto& [name, ino] : e) {
    out << name << ' ' << ino << '\n';
  }
  truncate_free(dir_inode);
  write_inode(dir_inode, 0, out.str());
}

std::optional<std::size_t> FileSystem::lookup(const std::string& path) const {
  if (path.empty() || path[0] != '/') {
    return std::nullopt;
  }
  std::size_t cur = 0;
  std::istringstream in(path.substr(1));
  std::string part;
  while (std::getline(in, part, '/')) {
    if (part.empty()) {
      continue;
    }
    if (!inodes_[cur].dir) {
      return std::nullopt;
    }
    bool found = false;
    for (const auto& [name, ino] : entries(cur)) {
      if (name == part) {
        cur = ino;
        found = true;
        break;
      }
    }
    if (!found) {
      return std::nullopt;
    }
  }
  return cur;
}

std::optional<std::pair<std::size_t, std::string>> FileSystem::parent_of(const std::string& path) const {
  const auto slash = path.find_last_of('/');
  if (slash == std::string::npos || slash + 1 >= path.size()) {
    return std::nullopt;
  }
  const auto parent = lookup(slash == 0 ? "/" : path.substr(0, slash));
  if (!parent || !inodes_[*parent].dir) {
    return std::nullopt;
  }
  return std::make_pair(*parent, path.substr(slash + 1));
}

std::optional<std::size_t> FileSystem::new_inode(bool dir) {
  for (std::size_t i = 1; i < inodes_.size(); ++i) {
    if (!inodes_[i].used) {
      inodes_[i] = Inode{};
      inodes_[i].used = true;
      inodes_[i].dir = dir;
      return i;
    }
  }
  return std::nullopt;
}

bool FileSystem::create(const std::string& path) {
  const auto p = parent_of(path);
  if (!p || lookup(path)) {
    return false;
  }
  const auto ino = new_inode(false);
  if (!ino) {
    return false;
  }
  auto e = entries(p->first);
  e.emplace_back(p->second, *ino);
  inodes_[*ino].links = 1;
  set_entries(p->first, e);
  return true;
}

bool FileSystem::mkdir(const std::string& path) {
  const auto p = parent_of(path);
  if (!p || lookup(path)) {
    return false;
  }
  const auto ino = new_inode(true);
  if (!ino) {
    return false;
  }
  auto e = entries(p->first);
  e.emplace_back(p->second, *ino);
  inodes_[*ino].links = 1;
  set_entries(p->first, e);
  return true;
}

bool FileSystem::write(const std::string& path, std::size_t offset, const std::string& data) {
  const auto ino = lookup(path);
  if (!ino || inodes_[*ino].dir) {
    return false;
  }
  write_inode(*ino, offset, data);
  return true;
}

std::optional<std::string> FileSystem::read(const std::string& path) const {
  const auto ino = lookup(path);
  if (!ino || inodes_[*ino].dir) {
    return std::nullopt;
  }
  return read_inode(*ino);
}

bool FileSystem::link(const std::string& existing, const std::string& new_path) {
  const auto ino = lookup(existing);
  const auto p = parent_of(new_path);
  if (!ino || inodes_[*ino].dir || !p || lookup(new_path)) {
    return false;
  }
  auto e = entries(p->first);
  e.emplace_back(p->second, *ino);
  set_entries(p->first, e);
  ++inodes_[*ino].links;
  return true;
}

bool FileSystem::unlink(const std::string& path) {
  const auto p = parent_of(path);
  const auto ino = lookup(path);
  if (!p || !ino || *ino == 0) {
    return false;
  }
  if (inodes_[*ino].dir && !entries(*ino).empty()) {
    return false;  // directory not empty
  }
  auto e = entries(p->first);
  e.erase(std::find_if(e.begin(), e.end(), [&](const auto& x) { return x.first == p->second; }));
  set_entries(p->first, e);
  if (--inodes_[*ino].links == 0) {
    truncate_free(*ino);
    inodes_[*ino] = Inode{};
  }
  return true;
}

std::vector<std::string> FileSystem::list(const std::string& dir) const {
  std::vector<std::string> out;
  const auto ino = lookup(dir);
  if (ino && inodes_[*ino].dir) {
    for (const auto& [name, i] : entries(*ino)) {
      out.push_back(name);
    }
  }
  return out;
}

std::size_t FileSystem::free_blocks() const {
  return static_cast<std::size_t>(std::count(bitmap_.begin(), bitmap_.end(), false));
}

std::size_t FileSystem::blocks_of(const std::string& path) const {
  const auto ino = lookup(path);
  if (!ino) {
    return 0;
  }
  const Inode& n = inodes_[*ino];
  std::size_t count = 0;
  for (const auto d : n.direct) {
    count += d >= 0 ? 1 : 0;
  }
  auto table = [&](std::int64_t t, int depth, auto&& self) -> void {
    if (t < 0) {
      return;
    }
    ++count;
    for (std::size_t i = 0; i < pointers_per_block_; ++i) {
      const std::int64_t b = get_ptr(disk_[static_cast<std::size_t>(t)], i);
      if (b >= 0) {
        if (depth > 1) {
          self(b, depth - 1, self);
        } else {
          ++count;
        }
      }
    }
  };
  table(n.indirect, 1, table);
  table(n.double_indirect, 2, table);
  return count;
}

bool FileSystem::fsck(std::string* report) const {
  std::vector<int> refs(bitmap_.size(), 0);
  std::vector<int> link_count(inodes_.size(), 0);
  link_count[0] = 1;
  std::ostringstream out;
  bool ok = true;
  auto mark = [&](std::int64_t b) {
    if (b >= 0) {
      ++refs[static_cast<std::size_t>(b)];
    }
  };
  for (std::size_t i = 0; i < inodes_.size(); ++i) {
    const Inode& n = inodes_[i];
    if (!n.used) {
      continue;
    }
    for (const auto d : n.direct) mark(d);
    auto table = [&](std::int64_t t, int depth, auto&& self) -> void {
      if (t < 0) return;
      mark(t);
      for (std::size_t k = 0; k < pointers_per_block_; ++k) {
        const std::int64_t b = get_ptr(disk_[static_cast<std::size_t>(t)], k);
        if (b >= 0) {
          if (depth > 1) self(b, depth - 1, self);
          else mark(b);
        }
      }
    };
    table(n.indirect, 1, table);
    table(n.double_indirect, 2, table);
    if (n.dir) {
      for (const auto& [name, ino] : entries(i)) {
        ++link_count[ino];
      }
    }
  }
  for (std::size_t b = 0; b < bitmap_.size(); ++b) {
    if (refs[b] > 1) {
      ok = false;
      out << "block " << b << " referenced " << refs[b] << " times\n";
    }
    if ((refs[b] > 0) != bitmap_[b]) {
      ok = false;
      out << "block " << b << (bitmap_[b] ? " marked used but unreferenced\n" : " referenced but marked free\n");
    }
  }
  for (std::size_t i = 0; i < inodes_.size(); ++i) {
    if (inodes_[i].used && inodes_[i].links != link_count[i]) {
      ok = false;
      out << "inode " << i << " link count " << inodes_[i].links << " but " << link_count[i] << " entries\n";
    }
  }
  if (report != nullptr) {
    *report = out.str();
  }
  return ok;
}

}  // namespace nexus::os
