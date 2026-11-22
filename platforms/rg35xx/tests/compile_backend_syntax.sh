#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
REPO="$ROOT/source/CPyMO-main"
mkdir -p "$ROOT/build/backend-objects"
for s in "$REPO"/cpymo-backends/sdl1/*.c; do
 basename "$s"
 gcc -std=gnu11 -O1 -Wall -Werror=implicit-function-declaration -Wno-unused-function -Wno-unused-variable \
  -DCPYMO_RG35XX_STOCK -DENABLE_SDL_TTF=2 -DDISABLE_AUDIO -DDISABLE_MOVIE \
  -DDISABLE_FFMPEG_AUDIO -DDISABLE_FFMPEG_MOVIE \
  -I"$ROOT/tests/sdl_stub" -I"$REPO/cpymo" -c "$s" -o "$ROOT/build/backend-objects/$(basename "$s" .c).o"
done
