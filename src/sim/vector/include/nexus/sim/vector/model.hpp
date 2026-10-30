#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nexus::sim::vector {

// A VMIPS-style vector processor (NEY704 (h): vector computers, from pipelined vector units to SIMD extensions,
// and programming them).
//
// Program text:
//   .param n 100                 a named constant (can be overridden)
//   .array X n ramp              X[i] = i + 1      (also: const <v>, zero)
//   .scalar F0 3                 a scalar floating-point register
//   LA R1, X        LI R3, n     SETVL R3 (VL = min(R3, MVL))
//   LV V1, R1       LVWS V1, R1, R2 (stride R2)       SV V1, R1       SVWS V1, R1, R2
//   ADDVV/SUBVV/MULVV/DIVVV Vd, Va, Vb               ADDVS/SUBVS/MULVS/DIVVS Vd, Va, Fs
//   ADDVL R1 (R1 += VL)  SUBVL R3 (R3 -= VL)  ADDI Rd, Rs, imm  BNEZ R3, label  HALT
// Timing (one vector instruction issued per cycle, in program order):
//   * functional units: load/store (start-up 12), add (6), multiply (7), divide (20) — Hennessy & Patterson;
//   * a unit accepts a new instruction when the previous one has sent its last element (ceil(VL / lanes) cycles);
//   * chaining: a dependent instruction may start when the first result element is available
//     (producer start + start-up); without chaining it waits for the producer's last element.
// The convoy/chime estimate of the textbook is computed alongside the cycle-level schedule.

struct Options {
  std::size_t mvl = 64;
  std::size_t lanes = 1;
  bool chaining = true;
  std::map<std::string, std::int64_t> params;  // overrides of .param
  std::size_t max_instructions = 1'000'000;
  bool trace = false;
};

struct Result {
  bool success = false;
  std::string error;
  std::size_t cycles = 0;
  std::size_t vector_instructions = 0;
  std::size_t scalar_instructions = 0;
  std::size_t elements = 0;            // vector element operations
  std::size_t strips = 0;              // SETVL executions
  std::size_t convoys = 0;             // textbook convoys over the whole run
  std::size_t chimes_estimate = 0;     // sum over strips of convoys x VL (the "chime" time model)
  std::map<std::string, std::vector<double>> arrays;  // final memory contents per array
  std::map<std::string, double> scalars;
  std::vector<std::string> trace_lines;
};

Result run(const std::string& program_text, const Options& options = {});

// Reference: Y = a * X + Y on the host, for checking DAXPY programs.
std::vector<double> daxpy_reference(double a, const std::vector<double>& x, std::vector<double> y);

}  // namespace nexus::sim::vector
