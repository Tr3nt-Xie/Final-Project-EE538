#!/usr/bin/env bash
# Lists the TODO markers assigned to one member (or all), with file:line.
#   scripts/todos.sh            # everything
#   scripts/todos.sh 2          # TODO(Member 2) only
#   scripts/todos.sh disabled   # DISABLED_ tests still pending
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
case "${1:-all}" in
  1|2|3) grep -rn "TODO(Member $1" include src tests examples benchmarks ;;
  disabled) grep -rn "DISABLED_" tests | sed 's/TEST(\(.*\), DISABLED_\(.*\)) {/\1.\2/' ;;
  all) grep -rn "TODO(Member" include src tests examples benchmarks ;;
  *) echo "usage: $0 [1|2|3|disabled]"; exit 64 ;;
esac
