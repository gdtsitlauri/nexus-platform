#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// I/O management, disk scheduling and file management (EY517).
namespace nexus::os {

// ---------------------------------------------------------------- disk scheduling

enum class DiskPolicy { FIFO, SSTF, SCAN, CSCAN, LOOK, CLOOK };
const char* disk_policy_name(DiskPolicy p);

struct DiskSchedule {
  std::vector<int> order;       // tracks in service order
  int total_movement = 0;       // tracks crossed (SCAN/C-SCAN include the sweep to the end, C-SCAN the return)
  double average_seek = 0.0;    // total / number of requests
};

// `start` head position; `increasing` initial direction for the elevator policies; tracks 0..max_track.
DiskSchedule schedule_disk(const std::vector<int>& requests, int start, DiskPolicy p, bool increasing = true,
                           int max_track = 199);
// Optimal order by exhaustive search (shortest total movement; requests <= 9), for comparison.
int optimal_disk_movement(const std::vector<int>& requests, int start);

// ---------------------------------------------------------------- RAID 5

// Block-interleaved distributed parity over `disks` disks (rotating parity position). Data blocks are bytes.
class Raid5 {
 public:
  explicit Raid5(std::size_t disks);
  void write_stripe(std::size_t stripe, const std::vector<std::uint8_t>& data);  // disks - 1 data blocks
  std::vector<std::uint8_t> read_stripe(std::size_t stripe) const;
  void fail_disk(std::size_t d);
  std::vector<std::uint8_t> read_stripe_degraded(std::size_t stripe) const;  // reconstructs from parity
  std::size_t parity_disk(std::size_t stripe) const { return (disks_ - 1) - stripe % disks_; }

 private:
  std::size_t disks_;
  std::vector<std::map<std::size_t, std::uint8_t>> store_;  // per disk: stripe -> block
  std::optional<std::size_t> failed_;
};

// ---------------------------------------------------------------- a Unix-like file system

// Blocks of `block_size` bytes; inodes with 10 direct pointers, one single-indirect and one double-indirect
// pointer; directories are files of (name, inode) entries; a free-block bitmap; hard links with link counts.
class FileSystem {
 public:
  FileSystem(std::size_t blocks, std::size_t block_size, std::size_t inodes);

  bool mkdir(const std::string& path);
  bool create(const std::string& path);
  bool write(const std::string& path, std::size_t offset, const std::string& data);
  std::optional<std::string> read(const std::string& path) const;
  bool link(const std::string& existing, const std::string& new_path);  // hard link
  bool unlink(const std::string& path);
  std::vector<std::string> list(const std::string& dir) const;
  std::size_t free_blocks() const;
  std::size_t max_file_size() const;  // bytes addressable by one inode
  std::size_t blocks_of(const std::string& path) const;  // data plus indirect blocks

  // Consistency check: every block marked used is referenced exactly once, link counts match directory entries.
  bool fsck(std::string* report = nullptr) const;

 private:
  struct Inode {
    bool used = false;
    bool dir = false;
    std::size_t size = 0;
    int links = 0;
    std::vector<std::int64_t> direct = std::vector<std::int64_t>(10, -1);
    std::int64_t indirect = -1;
    std::int64_t double_indirect = -1;
  };
  std::optional<std::size_t> lookup(const std::string& path) const;
  std::optional<std::pair<std::size_t, std::string>> parent_of(const std::string& path) const;
  std::int64_t alloc_block();
  void free_block(std::int64_t b);
  std::int64_t block_for(Inode& node, std::size_t index, bool allocate);
  std::int64_t block_for_read(const Inode& node, std::size_t index) const;
  std::vector<std::pair<std::string, std::size_t>> entries(std::size_t dir_inode) const;
  void set_entries(std::size_t dir_inode, const std::vector<std::pair<std::string, std::size_t>>& e);
  std::string read_inode(std::size_t ino) const;
  void write_inode(std::size_t ino, std::size_t offset, const std::string& data);
  void truncate_free(std::size_t ino);
  std::optional<std::size_t> new_inode(bool dir);

  std::size_t block_size_;
  std::size_t pointers_per_block_;
  std::vector<std::string> disk_;
  std::vector<bool> bitmap_;
  std::vector<Inode> inodes_;
};

}  // namespace nexus::os
