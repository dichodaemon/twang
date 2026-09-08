#!/usr/bin/env bash
#
# Build the multitimbral hybrid synth on the host.
#
# Owns the `build/` directory. The dev container builds to /tmp/twang-build
# for compile checks only, so this cache stays host-consistent. If you ever
# hit "CMakeCache.txt ... different" (a stale cache from another environment),
# `rm -rf build` and re-run.
set -euo pipefail
cd "$(dirname "$0")"

git submodule update --init

BUILD_TYPE="${BUILD_TYPE:-Release}"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build
ctest --test-dir build --output-on-failure

echo
echo "Built ($BUILD_TYPE). Run:"
echo "  ./build/live_render              # stream audio (Enter to stop)"
echo "  ./build/wav_render 1 out.wav     # render a note to a WAV file"
echo "  ./build/sim                      # LVGL simulator (needs a display)"
echo "  ./build/bench 5                  # cycle harness: ns/sample/voice"
