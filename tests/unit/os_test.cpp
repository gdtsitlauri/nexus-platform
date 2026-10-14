// EY517 Operating Systems: textbook examples (Stallings, Silberschatz) plus brute-force and property checks.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <thread>

#include "nexus/os/concurrency.hpp"
#include "nexus/os/resources.hpp"
#include "nexus/os/scheduling.hpp"
#include "nexus/os/storage.hpp"

namespace os = nexus::os;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::mt19937 rng(517);

std::vector<int> finishes(const os::Schedule& s) {
  std::vector<int> f;
  for (const auto& j : s.jobs) f.push_back(j.finish);
  return f;
}

void processes_and_scheduling() {
  os::ProcessControlBlock p;
  expect(p.transition(os::ProcState::Ready) && p.transition(os::ProcState::Running) &&
             p.transition(os::ProcState::Blocked) && p.transition(os::ProcState::BlockedSuspend) &&
             p.transition(os::ProcState::ReadySuspend) && p.transition(os::ProcState::Ready),
         "seven-state model: admit, dispatch, wait, suspend, event, activate");
  expect(!p.transition(os::ProcState::Blocked), "ready -> blocked is illegal");
  // Stallings' five-process example
  const std::vector<os::Job> jobs = {{"A", 0, 3}, {"B", 2, 6}, {"C", 4, 4}, {"D", 6, 5}, {"E", 8, 2}};
  expect(finishes(os::schedule(jobs, os::Policy::FCFS)) == std::vector<int>({3, 9, 13, 18, 20}), "FCFS finish times");
  expect(finishes(os::schedule(jobs, os::Policy::RR, 1)) == std::vector<int>({4, 18, 17, 20, 15}), "RR q=1 finish times");
  expect(finishes(os::schedule(jobs, os::Policy::RR, 4)) == std::vector<int>({3, 17, 11, 20, 19}), "RR q=4 finish times");
  expect(finishes(os::schedule(jobs, os::Policy::SPN)) == std::vector<int>({3, 9, 15, 20, 11}), "SPN finish times");
  expect(finishes(os::schedule(jobs, os::Policy::SRT)) == std::vector<int>({3, 15, 8, 20, 10}), "SRT finish times");
  expect(finishes(os::schedule(jobs, os::Policy::HRRN)) == std::vector<int>({3, 9, 13, 20, 15}), "HRRN finish times");
  for (const bool expo : {false, true}) {
    const auto fb = os::schedule(jobs, os::Policy::Feedback, 1, expo);
    int busy = 0;
    for (const auto& g : fb.gantt) busy += g.name.empty() ? 0 : g.end - g.start;
    const auto fin = finishes(fb);
    expect(busy == 20 && *std::max_element(fin.begin(), fin.end()) == 20,
           "feedback is work-conserving (no idle time while jobs wait)");
  }
  // properties on random workloads
  std::uniform_int_distribution<int> arr(0, 10), svc(1, 8);
  for (int trial = 0; trial < 200; ++trial) {
    std::vector<os::Job> w;
    for (int i = 0; i < 6; ++i) w.push_back({std::string(1, static_cast<char>('A' + i)), arr(rng), svc(rng), i % 3});
    const double srt = os::schedule(w, os::Policy::SRT).mean_turnaround;
    for (const auto pol : {os::Policy::FCFS, os::Policy::RR, os::Policy::SPN, os::Policy::HRRN, os::Policy::Priority,
                           os::Policy::Feedback}) {
      expect(srt <= os::schedule(w, pol, 2).mean_turnaround + 1e-9, "SRT minimises mean turnaround");
    }
    for (auto& j : w) j.arrival = 0;
    expect(std::abs(os::schedule(w, os::Policy::SPN).mean_turnaround - os::best_nonpreemptive_mean_turnaround(w)) < 1e-9,
           "SPN is optimal when all jobs arrive together (exhaustive check)");
  }
}

