#!/usr/bin/env bash

set -euo pipefail

build_dir="${1:-build-windows-shared}"
build_type="${BUILD_TYPE:-Release}"
runtime_dir="$build_dir/runtime"

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

chiaki_dll="$build_dir/lib/libchiaki.dll"
if [[ ! -f "$chiaki_dll" ]]; then
	chiaki_dll="$build_dir/lib/chiaki.dll"
fi
if [[ ! -f "$chiaki_dll" ]]; then
  echo "ERROR: expected Windows shared artifact missing under $build_dir/lib" >&2
  exit 1
fi

mkdir -p "$runtime_dir"
cp -f "$chiaki_dll" "$runtime_dir/chiaki.dll"

# In an MSYS2/MinGW build, recursively stage non-system DLL dependencies so
# DeckStation can copy a self-contained native runtime rather than relying on
# the developer shell's PATH.
if command -v ldd >/dev/null 2>&1; then
  declare -A scanned=()
  queue=("$chiaki_dll")
  while [[ ${#queue[@]} -gt 0 ]]; do
    binary="${queue[0]}"
    queue=("${queue[@]:1}")
    [[ -z "${scanned[$binary]+x}" ]] || continue
    scanned["$binary"]=1
    while IFS= read -r dependency; do
      [[ -f "$dependency" ]] || continue
      case "${dependency,,}" in
        *'/windows/system32/'*|*'\\windows\\system32\\'*) continue ;;
      esac
      destination="$runtime_dir/$(basename "$dependency")"
      if [[ ! -f "$destination" ]]; then
        cp -f "$dependency" "$destination"
        queue+=("$dependency")
      fi
    done < <(ldd "$binary" 2>/dev/null | awk '/=>/ && $(NF-1) != "not" { print $(NF-1) } /^\// { print $1 }')
  done
else
  echo "WARNING: ldd unavailable; dependency closure was not staged" >&2
fi

echo "Built Windows shared Chiaki artifact:"
echo "  build_dir=$build_dir"
echo "  chiaki_dll=$chiaki_dll"
echo "  runtime_dir=$runtime_dir"
echo "Build DeckStation from its repository with:"
echo "  bash scripts/build_windows_bundle.sh ../chiaki-ng/$chiaki_dll release ../chiaki-ng/$runtime_dir"
