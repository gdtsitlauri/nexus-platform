#include <algorithm>
#include <iostream>
#include <optional>
#include <string>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/functional/interpreter.hpp"
#include "nexus/sim/memory/system.hpp"
#include "nexus/sim/pipeline/model.hpp"

namespace {

std::optional<nexus::mips::loader::LoadedProgram> load(std::string_view text) {
  const auto parsed = nexus::mips::loader::load_program_from_text(std::string(text));
  if (!parsed.diagnostics.empty()) {
    return std::nullopt;
  }
  return parsed.program;
}

bool contains_line(const std::vector<std::string>& lines, std::string_view needle) {
  return std::any_of(lines.begin(), lines.end(), [needle](const std::string& line) {
    return line.find(needle) != std::string::npos;
  });
}

}  // namespace

int main() {
  const auto cache_program = load(R"(
main:
  addiu $t0, $zero, 0
  addiu $t1, $zero, 5
  sw $t1, 0($t0)
  lw $t2, 0($t0)
  addu $v0, $t2, $zero
  jr $ra
)");
  if (!cache_program.has_value()) {
    std::cerr << "Failed to load cache demo program.\n";
    return 1;
  }

  const auto cache_functional = nexus::sim::functional::run_program(*cache_program);
  const auto cache_pipeline = nexus::sim::pipeline::run_program(
      *cache_program,
      {
          .cache_mode = nexus::sim::memory::CacheMode::DirectMapped,
          .cache_sets = 2,
          .cache_line_words = 1,
          .cache_hit_latency = 1,
          .cache_miss_penalty = 3,
      });
  if (!cache_functional.success || !cache_pipeline.success || cache_pipeline.exit_code != 5 ||
      cache_pipeline.registers != cache_functional.registers || cache_pipeline.memory_accesses != 2U ||
      cache_pipeline.cache_hits != 1U || cache_pipeline.cache_misses != 1U) {
    std::cerr << "Pipeline cache-integrated execution is incorrect.\n";
    return 1;
  }

  const auto hierarchy_program = load(R"(
main:
  addiu $t0, $zero, 0
  addiu $t1, $zero, 7
  sw $t1, 0($t0)
  addiu $t0, $zero, 4
  addiu $t1, $zero, 9
  sw $t1, 0($t0)
  addiu $t0, $zero, 0
  lw $v0, 0($t0)
  jr $ra
)");
  if (!hierarchy_program.has_value()) {
    std::cerr << "Failed to load hierarchy demo program.\n";
    return 1;
  }

  const auto hierarchy_result = nexus::sim::pipeline::run_program(
      *hierarchy_program,
      {
          .cache_mode = nexus::sim::memory::CacheMode::DirectMapped,
          .cache_sets = 1,
          .cache_line_words = 1,
          .l2_cache_mode = nexus::sim::memory::CacheMode::SetAssociative,
          .l2_cache_sets = 1,
          .l2_cache_line_words = 1,
          .l2_cache_ways = 2,
          .memory_latency = 1,
          .cache_hit_latency = 1,
          .cache_miss_penalty = 4,
          .l2_cache_hit_latency = 3,
          .l2_cache_miss_penalty = 9,
      });
  if (!hierarchy_result.success || hierarchy_result.exit_code != 7 || hierarchy_result.l1_hits != 0U ||
      hierarchy_result.l1_misses != 3U || hierarchy_result.l2_hits != 1U ||
      hierarchy_result.l2_misses != 2U) {
    std::cerr << "Pipeline multi-level cache accounting is incorrect.\n";
    return 1;
  }

  const auto io_program = load(R"(
main:
  lui $t0, 1
  addiu $t1, $zero, 65
  sw $t1, 0($t0)
  addiu $v0, $zero, 65
  jr $ra
)");
  if (!io_program.has_value()) {
    std::cerr << "Failed to load I/O demo program.\n";
    return 1;
  }

  const auto io_result = nexus::sim::pipeline::run_program(*io_program, {.io_demo = true});
  if (!io_result.success || io_result.exit_code != 65 ||
      !contains_line(io_result.system_lines, "io[console]: value=65 char='A'")) {
    std::cerr << "Pipeline I/O demo behavior is incorrect.\n";
    return 1;
  }

  const auto interrupt_program = load(R"(
main:
  lui $t3, 1
  addiu $t0, $zero, 3
  sw $t0, 256($t3)
  addiu $t0, $zero, 1
  sw $t0, 260($t3)
  addiu $t1, $zero, 0
loop:
  beq $t1, $zero, loop
  addu $v0, $t1, $zero
  jr $ra
interrupt_handler:
  addiu $t1, $zero, 77
  jr $ra
)");
  if (!interrupt_program.has_value()) {
    std::cerr << "Failed to load interrupt demo program.\n";
    return 1;
  }

  const auto interrupt_result =
      nexus::sim::pipeline::run_program(*interrupt_program, {.trace = true, .interrupt_demo = true});
  if (!interrupt_result.success || interrupt_result.exit_code != 77 || interrupt_result.interrupts_handled != 1U ||
      !contains_line(interrupt_result.system_lines, "interrupt[timer]: dispatch")) {
    std::cerr << "Pipeline interrupt behavior is incorrect.\n";
    return 1;
  }

  const auto dma_program = load(R"(
main:
  addiu $t0, $zero, 0
  addiu $t1, $zero, 21
  sw $t1, 0($t0)
  addiu $t1, $zero, 22
  sw $t1, 4($t0)
  lui $t2, 1
  addiu $t3, $zero, 0
  sw $t3, 512($t2)
  addiu $t3, $zero, 64
  sw $t3, 516($t2)
  addiu $t3, $zero, 2
  sw $t3, 520($t2)
  addiu $t3, $zero, 1
  sw $t3, 524($t2)
  addiu $t5, $zero, 1
wait:
  lw $t4, 528($t2)
  bne $t4, $t5, wait
  lw $v0, 64($zero)
  jr $ra
)");
  if (!dma_program.has_value()) {
    std::cerr << "Failed to load DMA demo program.\n";
    return 1;
  }

  const auto dma_result = nexus::sim::pipeline::run_program(*dma_program, {.dma_demo = true});
  if (!dma_result.success || dma_result.exit_code != 21 || dma_result.dma_words_copied != 2U ||
      !contains_line(dma_result.system_lines, "dma(complete")) {
    std::cerr << "Pipeline DMA behavior is incorrect.\n";
    return 1;
  }

  return 0;
}