void concurrency() {
  auto check = [](const std::string& text) { return os::model_check(os::parse_concurrent(text)); };
  // lost update: two non-atomic increments
  const auto race = check(R"(
shared count=0
thread T0
  load %a count
  add %a 1
  store count %a
thread T1
  load %a count
  add %a 1
  store count %a
assert count 2
)");
  expect(!race.assertions_hold && race.final_values_seen == std::vector<std::int64_t>({1, 2}),
         "race condition: the final count can be 1 or 2");
  expect(!race.counterexample.empty(), "race: counterexample schedule given");
  // Peterson's algorithm (two loop iterations each)
  const auto peterson = check(R"(
shared flag0=0 flag1=0 turn=0 n=0
thread P0
  set %i 0
L: set flag0 1
  set turn 1
W: jeq flag1 0 C
  jeq turn 0 C
  jmp W
C: cs
  endcs
  set flag0 0
  add %i 1
  jne %i 2 L
thread P1
  set %i 0
L: set flag1 1
  set turn 0
W: jeq flag0 0 C
  jeq turn 1 C
  jmp W
C: cs
  endcs
  set flag1 0
  add %i 1
  jne %i 2 L
)");
  expect(peterson.mutual_exclusion && peterson.deadlock_free, "Peterson: mutual exclusion and no deadlock");
  // setting the flag after testing the other's flag breaks mutual exclusion
  const auto test_then_set = check(R"(
shared flag0=0 flag1=0
thread P0
  await flag1 0
  set flag0 1
  cs
  endcs
  set flag0 0
thread P1
  await flag0 0
  set flag1 1
  cs
  endcs
  set flag1 0
)");
  expect(!test_then_set.mutual_exclusion, "test-then-set flags violate mutual exclusion");
  // setting the flag first, then waiting, can deadlock
  const auto set_then_test = check(R"(
shared flag0=0 flag1=0
thread P0
  set flag0 1
  await flag1 0
  cs
  endcs
  set flag0 0
thread P1
  set flag1 1
  await flag0 0
  cs
  endcs
  set flag1 0
)");
  expect(set_then_test.mutual_exclusion && !set_then_test.deadlock_free, "set-then-test flags deadlock");
  // atomic test-and-set lock
  const auto tas = check(R"(
shared lock=0
thread T0
L: tas %r lock
  jne %r 0 L
  cs
  endcs
  set lock 0
thread T1
L: tas %r lock
  jne %r 0 L
  cs
  endcs
  set lock 0
thread T2
L: tas %r lock
  jne %r 0 L
  cs
  endcs
  set lock 0
)");
  expect(tas.mutual_exclusion && tas.deadlock_free, "test-and-set lock with three threads");
  // dining philosophers with semaphores: deadlock unless one philosopher reverses the order
  const std::string philosophers = R"(
shared f0=1 f1=1 f2=1
thread Ph0
  P f0
  P f1
  V f1
  V f0
thread Ph1
  P f1
  P f2
  V f2
  V f1
thread Ph2
  P SECOND
  P FIRST
  V FIRST
  V SECOND
)";
  auto with = [&](const std::string& a, const std::string& b) {
    std::string s = philosophers;
    s.replace(s.find("SECOND"), 6, a);
    s.replace(s.find("FIRST"), 5, b);
    s.replace(s.find("FIRST"), 5, b);
    s.replace(s.find("SECOND"), 6, a);
    return s;
  };
  expect(!check(with("f2", "f0")).deadlock_free, "dining philosophers (all left first) can deadlock");
  expect(check(with("f0", "f2")).deadlock_free, "resource ordering prevents the deadlock");
  // bounded buffer with semaphores: two producers, one consumer, every item consumed
  const auto pc = check(R"(
shared empty=1 full=0 buf=0 consumed=0
thread Prod0
  P empty
  set buf 1
  V full
thread Prod1
  P empty
  set buf 1
  V full
thread Cons
  P full
  add consumed 1
  V empty
  P full
  add consumed 1
  V empty
assert consumed 2
)");
  expect(pc.assertions_hold && pc.deadlock_free, "semaphore bounded buffer delivers every item");

  // real threads
  os::PetersonLock lock;
  long counter = 0;
  auto worker = [&](int self) {
    for (int i = 0; i < 100000; ++i) {
      lock.lock(self);
      ++counter;
      lock.unlock(self);
    }
  };
  std::thread a(worker, 0), b(worker, 1);
  a.join();
  b.join();
  expect(counter == 200000, "Peterson lock on real threads: no lost update");
  os::BoundedBuffer buf(8);
  std::atomic<long> sum{0};
  std::vector<std::thread> ts;
  for (int p = 0; p < 3; ++p) ts.emplace_back([&, p] { for (int i = 1; i <= 1000; ++i) buf.put(i + p * 1000); });
  for (int c = 0; c < 3; ++c) ts.emplace_back([&] { for (int i = 0; i < 1000; ++i) sum += buf.take(); });
  for (auto& t : ts) t.join();
  expect(sum == 3 * 500500 + 1000 * 1000 + 2000 * 1000, "bounded buffer: every produced item consumed once");
  os::ReadersWriters rw;
  std::atomic<bool> violation{false};
  std::vector<std::thread> rws;
  for (int r = 0; r < 4; ++r)
    rws.emplace_back([&] {
      for (int i = 0; i < 2000; ++i) {
        rw.start_read();
        if (rw.writer_active()) violation = true;
        rw.end_read();
      }
    });
  for (int w = 0; w < 2; ++w)
    rws.emplace_back([&] {
      for (int i = 0; i < 2000; ++i) {
        rw.start_write();
        if (rw.active_readers() != 0) violation = true;
        rw.end_write();
      }
    });
  for (auto& t : rws) t.join();
  expect(!violation, "readers-writers: no reader during a write");
}

