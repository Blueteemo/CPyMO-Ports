#!/usr/bin/env bash
# Build a real ARM/uClibc Libretro core only with a real target SDK.
# NO host-ABI shortcuts, no fake SDL headers, no glibc binaries.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-/opt/miyoo/bin/arm-miyoo-linux-uclibcgnueabi-gcc}"
if ! command -v "$CC" >/dev/null 2>&1; then
  echo "BLOCKED: missing ARM/uClibc compiler: $CC" >&2
  echo 'Get an actual RG35XX/Miyoo-compatible uClibc SDK before target compilation.' >&2
  exit 3
fi
triple="$($CC -dumpmachine)"
case "$triple" in
  arm*-linux-uclibc*) ;;
  *) echo "REFUSED: wrong toolchain target $triple (requires ARM/uClibc)" >&2; exit 4 ;;
esac
sysroot="$($CC -print-sysroot)"
if [[ -z "$sysroot" || ! -d "$sysroot" ]]; then
  echo 'REFUSED: no usable target sysroot' >&2
  exit 5
fi
if ! find "$sysroot" -name 'ld-uClibc.so*' -print -quit | grep -q . ; then
  echo 'REFUSED: uClibc target loader not present in SDK sysroot' >&2
  exit 6
fi
command -v cmake >/dev/null || { echo 'cmake missing' >&2; exit 7; }
: "${BUILD:=$ROOT/build-garlicos-uclibc}"
cmake -S "$ROOT/source/CPyMO-main/cpymo-backends/libretro" -B "$BUILD" \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_C_FLAGS='-O2 -DNDEBUG -fPIC -mcpu=cortex-a9 -mfloat-abi=soft' \
  -DCMAKE_BUILD_TYPE=Release -DGARLIC_BOOTSTRAP_NO_AUDIO=ON
cmake --build "$BUILD" --parallel 2
python3 "$ROOT/tools/verify_uclibc_core.py" "$BUILD/cpymo_libretro.so"
echo "TARGET CORE BUILT (UNTESTED ON HARDWARE): $BUILD/cpymo_libretro.so"
