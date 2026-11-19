#!/usr/bin/env bash
# Build and test inside the Linux dev container (for macOS/Windows developers).
#   scripts/docker-dev.sh                 # configure + build + test (Debug)
#   scripts/docker-dev.sh asan            # use a CMake preset name
#   scripts/docker-dev.sh shell           # interactive shell in the container
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE=aegisproxy-dev
docker build -q -t "$IMAGE" -f "$ROOT/scripts/docker/Dockerfile" "$ROOT/scripts/docker" >/dev/null

if [[ "${1:-}" == "shell" ]]; then
  exec docker run --rm -it -v "$ROOT:/work" -w /work "$IMAGE" bash
fi

PRESET="${1:-debug}"
exec docker run --rm -v "$ROOT:/work" -w /work "$IMAGE" bash -c "
  set -euo pipefail
  cmake --preset $PRESET -G Ninja -B build/docker-$PRESET
  cmake --build build/docker-$PRESET -j
  ctest --test-dir build/docker-$PRESET --output-on-failure
"
