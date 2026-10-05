#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/common/banner.hpp"
#include "nexus/common/fpu_lite.hpp"
#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/advanced/model.hpp"
#include "nexus/sim/functional/interpreter.hpp"
#include "nexus/sim/memory/system.hpp"
#include "nexus/sim/metrics/report.hpp"
#include "nexus/sim/multi_cycle/control.hpp"
#include "nexus/sim/multi_cycle/model.hpp"
#include "nexus/sim/parallel/model.hpp"
#include "nexus/sim/pipeline/model.hpp"
#include "nexus/sim/single_cycle/model.hpp"

namespace {

void print_usage(std::ostream& stream) {
  stream << "Usage:\n"
         << "  mips-sim --help\n"
         << "  mips-sim fp-demo <lhs> <rhs>\n"
         << "  mips-sim run <file> --mode functional [--trace] [--stats]\n"
         << "  mips-sim run <file> --mode single-cycle [--trace] [--stats]\n"
         << "  mips-sim run <file> --mode multi-cycle [--control hardwired|microcode] [--trace] [--stats]\n"
         << "  mips-sim run <file> --mode pipeline [--predictor static-not-taken|static-taken|static-btfnt]\n"
         << "               [--cache off|direct|assoc] [--ways N] [--sets N] [--line-words N]\n"
         << "               [--cache-l2 off|direct|assoc] [--l2-ways N] [--l2-sets N] [--l2-line-words N]\n"
         << "               [--memory-latency N] [--cache-hit-latency N] [--cache-miss-penalty N]\n"
         << "               [--l2-hit-latency N] [--l2-miss-penalty N]\n"
         << "               [--trace] [--timeline] [--stats] [--io-demo] [--interrupt-demo] [--dma-demo]\n"
         << "  mips-sim run <file> --mode advanced [--predictor static-not-taken|2bit]\n"
         << "               [--scheduler inorder|vliw-lite|scoreboard] [--issue-width 1|2] [--vliw]\n"
         << "               [--trace] [--stats]\n"
         << "  mips-sim run <file> --mode advanced --scheduler tomasulo [--issue-width 1-4] [--rob N]\n"
         << "               [--rs N] [--cdb N] [--pipeline-depth N] [--smt <thread1.s>] [--trace] [--stats]\n"
         << "  mips-sim run <file> --mode parallel [--cores N] [--coherence snoop|directory-lite]\n"
         << "               [--consistency sc|weak-lite] [--interconnect bus|switch|noc-lite]\n"
         << "               [--trace] [--stats]\n";
}

int print_loader_diagnostics(const std::vector<std::string>& diagnostics) {
  for (const auto& diagnostic : diagnostics) {
    std::cerr << "error: " << diagnostic << '\n';
  }
  return diagnostics.empty() ? 0 : 1;
}

int print_summary(const nexus::sim::metrics::ExecutionSummary& summary) {
  std::cout << nexus::sim::metrics::format_summary(summary);
  return 0;
}

std::optional<nexus::sim::multi_cycle::ControlStyle> parse_control_style(std::string_view text) {
  using nexus::sim::multi_cycle::ControlStyle;
  if (text == "hardwired") {
    return ControlStyle::Hardwired;
  }
  if (text == "microcode") {
    return ControlStyle::Microcode;
  }
  return std::nullopt;
}

std::optional<nexus::sim::pipeline::PredictorKind> parse_predictor(std::string_view text) {
  using nexus::sim::pipeline::PredictorKind;
  if (text == "static-not-taken") {
    return PredictorKind::StaticNotTaken;
  }
  if (text == "static-taken") {
    return PredictorKind::StaticTaken;
  }
  if (text == "static-btfnt") {
    return PredictorKind::StaticBackwardTakenForwardNotTaken;
  }
  return std::nullopt;
}

std::optional<nexus::sim::advanced::PredictorKind> parse_advanced_predictor(std::string_view text) {
  using nexus::sim::advanced::PredictorKind;
  if (text == "static-not-taken") {
    return PredictorKind::StaticNotTaken;
  }
  if (text == "2bit") {
    return PredictorKind::TwoBit;
  }
  return std::nullopt;
}

std::optional<nexus::sim::advanced::SchedulerKind> parse_advanced_scheduler(std::string_view text) {
  using nexus::sim::advanced::SchedulerKind;
  if (text == "inorder") {
    return SchedulerKind::InOrder;
  }
  if (text == "vliw-lite") {
    return SchedulerKind::VliwLite;
  }
  if (text == "scoreboard") {
    return SchedulerKind::Scoreboard;
  }
  if (text == "tomasulo") {
    return SchedulerKind::Tomasulo;
  }
  return std::nullopt;
}

std::optional<nexus::sim::memory::CacheMode> parse_cache_mode(std::string_view text) {
  using nexus::sim::memory::CacheMode;
  if (text == "off") {
    return CacheMode::Off;
  }
  if (text == "direct") {
    return CacheMode::DirectMapped;
  }
  if (text == "assoc") {
    return CacheMode::SetAssociative;
  }
  return std::nullopt;
}

std::optional<nexus::sim::parallel::CoherenceKind> parse_parallel_coherence(std::string_view text) {
  using nexus::sim::parallel::CoherenceKind;
  if (text == "snoop") {
    return CoherenceKind::SnoopingLite;
  }
  if (text == "directory-lite") {
    return CoherenceKind::DirectoryLite;
  }
  return std::nullopt;
}

std::optional<nexus::sim::parallel::ConsistencyKind> parse_parallel_consistency(std::string_view text) {
  using nexus::sim::parallel::ConsistencyKind;
  if (text == "sc") {
    return ConsistencyKind::Sequential;
  }
  if (text == "weak-lite") {
    return ConsistencyKind::WeakLite;
  }
  return std::nullopt;
}

std::optional<nexus::sim::parallel::InterconnectKind> parse_parallel_interconnect(std::string_view text) {
  using nexus::sim::parallel::InterconnectKind;
  if (text == "bus") {
    return InterconnectKind::Bus;
  }
  if (text == "switch") {
    return InterconnectKind::Switch;
  }
  if (text == "noc-lite") {
    return InterconnectKind::NoCLite;
  }
  return std::nullopt;
}

std::optional<std::size_t> parse_size_value(std::string_view text) {
  try {
    std::size_t parsed = 0;
    const unsigned long long value = std::stoull(std::string(text), &parsed, 10);
    if (parsed != text.size()) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(value);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<float> parse_float_value(std::string_view text) {
  try {
    std::size_t parsed = 0;
    const float value = std::stof(std::string(text), &parsed);
    if (parsed != text.size()) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

std::string format_cache_name(nexus::sim::memory::CacheMode mode, std::size_t ways) {
  using nexus::sim::memory::CacheMode;
  switch (mode) {
    case CacheMode::Off:
      return "off";
    case CacheMode::DirectMapped:
      return "direct-mapped";
    case CacheMode::SetAssociative:
      return std::to_string(std::max<std::size_t>(2, ways)) + "-way set-associative";
  }
  return "unknown";
}

std::string format_hierarchy_name(
    nexus::sim::memory::CacheMode l1_mode,
    std::size_t l1_ways,
    nexus::sim::memory::CacheMode l2_mode,
    std::size_t l2_ways) {
  std::string name = format_cache_name(l1_mode, l1_ways);
  if (l2_mode != nexus::sim::memory::CacheMode::Off) {
    name += " + L2 " + format_cache_name(l2_mode, l2_ways);
  }
  return name;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 1) {
    std::cout << nexus::common::banner_text("mips-sim") << '\n'
              << "Commands: fp-demo <lhs> <rhs>, run <file> --mode functional|single-cycle|multi-cycle|pipeline|advanced|parallel [--trace] [--stats], --help\n";
    return 0;
  }

  const std::string command = argv[1];
  if (command == "--help" || command == "help") {
    print_usage(std::cout);
    return 0;
  }

  if (command == "fp-demo") {
    if (argc != 4) {
      print_usage(std::cerr);
      return 1;
    }
    const auto lhs = parse_float_value(argv[2]);
    const auto rhs = parse_float_value(argv[3]);
    if (!lhs.has_value() || !rhs.has_value()) {
      std::cerr << "error: fp-demo expects two floating-point values\n";
      return 1;
    }
    std::cout << nexus::common::format_float32_demo(nexus::common::run_float32_demo(*lhs, *rhs));
    return 0;
  }

  if (command != "run" || argc < 3) {
    print_usage(std::cerr);
    return 1;
  }

  std::string mode = "functional";
  std::optional<std::string> control_name;
  std::optional<std::string> predictor_name;
  std::optional<std::string> cache_name;
  std::optional<std::string> l2_cache_name;
  std::optional<std::string> coherence_name;
  std::optional<std::string> consistency_name;
  std::optional<std::string> interconnect_name;
  bool trace = false;
  bool timeline = false;
  bool stats = false;
  bool io_demo = false;
  bool interrupt_demo = false;
  bool dma_demo = false;
  bool vliw = false;
  std::size_t cache_ways = 2;
  std::size_t cache_sets = 16;
  std::size_t cache_line_words = 4;
  std::size_t l2_cache_ways = 4;
  std::size_t l2_cache_sets = 32;
  std::size_t l2_cache_line_words = 4;
  std::size_t cores = 2;
  std::size_t issue_width = 1;
  std::size_t memory_latency = 1;
  std::size_t cache_hit_latency = 1;
  std::size_t cache_miss_penalty = 6;
  std::size_t l2_cache_hit_latency = 4;
  std::size_t l2_cache_miss_penalty = 16;
  std::optional<std::string> scheduler_name;
  std::optional<std::size_t> rob_entries;
  std::optional<std::size_t> reservation_stations;
  std::optional<std::size_t> cdb_width;
  std::optional<std::size_t> pipeline_depth;
  std::optional<std::string> smt_file;

  for (int index = 3; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--trace") {
      trace = true;
      continue;
    }
    if (option == "--timeline") {
      timeline = true;
      continue;
    }
    if (option == "--stats") {
      stats = true;
      continue;
    }
    if (option == "--io-demo") {
      io_demo = true;
      continue;
    }
    if (option == "--interrupt-demo") {
      interrupt_demo = true;
      continue;
    }
    if (option == "--dma-demo") {
      dma_demo = true;
      continue;
    }
    if (option == "--mode") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing simulator mode after --mode\n";
        return 1;
      }
      mode = argv[++index];
      continue;
    }
    if (option == "--control") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing control style after --control\n";
        return 1;
      }
      control_name = argv[++index];
      continue;
    }
    if (option == "--predictor") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing predictor after --predictor\n";
        return 1;
      }
      predictor_name = argv[++index];
      continue;
    }
    if (option == "--cache") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing cache mode after --cache\n";
        return 1;
      }
      cache_name = argv[++index];
      continue;
    }
    if (option == "--cache-l2") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing cache mode after --cache-l2\n";
        return 1;
      }
      l2_cache_name = argv[++index];
      continue;
    }
    if (option == "--coherence") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing coherence mode after --coherence\n";
        return 1;
      }
      coherence_name = argv[++index];
      continue;
    }
    if (option == "--consistency") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing consistency model after --consistency\n";
        return 1;
      }
      consistency_name = argv[++index];
      continue;
    }
    if (option == "--interconnect") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing interconnect after --interconnect\n";
        return 1;
      }
      interconnect_name = argv[++index];
      continue;
    }
    if (option == "--scheduler") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing scheduler after --scheduler\n";
        return 1;
      }
      scheduler_name = argv[++index];
      continue;
    }
    if (option == "--vliw") {
      vliw = true;
      continue;
    }
    if (option == "--smt") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing program for hardware thread 1 after --smt\n";
        return 1;
      }
      smt_file = argv[++index];
      continue;
    }
    if (option == "--rob" || option == "--rs" || option == "--cdb" || option == "--pipeline-depth") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing numeric value after '" << option << "'\n";
        return 1;
      }
      const auto parsed_value = parse_size_value(argv[++index]);
      if (!parsed_value.has_value() || *parsed_value == 0) {
        std::cerr << "error: expected a positive integer after '" << option << "'\n";
        return 1;
      }
      if (option == "--rob") {
        rob_entries = *parsed_value;
      } else if (option == "--rs") {
        reservation_stations = *parsed_value;
      } else if (option == "--cdb") {
        cdb_width = *parsed_value;
      } else {
        if (*parsed_value < 5) {
          std::cerr << "error: --pipeline-depth must be at least 5\n";
          return 1;
        }
        pipeline_depth = *parsed_value;
      }
      continue;
    }
    if (option == "--ways" || option == "--sets" || option == "--line-words" || option == "--cores" ||
        option == "--issue-width" || option == "--l2-ways" || option == "--l2-sets" ||
        option == "--l2-line-words" || option == "--memory-latency" ||
        option == "--cache-hit-latency" || option == "--cache-miss-penalty" ||
        option == "--l2-hit-latency" || option == "--l2-miss-penalty") {
      if (index + 1 >= argc) {
        std::cerr << "error: missing numeric value after '" << option << "'\n";
        return 1;
      }
      const auto parsed = parse_size_value(argv[++index]);
      if (!parsed.has_value() || *parsed == 0) {
        std::cerr << "error: expected a positive integer after '" << option << "'\n";
        return 1;
      }
      if (option == "--ways") {
        cache_ways = *parsed;
      } else if (option == "--sets") {
        cache_sets = *parsed;
      } else if (option == "--line-words") {
        cache_line_words = *parsed;
      } else if (option == "--l2-ways") {
        l2_cache_ways = *parsed;
      } else if (option == "--l2-sets") {
        l2_cache_sets = *parsed;
      } else if (option == "--l2-line-words") {
        l2_cache_line_words = *parsed;
      } else if (option == "--cores") {
        cores = *parsed;
      } else if (option == "--issue-width") {
        issue_width = *parsed;
      } else if (option == "--memory-latency") {
        memory_latency = *parsed;
      } else if (option == "--cache-hit-latency") {
        cache_hit_latency = *parsed;
      } else if (option == "--cache-miss-penalty") {
        cache_miss_penalty = *parsed;
      } else if (option == "--l2-hit-latency") {
        l2_cache_hit_latency = *parsed;
      } else if (option == "--l2-miss-penalty") {
        l2_cache_miss_penalty = *parsed;
      }
      continue;
    }
    std::cerr << "error: unknown option '" << option << "'\n";
    print_usage(std::cerr);
    return 1;
  }

  if (mode != "functional" && mode != "single-cycle" && mode != "multi-cycle" &&
      mode != "pipeline" && mode != "advanced" && mode != "parallel") {
    std::cerr << "error: unsupported mode '" << mode << "'\n";
    return 1;
  }

  if (control_name.has_value() && mode != "multi-cycle") {
    std::cerr << "error: --control is only valid with --mode multi-cycle\n";
    return 1;
  }
  if (predictor_name.has_value() && mode != "pipeline" && mode != "advanced") {
    std::cerr << "error: --predictor is only valid with --mode pipeline or --mode advanced\n";
    return 1;
  }
  if (cache_name.has_value() && mode != "pipeline") {
    std::cerr << "error: --cache is only valid with --mode pipeline\n";
    return 1;
  }
  if (l2_cache_name.has_value() && mode != "pipeline") {
    std::cerr << "error: --cache-l2 is only valid with --mode pipeline\n";
    return 1;
  }
  if (coherence_name.has_value() && mode != "parallel") {
    std::cerr << "error: --coherence is only valid with --mode parallel\n";
    return 1;
  }
  if (consistency_name.has_value() && mode != "parallel") {
    std::cerr << "error: --consistency is only valid with --mode parallel\n";
    return 1;
  }
  if (interconnect_name.has_value() && mode != "parallel") {
    std::cerr << "error: --interconnect is only valid with --mode parallel\n";
    return 1;
  }
  if (cores != 2 && mode != "parallel") {
    std::cerr << "error: --cores is only valid with --mode parallel\n";
    return 1;
  }
  if (timeline && mode != "pipeline") {
    std::cerr << "error: --timeline is only valid with --mode pipeline\n";
    return 1;
  }
  if ((io_demo || interrupt_demo || dma_demo) && mode != "pipeline") {
    std::cerr << "error: demo flags are only valid with --mode pipeline\n";
    return 1;
  }

  if (scheduler_name.has_value() && mode != "advanced") {
    std::cerr << "error: --scheduler is only valid with --mode advanced\n";
    return 1;
  }
  if (vliw && mode != "advanced") {
    std::cerr << "error: --vliw is only valid with --mode advanced\n";
    return 1;
  }
  if ((rob_entries || reservation_stations || cdb_width || smt_file) && scheduler_name.value_or("") != "tomasulo") {
    std::cerr << "error: --rob, --rs, --cdb and --smt are only valid with --scheduler tomasulo\n";
    return 1;
  }
  if (pipeline_depth && mode != "advanced") {
    std::cerr << "error: --pipeline-depth is only valid with --mode advanced\n";
    return 1;
  }

  const auto parsed = nexus::mips::loader::load_program_from_file(argv[2]);
  if (!parsed.diagnostics.empty()) {
    return print_loader_diagnostics(parsed.diagnostics);
  }

  if (mode == "functional") {
    const auto result = nexus::sim::functional::run_program(*parsed.program, {.trace = trace});
    if (!result.success) {
      std::cerr << "error: " << result.error << '\n';
      return 1;
    }

    for (const auto& line : result.trace_lines) {
      std::cout << line << '\n';
    }
    return print_summary({
        .mode = "functional",
        .cores = std::nullopt,
        .control = std::nullopt,
        .scheduler = std::nullopt,
        .predictor = std::nullopt,
        .cache = std::nullopt,
        .coherence = std::nullopt,
        .consistency = std::nullopt,
        .interconnect = std::nullopt,
        .issue_width = std::nullopt,
        .exit_code = result.exit_code,
        .instructions = result.executed_instructions,
        .cycles = result.executed_instructions,
        .cpi = std::nullopt,
        .ipc = std::nullopt,
        .stalls = std::nullopt,
        .load_use_stalls = std::nullopt,
        .flushes = std::nullopt,
        .forwardings = std::nullopt,
        .branch_predictions = std::nullopt,
        .branch_mispredictions = std::nullopt,
        .speculative_flush_cycles = std::nullopt,
        .slot_utilization = std::nullopt,
        .memory_accesses = std::nullopt,
        .memory_reads = std::nullopt,
        .memory_writes = std::nullopt,
        .cache_hits = std::nullopt,
        .cache_misses = std::nullopt,
        .cache_miss_rate = std::nullopt,
        .l1_hits = std::nullopt,
        .l1_misses = std::nullopt,
        .l2_hits = std::nullopt,
        .l2_misses = std::nullopt,
        .l2_miss_rate = std::nullopt,
        .io_reads = std::nullopt,
        .io_writes = std::nullopt,
        .interrupts_handled = std::nullopt,
        .dma_words_copied = std::nullopt,
        .coherence_events = std::nullopt,
        .invalidations = std::nullopt,
        .directory_lookups = std::nullopt,
        .synchronization_events = std::nullopt,
        .lock_acquisitions = std::nullopt,
        .lock_contentions = std::nullopt,
        .barrier_wait_cycles = std::nullopt,
        .atomic_operations = std::nullopt,
        .store_buffer_flushes = std::nullopt,
        .interconnect_messages = std::nullopt,
        .interconnect_cycles = std::nullopt,
    });
  }

  if (mode == "single-cycle") {
    const auto result = nexus::sim::single_cycle::run_program(*parsed.program, {.trace = trace});
    if (!result.success) {
      std::cerr << "error: " << result.error << '\n';
      return 1;
    }

    for (const auto& line : result.trace_lines) {
      std::cout << line << '\n';
    }
    return print_summary({
        .mode = "single-cycle",
        .cores = std::nullopt,
        .control = std::nullopt,
        .scheduler = std::nullopt,
        .predictor = std::nullopt,
        .cache = std::nullopt,
        .coherence = std::nullopt,
        .consistency = std::nullopt,
        .interconnect = std::nullopt,
        .issue_width = std::nullopt,
        .exit_code = result.exit_code,
        .instructions = result.executed_instructions,
        .cycles = result.cycles,
        .cpi = std::nullopt,
        .ipc = std::nullopt,
        .stalls = std::nullopt,
        .load_use_stalls = std::nullopt,
        .flushes = std::nullopt,
        .forwardings = std::nullopt,
        .branch_predictions = std::nullopt,
        .branch_mispredictions = std::nullopt,
        .speculative_flush_cycles = std::nullopt,
        .slot_utilization = std::nullopt,
        .memory_accesses = std::nullopt,
        .memory_reads = std::nullopt,
        .memory_writes = std::nullopt,
        .cache_hits = std::nullopt,
        .cache_misses = std::nullopt,
        .cache_miss_rate = std::nullopt,
        .l1_hits = std::nullopt,
        .l1_misses = std::nullopt,
        .l2_hits = std::nullopt,
        .l2_misses = std::nullopt,
        .l2_miss_rate = std::nullopt,
        .io_reads = std::nullopt,
        .io_writes = std::nullopt,
        .interrupts_handled = std::nullopt,
        .dma_words_copied = std::nullopt,
        .coherence_events = std::nullopt,
        .invalidations = std::nullopt,
        .directory_lookups = std::nullopt,
        .synchronization_events = std::nullopt,
        .lock_acquisitions = std::nullopt,
        .lock_contentions = std::nullopt,
        .barrier_wait_cycles = std::nullopt,
        .atomic_operations = std::nullopt,
        .store_buffer_flushes = std::nullopt,
        .interconnect_messages = std::nullopt,
        .interconnect_cycles = std::nullopt,
    });
  }

  if (mode == "pipeline") {
    const auto predictor = parse_predictor(predictor_name.value_or("static-not-taken"));
    if (!predictor.has_value()) {
      std::cerr << "error: unsupported predictor '" << predictor_name.value_or("?")
                << "', expected 'static-not-taken', 'static-taken', or 'static-btfnt'\n";
      return 1;
    }
    const auto cache_mode = parse_cache_mode(cache_name.value_or("off"));
    if (!cache_mode.has_value()) {
      std::cerr << "error: unsupported cache mode '" << cache_name.value_or("?")
                << "', expected 'off', 'direct', or 'assoc'\n";
      return 1;
    }
    const auto l2_cache_mode = parse_cache_mode(l2_cache_name.value_or("off"));
    if (!l2_cache_mode.has_value()) {
      std::cerr << "error: unsupported L2 cache mode '" << l2_cache_name.value_or("?")
                << "', expected 'off', 'direct', or 'assoc'\n";
      return 1;
    }
    if (*l2_cache_mode != nexus::sim::memory::CacheMode::Off &&
        *cache_mode == nexus::sim::memory::CacheMode::Off) {
      std::cerr << "error: --cache-l2 requires an enabled L1 cache via --cache\n";
      return 1;
    }

    const auto result = nexus::sim::pipeline::run_program(
        *parsed.program,
        {
            .trace = trace,
            .timeline = timeline,
            .predictor = *predictor,
            .memory_words = 1U << 18,
            .cache_mode = *cache_mode,
            .cache_sets = cache_sets,
            .cache_line_words = cache_line_words,
            .cache_ways = cache_ways,
            .l2_cache_mode = *l2_cache_mode,
            .l2_cache_sets = l2_cache_sets,
            .l2_cache_line_words = l2_cache_line_words,
            .l2_cache_ways = l2_cache_ways,
            .memory_latency = memory_latency,
            .cache_hit_latency = cache_hit_latency,
            .cache_miss_penalty = cache_miss_penalty,
            .l2_cache_hit_latency = l2_cache_hit_latency,
            .l2_cache_miss_penalty = l2_cache_miss_penalty,
            .io_demo = io_demo,
            .interrupt_demo = interrupt_demo,
            .dma_demo = dma_demo,
        });
    if (!result.success) {
      std::cerr << "error: " << result.error << '\n';
      return 1;
    }

    for (const auto& line : result.trace_lines) {
      std::cout << line << '\n';
    }
    for (const auto& line : result.timeline_lines) {
      std::cout << line << '\n';
    }
    for (const auto& line : result.system_lines) {
      std::cout << line << '\n';
    }

    const bool show_extended_stats =
        stats || *cache_mode != nexus::sim::memory::CacheMode::Off || io_demo || interrupt_demo || dma_demo;
    const double cpi = result.retired_instructions == 0
        ? 0.0
        : static_cast<double>(result.cycles) / static_cast<double>(result.retired_instructions);
    const double ipc = result.cycles == 0
        ? 0.0
        : static_cast<double>(result.retired_instructions) / static_cast<double>(result.cycles);
    const double miss_rate = (result.cache_hits + result.cache_misses) == 0
        ? 0.0
        : static_cast<double>(result.cache_misses) /
            static_cast<double>(result.cache_hits + result.cache_misses);
    const double l2_miss_rate = (result.l2_hits + result.l2_misses) == 0
        ? 0.0
        : static_cast<double>(result.l2_misses) /
            static_cast<double>(result.l2_hits + result.l2_misses);
    return print_summary({
        .mode = "pipeline",
        .cores = std::nullopt,
        .control = std::nullopt,
        .scheduler = std::nullopt,
        .predictor = std::string(nexus::sim::pipeline::predictor_name(*predictor)),
        .cache = show_extended_stats
            ? std::optional<std::string>(
                  format_hierarchy_name(*cache_mode, cache_ways, *l2_cache_mode, l2_cache_ways))
                                     : std::nullopt,
        .coherence = std::nullopt,
        .consistency = std::nullopt,
        .interconnect = std::nullopt,
        .issue_width = std::nullopt,
        .exit_code = result.exit_code,
        .instructions = result.retired_instructions,
        .cycles = result.cycles,
        .cpi = cpi,
        .ipc = show_extended_stats ? std::optional<double>(ipc) : std::nullopt,
        .stalls = result.stall_cycles,
        .load_use_stalls = result.load_use_stalls,
        .flushes = result.flushes,
        .forwardings = result.forwarding_events,
        .branch_predictions = result.branch_predictions,
        .branch_mispredictions = result.branch_mispredictions,
        .speculative_flush_cycles = std::nullopt,
        .slot_utilization = std::nullopt,
        .memory_accesses = show_extended_stats ? std::optional<std::size_t>(result.memory_accesses)
                                               : std::nullopt,
        .memory_reads = show_extended_stats ? std::optional<std::size_t>(result.memory_reads)
                                            : std::nullopt,
        .memory_writes = show_extended_stats ? std::optional<std::size_t>(result.memory_writes)
                                             : std::nullopt,
        .cache_hits = show_extended_stats ? std::optional<std::size_t>(result.cache_hits) : std::nullopt,
        .cache_misses = show_extended_stats ? std::optional<std::size_t>(result.cache_misses) : std::nullopt,
        .cache_miss_rate = show_extended_stats ? std::optional<double>(miss_rate) : std::nullopt,
        .l1_hits = show_extended_stats ? std::optional<std::size_t>(result.l1_hits) : std::nullopt,
        .l1_misses = show_extended_stats ? std::optional<std::size_t>(result.l1_misses) : std::nullopt,
        .l2_hits = (*l2_cache_mode != nexus::sim::memory::CacheMode::Off)
            ? std::optional<std::size_t>(result.l2_hits)
            : std::nullopt,
        .l2_misses = (*l2_cache_mode != nexus::sim::memory::CacheMode::Off)
            ? std::optional<std::size_t>(result.l2_misses)
            : std::nullopt,
        .l2_miss_rate = (*l2_cache_mode != nexus::sim::memory::CacheMode::Off)
            ? std::optional<double>(l2_miss_rate)
            : std::nullopt,
        .io_reads = show_extended_stats ? std::optional<std::size_t>(result.io_reads) : std::nullopt,
        .io_writes = show_extended_stats ? std::optional<std::size_t>(result.io_writes) : std::nullopt,
        .interrupts_handled = show_extended_stats ? std::optional<std::size_t>(result.interrupts_handled)
                                                  : std::nullopt,
        .dma_words_copied = show_extended_stats ? std::optional<std::size_t>(result.dma_words_copied)
                                                : std::nullopt,
        .coherence_events = std::nullopt,
        .invalidations = std::nullopt,
        .directory_lookups = std::nullopt,
        .synchronization_events = std::nullopt,
        .lock_acquisitions = std::nullopt,
        .lock_contentions = std::nullopt,
        .barrier_wait_cycles = std::nullopt,
        .atomic_operations = std::nullopt,
        .store_buffer_flushes = std::nullopt,
        .interconnect_messages = std::nullopt,
        .interconnect_cycles = std::nullopt,
    });
  }

  if (mode == "advanced") {
    const auto predictor = parse_advanced_predictor(predictor_name.value_or("static-not-taken"));
    if (!predictor.has_value()) {
      std::cerr << "error: unsupported advanced predictor '" << predictor_name.value_or("?")
                << "', expected 'static-not-taken' or '2bit'\n";
      return 1;
    }

    const auto scheduler = parse_advanced_scheduler(
        vliw ? std::string_view("vliw-lite") : std::string_view(scheduler_name.value_or("inorder")));
    if (!scheduler.has_value()) {
      std::cerr << "error: unsupported advanced scheduler '" << scheduler_name.value_or("?")
                << "', expected 'inorder', 'vliw-lite', 'scoreboard', or 'tomasulo'\n";
      return 1;
    }

    std::optional<nexus::mips::loader::LoadedProgram> smt_program;
    if (smt_file.has_value()) {
      auto smt_parsed = nexus::mips::loader::load_program_from_file(*smt_file);
      if (!smt_parsed.diagnostics.empty()) {
        return print_loader_diagnostics(smt_parsed.diagnostics);
      }
      smt_program = std::move(*smt_parsed.program);
    }

    nexus::sim::advanced::RunOptions advanced_options{
        .trace = trace,
        .predictor = *predictor,
        .scheduler = *scheduler,
        .issue_width = vliw ? 2U : issue_width,
        .memory_words = 1U << 18,
    };
    // A D-stage pipeline resolves branches D-2 stages after fetch: the 5-stage default costs 2 cycles.
    advanced_options.mispredict_penalty = pipeline_depth.has_value() ? *pipeline_depth - 3U : 2U;
    advanced_options.rob_entries = rob_entries.value_or(advanced_options.rob_entries);
    advanced_options.reservation_stations = reservation_stations.value_or(advanced_options.reservation_stations);
    advanced_options.cdb_width = cdb_width.value_or(advanced_options.cdb_width);
    advanced_options.smt_program = smt_program.has_value() ? &*smt_program : nullptr;
    const auto result = nexus::sim::advanced::run_program(*parsed.program, advanced_options);
    if (!result.success) {
      std::cerr << "error: " << result.error << '\n';
      return 1;
    }

    for (const auto& line : result.trace_lines) {
      std::cout << line << '\n';
    }

    const double cpi = result.executed_instructions == 0
        ? 0.0
        : static_cast<double>(result.cycles) / static_cast<double>(result.executed_instructions);
    const double ipc = result.cycles == 0
        ? 0.0
        : static_cast<double>(result.executed_instructions) / static_cast<double>(result.cycles);
    const double utilization = (result.cycles == 0 || result.issued_slots == 0)
        ? 0.0
        : static_cast<double>(result.issued_slots) /
            static_cast<double>(result.cycles * (vliw ? 2U : issue_width));
    print_summary({
        .mode = "advanced",
        .cores = std::nullopt,
        .control = std::nullopt,
        .scheduler = std::string(nexus::sim::advanced::scheduler_name(*scheduler)),
        .predictor = std::string(nexus::sim::advanced::predictor_name(*predictor)),
        .cache = std::nullopt,
        .coherence = std::nullopt,
        .consistency = std::nullopt,
        .interconnect = std::nullopt,
        .issue_width = vliw ? std::optional<std::size_t>(2U) : std::optional<std::size_t>(issue_width),
        .exit_code = result.exit_code,
        .instructions = result.executed_instructions,
        .cycles = result.cycles,
        .cpi = cpi,
        .ipc = ipc,
        .stalls = std::nullopt,
        .load_use_stalls = std::nullopt,
        .flushes = std::nullopt,
        .forwardings = std::nullopt,
        .branch_predictions = result.branch_predictions,
        .branch_mispredictions = result.branch_mispredictions,
        .speculative_flush_cycles = result.speculative_flush_cycles,
        .slot_utilization = utilization,
        .memory_accesses = std::nullopt,
        .memory_reads = std::nullopt,
        .memory_writes = std::nullopt,
        .cache_hits = std::nullopt,
        .cache_misses = std::nullopt,
        .cache_miss_rate = std::nullopt,
        .l1_hits = std::nullopt,
        .l1_misses = std::nullopt,
        .l2_hits = std::nullopt,
        .l2_misses = std::nullopt,
        .l2_miss_rate = std::nullopt,
        .io_reads = std::nullopt,
        .io_writes = std::nullopt,
        .interrupts_handled = std::nullopt,
        .dma_words_copied = std::nullopt,
        .coherence_events = std::nullopt,
        .invalidations = std::nullopt,
        .directory_lookups = std::nullopt,
        .synchronization_events = std::nullopt,
        .lock_acquisitions = std::nullopt,
        .lock_contentions = std::nullopt,
        .barrier_wait_cycles = std::nullopt,
        .atomic_operations = std::nullopt,
        .store_buffer_flushes = std::nullopt,
        .interconnect_messages = std::nullopt,
        .interconnect_cycles = std::nullopt,
    });
    if (*scheduler == nexus::sim::advanced::SchedulerKind::Tomasulo) {
      const double occupancy = result.cycles == 0
          ? 0.0
          : static_cast<double>(result.rob_occupancy_sum) / static_cast<double>(result.cycles);
      std::cout << "ROB entries: " << advanced_options.rob_entries
                << "\nReservation stations per class: " << advanced_options.reservation_stations
                << "\nCDB width: " << advanced_options.cdb_width
                << "\nMispredict penalty: " << advanced_options.mispredict_penalty
                << "\nAverage ROB occupancy: " << occupancy
                << "\nMax ROB occupancy: " << result.max_rob_occupancy
                << "\nROB-full stalls: " << result.rob_full_stalls
                << "\nReservation-station stalls: " << result.reservation_station_stalls
                << "\nStructural stalls: " << result.structural_stalls
                << "\nCDB wait (entry-cycles): " << result.cdb_conflicts
                << "\nStore-to-load forwards: " << result.load_forwards
                << "\nReturn predictions: " << result.return_predictions
                << "\nReturn mispredictions: " << result.return_mispredictions
                << "\nWrong-path fetch cycles: " << result.wrong_path_cycles << '\n';
      for (std::size_t thread = 0; thread < result.thread_instructions.size() && smt_program.has_value(); ++thread) {
        std::cout << "Thread " << thread << ": instructions=" << result.thread_instructions[thread]
                  << " exit_code=" << result.thread_exit_codes[thread] << '\n';
      }
    }
    return 0;
  }

  if (mode == "parallel") {
    const auto coherence = parse_parallel_coherence(coherence_name.value_or("snoop"));
    if (!coherence.has_value()) {
      std::cerr << "error: unsupported coherence mode '" << coherence_name.value_or("?")
                << "', expected 'snoop' or 'directory-lite'\n";
      return 1;
    }
    const auto consistency = parse_parallel_consistency(consistency_name.value_or("sc"));
    if (!consistency.has_value()) {
      std::cerr << "error: unsupported consistency model '" << consistency_name.value_or("?")
                << "', expected 'sc' or 'weak-lite'\n";
      return 1;
    }
    const auto interconnect = parse_parallel_interconnect(interconnect_name.value_or("bus"));
    if (!interconnect.has_value()) {
      std::cerr << "error: unsupported interconnect '" << interconnect_name.value_or("?")
                << "', expected 'bus', 'switch', or 'noc-lite'\n";
      return 1;
    }

    const auto result = nexus::sim::parallel::run_program(
        *parsed.program,
        {
            .trace = trace,
            .cores = cores,
            .memory_words = 1U << 18,
            .memory_latency = memory_latency,
            .coherence = *coherence,
            .consistency = *consistency,
            .interconnect = *interconnect,
        });
    if (!result.success) {
      std::cerr << "error: " << result.error << '\n';
      return 1;
    }

    for (const auto& line : result.trace_lines) {
      std::cout << line << '\n';
    }
    for (const auto& line : result.system_lines) {
      std::cout << line << '\n';
    }

    const double cpi = result.retired_instructions == 0
        ? 0.0
        : static_cast<double>(result.cycles) / static_cast<double>(result.retired_instructions);
    const double ipc = result.cycles == 0
        ? 0.0
        : static_cast<double>(result.retired_instructions) / static_cast<double>(result.cycles);
    const double miss_rate = (result.cache_hits + result.cache_misses) == 0
        ? 0.0
        : static_cast<double>(result.cache_misses) /
            static_cast<double>(result.cache_hits + result.cache_misses);
    return print_summary({
        .mode = "parallel",
        .cores = result.active_cores,
        .control = std::nullopt,
        .scheduler = std::nullopt,
        .predictor = std::nullopt,
        .cache = std::string("private coherent-lite caches"),
        .coherence = std::string(nexus::sim::parallel::coherence_name(*coherence)),
        .consistency = std::string(nexus::sim::parallel::consistency_name(*consistency)),
        .interconnect = std::string(nexus::sim::parallel::interconnect_name(*interconnect)),
        .issue_width = std::nullopt,
        .exit_code = result.exit_code,
        .instructions = result.retired_instructions,
        .cycles = result.cycles,
        .cpi = cpi,
        .ipc = stats ? std::optional<double>(ipc) : std::nullopt,
        .stalls = std::nullopt,
        .load_use_stalls = std::nullopt,
        .flushes = std::nullopt,
        .forwardings = std::nullopt,
        .branch_predictions = std::nullopt,
        .branch_mispredictions = std::nullopt,
        .speculative_flush_cycles = std::nullopt,
        .slot_utilization = std::nullopt,
        .memory_accesses = result.memory_accesses,
        .memory_reads = result.memory_reads,
        .memory_writes = result.memory_writes,
        .cache_hits = result.cache_hits,
        .cache_misses = result.cache_misses,
        .cache_miss_rate = std::optional<double>(miss_rate),
        .l1_hits = std::nullopt,
        .l1_misses = std::nullopt,
        .l2_hits = std::nullopt,
        .l2_misses = std::nullopt,
        .l2_miss_rate = std::nullopt,
        .io_reads = std::nullopt,
        .io_writes = std::nullopt,
        .interrupts_handled = std::nullopt,
        .dma_words_copied = std::nullopt,
        .coherence_events = result.coherence_events,
        .invalidations = result.invalidations,
        .directory_lookups = *coherence == nexus::sim::parallel::CoherenceKind::DirectoryLite
            ? std::optional<std::size_t>(result.directory_lookups)
            : std::nullopt,
        .synchronization_events = result.synchronization_events,
        .lock_acquisitions = result.lock_acquisitions,
        .lock_contentions = result.lock_contentions,
        .barrier_wait_cycles = result.barrier_wait_cycles,
        .atomic_operations = result.atomic_operations,
        .store_buffer_flushes = *consistency == nexus::sim::parallel::ConsistencyKind::WeakLite
            ? std::optional<std::size_t>(result.store_buffer_flushes)
            : std::nullopt,
        .interconnect_messages = result.interconnect_messages,
        .interconnect_cycles = result.interconnect_cycles,
    });
  }

  const auto control = parse_control_style(control_name.value_or("hardwired"));
  if (!control.has_value()) {
    std::cerr << "error: unsupported control style '" << control_name.value_or("?")
              << "', expected 'hardwired' or 'microcode'\n";
    return 1;
  }

  const auto result = nexus::sim::multi_cycle::run_program(*parsed.program, {.trace = trace, .control = *control});
  if (!result.success) {
    std::cerr << "error: " << result.error << '\n';
    return 1;
  }

  for (const auto& line : result.trace_lines) {
    std::cout << line << '\n';
  }
  return print_summary({
      .mode = "multi-cycle",
      .cores = std::nullopt,
      .control = std::string(nexus::sim::multi_cycle::format_control_style(*control)),
      .scheduler = std::nullopt,
      .predictor = std::nullopt,
      .cache = std::nullopt,
      .coherence = std::nullopt,
      .consistency = std::nullopt,
      .interconnect = std::nullopt,
      .issue_width = std::nullopt,
      .exit_code = result.exit_code,
      .instructions = result.executed_instructions,
      .cycles = result.cycles,
      .cpi = std::nullopt,
      .ipc = std::nullopt,
      .stalls = std::nullopt,
      .load_use_stalls = std::nullopt,
      .flushes = std::nullopt,
      .forwardings = std::nullopt,
      .branch_predictions = std::nullopt,
      .branch_mispredictions = std::nullopt,
      .speculative_flush_cycles = std::nullopt,
      .slot_utilization = std::nullopt,
      .memory_accesses = std::nullopt,
      .memory_reads = std::nullopt,
      .memory_writes = std::nullopt,
      .cache_hits = std::nullopt,
      .cache_misses = std::nullopt,
      .cache_miss_rate = std::nullopt,
      .l1_hits = std::nullopt,
      .l1_misses = std::nullopt,
      .l2_hits = std::nullopt,
      .l2_misses = std::nullopt,
      .l2_miss_rate = std::nullopt,
      .io_reads = std::nullopt,
      .io_writes = std::nullopt,
      .interrupts_handled = std::nullopt,
      .dma_words_copied = std::nullopt,
      .coherence_events = std::nullopt,
      .invalidations = std::nullopt,
      .directory_lookups = std::nullopt,
      .synchronization_events = std::nullopt,
      .lock_acquisitions = std::nullopt,
      .lock_contentions = std::nullopt,
      .barrier_wait_cycles = std::nullopt,
      .atomic_operations = std::nullopt,
      .store_buffer_flushes = std::nullopt,
      .interconnect_messages = std::nullopt,
      .interconnect_cycles = std::nullopt,
  });
}
