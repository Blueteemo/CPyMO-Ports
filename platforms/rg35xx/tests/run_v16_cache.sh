#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
OUT="$ROOT/build-font-cache"
mkdir -p "$OUT"
CC="${CC:-cc}"
"$CC" -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DCPYMO_RG35XX_STOCK -I"$ROOT/tests/sdl_stub" \
  -I"$ROOT/source/CPyMO-main/cpymo-backends/sdl1" \
  "$ROOT/tests/test_font_cache.c" \
  "$ROOT/source/CPyMO-main/cpymo-backends/sdl1/rg35xx_ttf_cache.c" \
  -o "$OUT/test_font_cache"
"$OUT/test_font_cache"
