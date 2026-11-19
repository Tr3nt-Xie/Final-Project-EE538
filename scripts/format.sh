#!/usr/bin/env bash
# Formats all C++ sources in place (or checks them with --check).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
FILES=$(find include src tests benchmarks -name '*.cpp' -o -name '*.hpp')
if [[ "${1:-}" == "--check" ]]; then
  clang-format --dry-run --Werror $FILES
else
  clang-format -i $FILES
fi
