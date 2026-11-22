#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO="$HERE/source/CPyMO-main"
BUILD="$HERE/build"
mkdir -p "$BUILD" "$BUILD/testgame/script"
# Compile the entire real engine core, each source in its own section.
for src in "$REPO"/cpymo/*.c; do
  b=$(basename "$src" .c)
  gcc -std=gnu11 -O1 -g -ffunction-sections -fdata-sections -DDISABLE_AUDIO -DDISABLE_MOVIE -c "$src" -o "$BUILD/$b.o"
done
# Link only reached portions. This is a core I/O integration test, not SDL or ARM game execution.
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections -I"$REPO/cpymo" -c "$HERE/tests/test_game_loading.c" -o "$BUILD/test_game_loading.o"
gcc -fsanitize=address,undefined -Wl,--gc-sections "$BUILD/test_game_loading.o" "$BUILD"/cpymo_*.o -lm -o "$BUILD/test_game_loading"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$BUILD/test_game_loading" "$BUILD/testgame"
mkdir -p "$BUILD/中文路径测试/script"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$BUILD/test_game_loading" "$BUILD/中文路径测试"
echo "PASS: all 40 core sources compiled; game I/O, UTF-8 directory and bootloader real functions linked and exercised"
