#!/usr/bin/env bash
# Starts two example backends and the proxy with the example config. Ctrl-C stops all.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${AEGIS_BIN:-$ROOT/build/aegisproxy}"
[[ -x "$BIN" ]] || { echo "build first: cmake -S . -B build && cmake --build build -j"; exit 1; }

python3 "$ROOT/examples/backend_server/simple_backend.py" --port 9001 --name app-1 &
python3 "$ROOT/examples/backend_server/simple_backend.py" --port 9002 --name app-2 &
trap 'kill $(jobs -p) 2>/dev/null || true' EXIT
sleep 0.5
"$BIN" --config "$ROOT/config/aegisproxy.example.conf" --log-level debug