void deadlock() {
  os::BankerState s;
  s.available = {3, 3, 2};
  s.max = {{7, 5, 3}, {3, 2, 2}, {9, 0, 2}, {2, 2, 2}, {4, 3, 3}};
  s.allocation = {{0, 1, 0}, {2, 0, 0}, {3, 0, 2}, {2, 1, 1}, {0, 0, 2}};
  const auto seq = os::safe_sequence(s);
  expect(seq.has_value() && seq->size() == 5, "Silberschatz example is safe");
  std::string why;
  expect(os::request_resources(s, 1, {1, 0, 2}, &why), "P1 requests (1,0,2): granted");
  expect(!os::request_resources(s, 4, {3, 3, 0}, &why) && why.find("not enough") != std::string::npos,
         "P4 requests (3,3,0): must wait");
  expect(!os::request_resources(s, 0, {0, 2, 0}, &why) && why.find("unsafe") != std::string::npos,
         "P0 requests (0,2,0): denied, unsafe");
  std::uniform_int_distribution<int> r(0, 4);
  for (int trial = 0; trial < 300; ++trial) {
    os::BankerState t;
    t.available = {r(rng), r(rng)};
    for (int p = 0; p < 5; ++p) {
      const std::vector<int> alloc{r(rng), r(rng)};
      t.allocation.push_back(alloc);
      t.max.push_back({alloc[0] + r(rng), alloc[1] + r(rng)});
    }
    expect(os::safe_sequence(t).has_value() == os::safe_by_enumeration(t), "banker safety equals exhaustive search");
  }
  const std::vector<int> avail{0, 0, 0};
  const os::Matrix alloc{{0, 1, 0}, {2, 0, 0}, {3, 0, 3}, {2, 1, 1}, {0, 0, 2}};
  os::Matrix req{{0, 0, 0}, {2, 0, 2}, {0, 0, 0}, {1, 0, 0}, {0, 0, 2}};
  expect(os::detect_deadlock(avail, alloc, req).empty(), "detection example: no deadlock");
  req[2] = {0, 0, 1};
  expect(os::detect_deadlock(avail, alloc, req) == std::vector<std::size_t>({1, 2, 3, 4}),
         "after P2 requests one more C: P1-P4 deadlocked");
  expect(os::wait_for_cycle({{1}, {2}, {0}, {}}).has_value() && !os::wait_for_cycle({{1}, {2}, {}, {0}}).has_value(),
         "wait-for graph cycle detection");
}

