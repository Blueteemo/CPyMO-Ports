#!/usr/bin/env bash
set -euo pipefail
platform="$(cd -- "$(dirname -- "$0")/.." && pwd)"
src="$platform/source/CPyMO-main"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -ffunction-sections -fdata-sections \
    -DDISABLE_FFMPEG_AUDIO -DGARLIC_NO_FFMPEG -DGARLIC_MP3_AUDIO \
    -I"$src/cpymo" -I"$src/cpymo-backends/libretro" \
    "$platform/tests/test_native_movie_volume.c" -Wl,--gc-sections \
    -o "$out/test"
"$out/test"
