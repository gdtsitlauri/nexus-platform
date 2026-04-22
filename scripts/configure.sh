#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build -G Ninja "$@"
