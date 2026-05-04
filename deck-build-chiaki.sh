# build-chiaki.sh (run on Steam Deck)
#!/usr/bin/env bash
set -euo pipefail

flatpak run --command=bash --devel io.github.streetpea.Chiaki4deck-devel -lc '
  set -euo pipefail
  cd ~/Desktop/Projects/chiaki-ng
  mkdir -p build-chiaki4deck
  cd build-chiaki4deck
  cmake -DCMAKE_BUILD_TYPE=Release ..
  cmake --build . -j"$(nproc)"
  ./gui/chiaki
'
