#!/usr/bin/env bash
#
# Build and run the tests under ThreadSanitizer to verify the control/audio
# split (lock-free event ring + double-buffered params) for data races.
#
# Owns the `build-tsan/` directory (separate from `build/`, owned by build.sh).
# TSAN's runtime rejects the kernel's default ASLR layout, so the tests run
# under `setarch -R`; if that is unavailable (unprivileged container), run this
# on the host instead.
set -euo pipefail
cd "$(dirname "$0")"

git submodule update --init

BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
cmake -S . -B build-tsan -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DTWANG_ENABLE_TSAN=ON

cmake --build build-tsan \
    --target test_split test_ring test_param_block test_engine test_params

setarch "$(uname -m)" -R ctest --test-dir build-tsan --output-on-failure
