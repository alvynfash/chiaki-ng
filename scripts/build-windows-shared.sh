#!/usr/bin/env bash

set -euo pipefail

build_dir="${1:-build-windows-shared}"
build_type="${BUILD_TYPE:-Release}"

cmake -S . -B "$build_dir" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE="$build_type" \
  -DBUILD_SHARED_LIBS=ON \
  -DCHIAKI_ENABLE_GUI=OFF \
  -DCHIAKI_ENABLE_CLI=OFF \
  -DCHIAKI_ENABLE_TESTS=OFF \
  -DCHIAKI_ENABLE_BOREALIS=OFF \
  -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF \
  -DCHIAKI_ENABLE_SETSU=OFF \
  -DCHIAKI_ENABLE_FFMPEG_DECODER=ON

cmake --build "$build_dir" --config "$build_type"

echo "Built Windows shared Chiaki artifact:"
echo "  build_dir=$build_dir"
echo "  expected_dll=$build_dir/lib/chiaki.dll"
