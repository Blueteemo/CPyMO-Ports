#!/usr/bin/env bash
# Tests for the game-selector bridge.
#
#   bash tests/run_menu_test.sh
#
# The subject is not "does cpymo_game_selector work" (the shipped, unmodified
# engine code) but "does THIS core drive it correctly": the menu appears with
# several games present, with exactly ONE game it still opens, with NONE it
# falls back to a plain load, a selection really switches game, the two sides
# of the selection do not leak fonts or saves, and the counters/log still make
# sense. All of it through dlopen of the real core and the real retro_* calls.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build-menu}"
FIX="$BUILD/apps"
FONT="${CPYMO_TEST_FONT:-$ROOT/deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test/system/default.ttf}"
export CPYMO_TEST_FONT="$FONT"
export CPYMO_DEBUG=1 # this suite explicitly asserts diagnostic content

mkdir -p "$BUILD"

echo "[1/4] the host core, from the current source"
cmake -S "$ROOT/source/CPyMO-main/cpymo-backends/libretro" -B "$BUILD/cmake" \
  -DCMAKE_BUILD_TYPE=Release -DGARLIC_BOOTSTRAP_NO_AUDIO=ON \
  -DCMAKE_C_FLAGS='-O2 -g' > "$BUILD/cmake.log" 2>&1
cmake --build "$BUILD/cmake" --parallel 4 > "$BUILD/build.log" 2>&1
CORE="$(find "$BUILD/cmake" -maxdepth 1 -name 'cpymo_libretro.*' -type f \
        ! -name '*.a' | sort | head -1)"
[ -n "$CORE" ] || { echo "no core built" >&2; tail -20 "$BUILD/build.log"; exit 1; }

echo "[2/4] fixtures: an APPS-like tree with 0, 1 and 3 games"
rm -rf "$FIX"
#   menu_none/ : no game at all, and no font: the empty-menu path.
#   menu_one/  : exactly one game: the historic auto-start convenience.
#   menu_3/    : three games, and a card-wide font next to them.
mkdir -p "$FIX/menu_none"
for n in menu_one menu_3; do mkdir -p "$FIX/$n"; done
python3 "$ROOT/tests/build_resolution_fixture.py" "$FIX/menu_one" --font "$FONT" \
  --system "$(dirname "$FONT")" > "$BUILD/one.log" 2>&1
# build_resolution_fixture makes Res320/Res540/Res800/CaseUpper. Two of them
# get game-like names, so the menu shows a plausible list, and the third and
# fourth stay realistic too ("Res800" is a game dir with a dot-free name).
mv "$FIX/menu_one/Res320" "$FIX/menu_one/Narcissus"
mv "$FIX/menu_one/Res540" "$FIX/menu_one/Yume"
mv "$FIX/menu_one/Res800" "$FIX/menu_one/Piano"
mkdir -p "$FIX/menu_3"
for g in Narcissus Yume Piano; do
  cp -a "$FIX/menu_one/$g" "$FIX/menu_3/$g"
done
mv "$FIX/menu_one/CaseUpper" "$FIX/menu_one/Third"
mkdir -p "$FIX/menu_3/system"
cp "$FONT" "$FIX/menu_3/system/default.ttf"

#   menu_nofontgame/ : three games with NO font of their own; the card-wide
#                      system/default.ttf at the games root is the only one,
#                      so the selection has to carry it across.
#   menu_fontless/   : the same, but with no font anywhere. Only an
#                      explicit CPYMO_MENU_FONT can make this one work.
mkdir -p "$FIX/menu_nofontgame" "$FIX/menu_fontless"
for g in Narcissus Yume Piano; do
  cp -a "$FIX/menu_one/$g" "$FIX/menu_nofontgame/$g"
  cp -a "$FIX/menu_one/$g" "$FIX/menu_fontless/$g"
done
rm -f "$FIX"/menu_nofontgame/*/system/default.ttf
rm -f "$FIX"/menu_fontless/*/system/default.ttf
mkdir -p "$FIX/menu_nofontgame/system"
cp "$FONT" "$FIX/menu_nofontgame/system/default.ttf"
cat "$BUILD/one.log"
ls -1 "$FIX"

echo "[3/4] the harness"
gcc -O2 -Wall -Wextra -Werror -std=gnu11 \
  -I"$ROOT/source/CPyMO-main/cpymo-backends/libretro" \
  "$ROOT/tests/host_menu.c" -ldl -o "$BUILD/host_menu"

echo "[4/4] the checks"
"$BUILD/host_menu" "$CORE" "$FIX" "$FONT" | tee "$BUILD/result.txt"

if grep -q '  FAIL' "$BUILD/result.txt"; then
  echo
  echo "FAIL: see the lines above" >&2
  exit 1
fi
echo
echo "PASS menu: the selector opens for several games, keeps the single-game"
echo "     auto-start, starts the plain path on an empty folder, switches game"
echo "     on selection, and does not leak a font or a save across games."
