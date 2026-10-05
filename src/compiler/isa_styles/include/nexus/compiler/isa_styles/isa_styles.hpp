#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::isa_styles {

// Three classic instruction-set styles that NexusLang programs can be compiled to and executed on,
// for comparison with the MIPS load/store back end:
//
//   Stack          JVM-bytecode-like operand-stack machine (iload/istore, iadd, if_icmpXX, invokestatic,
//                  iaload/iastore).  Zero-address arithmetic; comparisons use the javac branch pattern.
//   Accumulator    One-address machine with a single accumulator (LOAD/STORE/ADD m, JNZ, CALL);
//                  arguments go through a parameter area.
//   RegisterMemory IA-32-like two-address register-memory machine (mov/add/imul/idiv/cmp/setcc with
//                  [ebp+disp] and [base+index*4] operands, push/call/ret, cdecl frames).
//
// Every style has its own emitter and interpreter, so the comparison reports real static code size
// (instructions and encoded bytes) and real dynamic counts (instructions, data-memory traffic).
enum class IsaStyle {
  Stack,
  Accumulator,
  RegisterMemory,
};

std::string_view isa_style_name(IsaStyle style);

struct StyleRun {
  bool success = false;
  std::string error;
  std::int32_t exit_code = 0;
  std::size_t dynamic_instructions = 0;
  std::size_t memory_reads = 0;
  std::size_t memory_writes = 0;
  std::size_t max_operand_stack = 0;  // stack style only
};

struct StyleProgram {
  IsaStyle style = IsaStyle::Stack;
  std::string listing;
  std::size_t static_instructions = 0;
  std::size_t code_bytes = 0;
};

struct StyleResult {
  StyleProgram program;
  StyleRun run;
};

// Compiles `module` (which must contain `main`) to `style` and runs it.
StyleResult compile_and_run(const ir::Module& module, IsaStyle style);

}  // namespace nexus::compiler::isa_styles
