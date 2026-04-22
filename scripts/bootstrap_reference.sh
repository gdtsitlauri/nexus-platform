#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
Nexus Phase 1 bootstrap reference

This repository assumes the following tools are already installed and working:
- CMake
- Ninja
- GCC or Clang with C++20 support
- Python 3
- Doxygen (optional for the docs target)

Phase 1 does not install dependencies.
Use scripts/configure.sh, scripts/build.sh, and scripts/test.sh to exercise the bootstrap flow.
EOF
