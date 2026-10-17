#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build-garlicos-host}"
mkdir -p "$BUILD"
echo '[1/3] Compile original GarlicOS RGB565 physical button mapping tests'
gcc -O2 -Wall -Wextra -Werror -std=c11 -o "$BUILD/test_adapter" \
  "$ROOT/tests/test_garlic_adapter.c" "$ROOT/source/CPyMO-main/cpymo-backends/garlicos/garlic_adapter.c"
"$BUILD/test_adapter"
echo '[2/3] Build complete x86_64 host-only Libretro CPyMO .so'
CFLAGS="${CFLAGS:--O1 -g}"
cmake -S "$ROOT/source/CPyMO-main/cpymo-backends/libretro" -B "$BUILD/cmake" \
  -DGARLIC_BOOTSTRAP_NO_AUDIO=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="$CFLAGS"
cmake --build "$BUILD/cmake" --parallel 2
if [[ "${SANITIZE:-0}" == 1 ]]; then
  echo 'NOTE: To run with sanitizers, invoke a fresh build with SANITIZE=1 and use ASan-enabled host harness.'
fi
echo '[3/3] Full Libretro frontend ABI test: frame, input, font-failure rollback, reload'
python3 "$ROOT/tests/build_garlic_fixture.py" "$BUILD/game"
python3 "$ROOT/tests/build_garlic_fixture.py" "$BUILD/no-font" --bad-font
gcc -std=gnu11 -Wall -Wextra -Werror \
  -I"$ROOT/source/CPyMO-main/cpymo-backends/libretro" \
  "$ROOT/tests/test_host_retro_api.c" -ldl -o "$BUILD/test_host_retro_api"
(cd "$BUILD" && ./test_host_retro_api "$BUILD/cmake/cpymo_libretro.so" "$BUILD/game/gameconfig.txt" "$BUILD/no-font/gameconfig.txt")
echo 'PASS: host-only native Libretro core and failure recovery. NOT ARM/uClibc hardware verification.'
