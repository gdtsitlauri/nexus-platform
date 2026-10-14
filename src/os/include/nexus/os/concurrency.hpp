#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Concurrency (EY517): mutual exclusion and synchronisation.
//
// 1. An explicit-state model checker for small concurrent programs. Every interleaving of the threads' atomic steps
//    is explored (depth-first, with a visited set), and the checker reports mutual-exclusion violations, deadlocks
//    and failed final assertions with a counterexample schedule. Program format:
//      shared x=0 turn=0            shared variables (also semaphores and locks)
//      thread NAME                  starts a thread; its local registers are %a, %b, ...
//      [label:] op args             one atomic step per line
//    Ops: set v k | copy v w | load %r v | store v %r | add %r k | jmp L | jeq v k L | jne v k L
//         await v k (blocks until v == k) | tas %r v (atomic test-and-set) | P s | V s (semaphore wait/signal)
//         cs | endcs (critical section) | halt
//      assert v k                   checked when every thread has halted
// 2. Synchronisation primitives on real threads: Peterson's lock, a counting semaphore, a bounded buffer and a
//    readers-writers lock.
namespace nexus::os {

struct ConcurrentProgram {
  struct Instr {
    std::string op;
    std::vector<std::string> args;
    std::size_t target = 0;  // resolved jump target
  };
  std::map<std::string, std::int64_t> shared_init;
  std::vector<std::string> thread_names;
  std::vector<std::vector<Instr>> threads;
  std::vector<std::pair<std::string, std::int64_t>> asserts;
};

ConcurrentProgram parse_concurrent(const std::string& text);

struct CheckResult {
  std::size_t states = 0;
  std::size_t transitions = 0;
  bool mutual_exclusion = true;
  bool deadlock_free = true;
  bool assertions_hold = true;
  std::vector<std::int64_t> final_values_seen;  // distinct values of the first asserted variable at termination
  std::vector<std::string> counterexample;      // steps "thread: op" leading to the first violation
  std::string violation;
};

CheckResult model_check(const ConcurrentProgram& p, std::size_t max_states = 2'000'000);

// ---------------------------------------------------------------- real threads

class PetersonLock {
 public:
  void lock(int self);
  void unlock(int self);

 private:
  std::atomic<bool> flag_[2] = {false, false};
  std::atomic<int> turn_{0};
};

class Semaphore {
 public:
  explicit Semaphore(int initial) : count_(initial) {}
  void wait();    // P
  void signal();  // V

 private:
  std::mutex m_;
  std::condition_variable cv_;
  int count_;
};

// Producer-consumer bounded buffer with the classic three semaphores.
class BoundedBuffer {
 public:
  explicit BoundedBuffer(std::size_t capacity);
  void put(int item);
  int take();

 private:
  std::vector<int> items_;
  std::size_t in_ = 0, out_ = 0;
  Semaphore empty_, full_, mutex_;
};

// Readers-writers lock with writer preference.
class ReadersWriters {
 public:
  void start_read();
  void end_read();
  void start_write();
  void end_write();
  int active_readers() const { return readers_.load(); }
  bool writer_active() const { return writer_.load(); }

 private:
  std::mutex m_;
  std::condition_variable cv_;
  std::atomic<int> readers_{0};
  std::atomic<bool> writer_{false};
  int waiting_writers_ = 0;
};

}  // namespace nexus::os
