#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/hdl"
mkdir -p "${BUILD_DIR}"

require_tool() {
  local tool="$1"
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "missing required HDL tool: ${tool}" >&2
    exit 1
  fi
}

run_tb() {
  local name="$1"
  shift
  local output="${BUILD_DIR}/${name}.vvp"
  iverilog -g2012 -o "${output}" "$@"
  vvp "${output}"
}

require_tool iverilog
require_tool vvp

target="${1:-all}"

case "${target}" in
  adder)
    run_tb adder \
      "${ROOT_DIR}/src/hdl/alu/nexus_adder.v" \
      "${ROOT_DIR}/src/hdl/alu/adder_tb.v"
    ;;
  alu)
    run_tb alu \
      "${ROOT_DIR}/src/hdl/alu/nexus_adder.v" \
      "${ROOT_DIR}/src/hdl/alu/nexus_alu.v" \
      "${ROOT_DIR}/src/hdl/alu/alu_tb.v"
    ;;
  multiplier)
    run_tb multiplier \
      "${ROOT_DIR}/src/hdl/alu/nexus_iterative_multiplier.v" \
      "${ROOT_DIR}/src/hdl/alu/iterative_multiplier_tb.v"
    ;;
  divider)
    run_tb divider \
      "${ROOT_DIR}/src/hdl/alu/nexus_iterative_divider.v" \
      "${ROOT_DIR}/src/hdl/alu/iterative_divider_tb.v"
    ;;
  register-file)
    run_tb register_file \
      "${ROOT_DIR}/src/hdl/register_file/nexus_register_file.v" \
      "${ROOT_DIR}/src/hdl/register_file/register_file_tb.v"
    ;;
  control)
    run_tb control \
      "${ROOT_DIR}/src/hdl/control/nexus_control_unit.v" \
      "${ROOT_DIR}/src/hdl/control/control_unit_tb.v"
    ;;
  pipeline-reg)
    run_tb pipeline_reg \
      "${ROOT_DIR}/src/hdl/pipeline_regs/nexus_pipeline_reg.v" \
      "${ROOT_DIR}/src/hdl/pipeline_regs/pipeline_reg_tb.v"
    ;;
  cpu-slice)
    run_tb cpu_slice \
      "${ROOT_DIR}/src/hdl/alu/nexus_adder.v" \
      "${ROOT_DIR}/src/hdl/alu/nexus_alu.v" \
      "${ROOT_DIR}/src/hdl/control/nexus_control_unit.v" \
      "${ROOT_DIR}/src/hdl/register_file/nexus_register_file.v" \
      "${ROOT_DIR}/src/hdl/pipeline_regs/nexus_pipeline_reg.v" \
      "${ROOT_DIR}/src/hdl/cpu_slice/nexus_cpu_slice.v" \
      "${ROOT_DIR}/src/hdl/cpu_slice/cpu_slice_tb.v"
    ;;
  all)
    for suite in adder alu multiplier divider register-file control pipeline-reg cpu-slice; do
      "${BASH_SOURCE[0]}" "${suite}"
    done
    ;;
  *)
    echo "usage: $0 {all|adder|alu|multiplier|divider|register-file|control|pipeline-reg|cpu-slice}" >&2
    exit 1
    ;;
esac
