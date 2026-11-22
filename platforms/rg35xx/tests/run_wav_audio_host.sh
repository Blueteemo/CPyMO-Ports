#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/source/CPyMO-main/cpymo-backends/sdl1/rg35xx_audio_wav.c"
mkdir -p "$ROOT/build-audio-host"
gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror=implicit-function-declaration \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DCPYMO_RG35XX_WAV_AUDIO -DDISABLE_FFMPEG_AUDIO \
  -I"$ROOT/tests/audio_stub" "$SRC" "$ROOT/tests/test_wav_audio.c" \
  -lm -o "$ROOT/build-audio-host/test_wav_audio"
ASAN_OPTIONS=detect_leaks=1 "$ROOT/build-audio-host/test_wav_audio"
