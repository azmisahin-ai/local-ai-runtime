#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v cmake >/dev/null 2>&1; then
  echo "CMake 3.24+ is required. Install CMake and a C++20 compiler." >&2
  exit 1
fi
if [[ ! -f /usr/include/boost/beast.hpp && ! -f /usr/local/include/boost/beast.hpp ]]; then
  echo "Boost headers (1.74+) are missing. On Ubuntu/Debian run: sudo apt-get install build-essential cmake libboost-dev" >&2
  exit 1
fi

cmake -S . -B build -DLAR_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