void memory() {
  std::uniform_int_distribution<std::size_t> sz(1, 60);
  for (const auto fit : {os::Fit::First, os::Fit::Next, os::Fit::Best, os::Fit::Worst}) {
    os::PartitionAllocator a(1024, fit);
    std::vector<int> live;
    for (int step = 0; step < 400; ++step) {
      if (!live.empty() && rng() % 3 == 0) {
        const auto k = rng() % live.size();
        a.release(live[k]);
        live.erase(live.begin() + static_cast<long>(k));
      } else if (a.allocate(sz(rng), step)) {
        live.push_back(step);
      }
      std::size_t covered = 0;
      bool ordered = true;
      for (std::size_t i = 0; i < a.blocks().size(); ++i) {
        covered += a.blocks()[i].size;
        if (i > 0) ordered &= a.blocks()[i].start == a.blocks()[i - 1].start + a.blocks()[i - 1].size;
      }
      expect(covered == 1024 && ordered, "partitions tile memory exactly");
    }
    a.compact();
    expect(a.holes() <= 1 && a.external_fragmentation() == 0.0, "compaction leaves one hole");
  }
  os::BuddyAllocator buddy(10);
  std::vector<std::size_t> addrs;
  for (int step = 0; step < 500; ++step) {
    if (!addrs.empty() && rng() % 2 == 0) {
      const auto k = rng() % addrs.size();
      buddy.release(addrs[k]);
      addrs.erase(addrs.begin() + static_cast<long>(k));
    } else if (const auto a = buddy.allocate(sz(rng))) {
      addrs.push_back(*a);
    }
    expect(buddy.check_invariants(), "buddy blocks aligned, disjoint and coalesced");
  }
  for (const auto a : addrs) buddy.release(a);
  expect(buddy.free_units() == 1024 && buddy.check_invariants(), "buddy coalesces back to one block");
  os::TwoLevelPaging pg(4);
  for (std::uint32_t p = 0; p < 64; ++p) pg.map(p, 100 + p);
  pg.map(0xFFFFF, 7);
  expect(pg.translate(0x00003123) == std::optional<std::uint32_t>((103U << 12) | 0x123), "two-level translation");
  expect(pg.translate(0xFFFFF004) == std::optional<std::uint32_t>((7U << 12) | 4), "high page in another table");
  expect(!pg.translate(0x00100000).has_value() && pg.page_faults() == 1, "unmapped page faults");
  for (int rep = 0; rep < 10; ++rep)
    for (std::uint32_t p = 0; p < 3; ++p) pg.translate(p << 12);
  expect(pg.tlb_hits() >= 27 && pg.page_table_bytes() == 3 * 4096, "TLB locality and on-demand page tables");
  expect(pg.effective_access_time(100, 10) < 3 * 100 + 10, "effective access time below the uncached walk");
}

