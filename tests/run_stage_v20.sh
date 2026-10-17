#!/usr/bin/env bash
# Full host smoke and preflight. Cross SDK optional if cross-building separately.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build-stage-v20}"
mkdir -p "$BUILD"
BUILD="$BUILD/host" bash "$ROOT/tests/run_host_garlicos.sh" > "$BUILD/host.log" 2>&1
CORE="$BUILD/host/cmake/cpymo_libretro.so"
gcc -std=gnu11 -Wall -Wextra -Werror \
  -I"$ROOT/source/CPyMO-main/cpymo-backends/libretro" \
  "$ROOT/tests/test_host_retro_api_no_frame.c" -ldl -o "$BUILD/test_no_frame"
python3 "$ROOT/tests/build_garlic_fixture.py" "$BUILD/timing" --timing >/dev/null
"$BUILD/test_no_frame" "$CORE" "$BUILD/timing/gameconfig.txt" "$BUILD/host/no-font/gameconfig.txt" > "$BUILD/timing.log" 2>&1
grep -q 'PASS: no-frame-callback' "$BUILD/timing.log"
gcc -std=gnu11 -Wall -Wextra -Werror \
  -I"$ROOT/source/CPyMO-main/cpymo-backends/libretro" \
  "$ROOT/tests/test_garlic_smoke_host.c" -ldl -o "$BUILD/test_smoke"
GAME_SRC="$ROOT/deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test"
GAME="$BUILD/smoke_fixture_copy"
rm -rf "$GAME"
cp -a "$GAME_SRC" "$GAME"
"$BUILD/test_smoke" "$CORE" "$GAME/gameconfig.txt" > "$BUILD/smoke.log" 2>&1
grep -q 'PASS: entire test game' "$BUILD/smoke.log"
python3 "$ROOT/tools/verify_uclibc_core.py" "$ROOT/target/cpymo_libretro.so" > "$BUILD/arm_verify.log"
sha256sum "$ROOT/target/cpymo_libretro.so" "$GAME/cpymo_libretro.so" | awk '{print $1}' | uniq -c | grep -q '^ *2 '
sh -n "$ROOT/deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test.sh"
# Launch script test exercises missing uClibc-loader guard, no host system changes.
( cd "$ROOT/deploy/Roms/APPS" && \
 CPYMO_RETROARCH_BIN=/bin/true CPYMO_RETROARCH_HOME=/tmp \
  sh ./CPyMO_RG35XX_uClibc_Test.sh ) >/dev/null 2>&1 || test "$?" -eq 43
LOG="$ROOT/deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test.log"
grep -q 'stage=refused_missing_uclibc_loader' "$LOG"
rm -f "$LOG"
echo 'PASS v0.20: host full core, timer fallback, real smoke fixture 328+ frames, ARM/uClibc ELF validation, launch failure logging.'
