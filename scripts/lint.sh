#!/usr/bin/env bash
# Runs clang-tidy using the compile database from a configured build directory.
#   scripts/lint.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
[[ -f "$BUILD/compile_commands.json" ]] || { echo "configure first: cmake -S . -B $BUILD"; exit 1; }
cd "$ROOT"
find src -name '*.cpp' | xargs clang-tidy -p "$BUILD"