void virtual_memory() {
  const std::vector<int> belady{1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
  expect(os::simulate_replacement(belady, 3, os::Replacement::FIFO).faults == 9 &&
             os::simulate_replacement(belady, 4, os::Replacement::FIFO).faults == 10,
         "Belady's anomaly: FIFO with 4 frames faults more than with 3");
  const std::vector<int> stallings{2, 3, 2, 1, 5, 2, 4, 5, 3, 2, 5, 2};
  expect(os::simulate_replacement(stallings, 3, os::Replacement::OPT).faults == 6 &&
             os::simulate_replacement(stallings, 3, os::Replacement::LRU).faults == 7 &&
             os::simulate_replacement(stallings, 3, os::Replacement::FIFO).faults == 9 &&
             os::simulate_replacement(stallings, 3, os::Replacement::Clock).faults == 8,
         "Stallings example: OPT 6, LRU 7, Clock 8, FIFO 9 faults");
  std::uniform_int_distribution<int> page(0, 9);
  for (int trial = 0; trial < 200; ++trial) {
    std::vector<int> refs(60);
    for (auto& x : refs) x = page(rng);
    for (std::size_t f = 1; f <= 6; ++f) {
      const auto opt = os::simulate_replacement(refs, f, os::Replacement::OPT).faults;
      for (const auto r : {os::Replacement::FIFO, os::Replacement::LRU, os::Replacement::Clock}) {
        expect(opt <= os::simulate_replacement(refs, f, r).faults, "OPT has the fewest faults");
      }
      expect(os::simulate_replacement(refs, f + 1, os::Replacement::LRU).faults <=
                 os::simulate_replacement(refs, f, os::Replacement::LRU).faults,
             "LRU is a stack algorithm: more frames never mean more faults");
    }
  }
  const auto ws = os::working_set_sizes({1, 2, 1, 3, 3, 3, 4}, 3);
  expect(ws == std::vector<std::size_t>({1, 2, 2, 3, 2, 1, 2}), "working-set sizes");
}

void storage() {
  const std::vector<int> req{55, 58, 39, 18, 90, 160, 150, 38, 184};
  auto avg = [&](os::DiskPolicy p) { return os::schedule_disk(req, 100, p).average_seek; };
  expect(std::abs(avg(os::DiskPolicy::FIFO) - 55.3) < 0.05 && std::abs(avg(os::DiskPolicy::SSTF) - 27.5) < 0.1 &&
             std::abs(avg(os::DiskPolicy::LOOK) - 27.8) < 0.05 && std::abs(avg(os::DiskPolicy::CLOOK) - 35.8) < 0.05,
         "Stallings disk example: FIFO 55.3, SSTF 27.5, SCAN(LOOK) 27.8, C-SCAN(C-LOOK) 35.8");
  const int best = os::optimal_disk_movement(req, 100);
  for (const auto p : {os::DiskPolicy::FIFO, os::DiskPolicy::SSTF, os::DiskPolicy::SCAN, os::DiskPolicy::CSCAN,
                       os::DiskPolicy::LOOK, os::DiskPolicy::CLOOK}) {
    const auto s = os::schedule_disk(req, 100, p);
    auto sorted = s.order;
    std::sort(sorted.begin(), sorted.end());
    auto want = req;
    std::sort(want.begin(), want.end());
    expect(sorted == want && s.total_movement >= best, std::string("every request served, no better than optimal: ") +
                                                            os::disk_policy_name(p));
  }
  os::Raid5 raid(5);
  std::uniform_int_distribution<int> byte(0, 255);
  std::map<std::size_t, std::vector<std::uint8_t>> written;
  for (std::size_t stripe = 0; stripe < 20; ++stripe) {
    std::vector<std::uint8_t> d(4);
    for (auto& x : d) x = static_cast<std::uint8_t>(byte(rng));
    raid.write_stripe(stripe, d);
    written[stripe] = d;
  }
  bool ok = true;
  for (std::size_t failed = 0; failed < 5; ++failed) {
    os::Raid5 copy = raid;
    copy.fail_disk(failed);
    for (const auto& [stripe, d] : written) ok &= copy.read_stripe_degraded(stripe) == d;
  }
  expect(ok, "RAID 5 reconstructs every stripe after any single disk failure");
  // file system against a reference model
  os::FileSystem fs(400, 64, 64);
  const std::size_t free0 = fs.free_blocks();
  expect(fs.max_file_size() == (10 + 8 + 64) * 64, "maximum file size with direct, indirect, double indirect");
  std::map<std::string, std::string> model;
  expect(fs.mkdir("/home") && fs.mkdir("/home/user") && !fs.mkdir("/home"), "directories");
  std::uniform_int_distribution<int> op(0, 3), name(0, 5), len(0, 300);
  for (int step = 0; step < 400; ++step) {
    const std::string path = "/home/user/f" + std::to_string(name(rng));
    const int o = op(rng);
    if (o == 0) {
      const bool created = fs.create(path);
      expect(created == (model.count(path) == 0), "create succeeds exactly for new files");
      if (created) model[path] = "";
    } else if (o == 1 && model.count(path)) {
      std::string data(static_cast<std::size_t>(len(rng)), 'x');
      for (auto& c : data) c = static_cast<char>('a' + rng() % 26);
      const std::size_t off = model[path].size() > 0 ? rng() % model[path].size() : 0;
      fs.write(path, off, data);
      auto& m = model[path];
      if (m.size() < off + data.size()) m.resize(off + data.size(), '\0');
      m.replace(off, data.size(), data);
    } else if (o == 2 && model.count(path)) {
      expect(fs.unlink(path), "unlink existing file");
      model.erase(path);
    }
    for (const auto& [p, content] : model) {
      if (fs.read(p) != content) {
        expect(false, "file contents equal the reference model: " + p);
        break;
      }
    }
  }
  std::string report;
  expect(fs.fsck(&report), "fsck clean after random operations: " + report);
  // a large file through the double-indirect block, then hard links
  expect(fs.create("/big") && fs.write("/big", 0, std::string(5000, 'z')), "large file written");
  expect(fs.read("/big") == std::string(5000, 'z') && fs.blocks_of("/big") > 79, "large file uses indirect blocks");
  expect(fs.link("/big", "/home/alias") && fs.unlink("/big") && fs.read("/home/alias") == std::string(5000, 'z'),
         "hard link keeps the data after unlinking the original name");
  expect(fs.unlink("/home/alias") && fs.fsck(&report), "last link frees the inode");
  for (const auto& [p, content] : model) fs.unlink(p);
  expect(fs.unlink("/home/user") && fs.unlink("/home") && fs.free_blocks() == free0, "all blocks freed again");
}

}  // namespace

int main() {
  processes_and_scheduling();
  concurrency();
  deadlock();
  memory();
  virtual_memory();
  storage();
  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "os tests passed\n";
  return 0;
}
