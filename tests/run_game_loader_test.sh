#!/usr/bin/env bash
# §1 + §2 host acceptance: load games through the core the way the APPS launcher
# does, and check that each game keeps its OWN logical aspect and shows its own
# art.
#
#   bash tests/run_game_loader_test.sh
#
# The fixture games are all 4:3 EXCEPT Res540, which is 3:2. The output is
# always the 640x480 RGB565 frame (unchanged since v0.21), so the game's own
# aspect shows up in the letterbox: the 4:3 games fill the frame, Res540
# must leave black bars.
#
# Expected means are written in the OUTPUT's RGB565 field widths (r:5, g:6,
# b:5), because that is exactly what the core delivers: e.g. the RGB
# background (40,88,200) comes back as (5,22,25).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build-game-loader}"
FIX="$BUILD/fixture"
FONT="${CPYMO_TEST_FONT:-$ROOT/deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test/system/default.ttf}"
export CPYMO_TEST_FONT="$FONT"
unset CPYMO_SAVE_ROOT || true

mkdir -p "$BUILD"

echo "[1/4] host Libretro core from the current source"
cmake -S "$ROOT/source/CPyMO-main/cpymo-backends/libretro" -B "$BUILD/cmake" \
  -DCMAKE_BUILD_TYPE=Release -DGARLIC_BOOTSTRAP_NO_AUDIO=ON \
  -DCMAKE_C_FLAGS='-O2 -g' > "$BUILD/cmake.log" 2>&1
cmake --build "$BUILD/cmake" --parallel 4 > "$BUILD/build.log" 2>&1
CORE="$(find "$BUILD/cmake" -maxdepth 1 -name 'cpymo_libretro.*' -type f \
        ! -name '*.a' | sort | head -1)"
[ -n "$CORE" ] || { echo "no core built" >&2; exit 1; }

echo "[2/4] fixture games"
rm -rf "$FIX"
python3 "$ROOT/tests/build_resolution_fixture.py" "$FIX" --font "$FONT" \
  > "$BUILD/fixture.log" 2>&1
cat "$BUILD/fixture.log"

# Two extra directories, each a copy of Res320 with exactly one thing changed.
cp -a "$FIX/Res320" "$FIX/NoConfig"
rm -f "$FIX/NoConfig/gameconfig.txt"
cp -a "$FIX/Res320" "$FIX/CaseConfig"
mv "$FIX/CaseConfig/gameconfig.txt" "$FIX/CaseConfig/GameConfig.TXT"

echo "[3/4] frontend"
gcc -O2 -Wall -Wextra -Werror -std=gnu11 \
  -I"$ROOT/source/CPyMO-main/cpymo-backends/libretro" \
  "$ROOT/tests/host_game_loader.c" -ldl -o "$BUILD/host_game_loader"

echo "[4/4] each fixture as a DIRECTORY and as the FILE inside it"
{
  "$BUILD/host_game_loader" "$CORE" \
    "ok|5,22,25|0|-|$FIX/Res320" \
    "ok|5,22,25|0|-|$FIX/Res320/gameconfig.txt" \
    "ok|25,22,5|1|-|$FIX/Res540" \
    "ok|5,50,11|0|-|$FIX/Res800"
  # CaseUpper requests bg/test.png and the fixture deliberately ships only
  # BG/TEST.PNG, so whether it renders is a property of the HOST filesystem and
  # not of the engine. On a case-SENSITIVE host that fixture is a NEGATIVE case,
  # not a positive one; requiring it to load made this test fail on Linux/WSL
  # while passing on Windows. The engine's real requirement -- that a wrong-case
  # gameconfig.txt is reported as missing -- is tested just below.
  if grep -q 'CASE-SENSITIVE' "$BUILD/fixture.log"; then
    "$BUILD/host_game_loader" "$CORE" "ok|0,0,0|0|-|$FIX/CaseUpper"
    "$BUILD/host_game_loader" "$CORE" "fail|0,0,0|-|GameConfig.TXT|$FIX/CaseConfig"
  else
    "$BUILD/host_game_loader" "$CORE" "ok|18,15,22|0|-|$FIX/CaseUpper"
    "$BUILD/host_game_loader" "$CORE" "ok|5,22,25|0|-|$FIX/CaseConfig"
  fi
  "$BUILD/host_game_loader" "$CORE" "fail|0,0,0|-|gameconfig.txt|$FIX/NoConfig"
  "$BUILD/host_game_loader" "$CORE" "fail|0,0,0|-|-|$FIX/DoesNotExist"
} 2>&1 | tee "$BUILD/result.txt"

if [ -n "${PHYSICAL_GAMES:-}" ]; then
  echo "[extra] the user's own game directories under $PHYSICAL_GAMES"
  cases=()
  for g in "$PHYSICAL_GAMES"/*/; do
    [ -f "$g/gameconfig.txt" ] || continue
    # "0,0,0" means: this game's colours are unknown, only require that
    # something was painted.
    cases+=("ok|0,0,0|-|-|${g%/}")
  done
  if [ "${#cases[@]}" -gt 0 ]; then
    "$BUILD/host_game_loader" "$CORE" "${cases[@]}" 2>&1 \
      | tee -a "$BUILD/result.txt"
  else
    echo "  (no subdirectory with a gameconfig.txt)" | tee -a "$BUILD/result.txt"
  fi
fi

if grep -q 'FAIL' "$BUILD/result.txt"; then
  echo
  echo "FAIL: see the lines above" >&2
  exit 1
fi

echo "[5/5] save isolation (§3) and the historical layout (§3 back-compat)"
SAVE_ROOT="$BUILD/saveroot"
rm -rf "$SAVE_ROOT"
{
  CPYMO_SAVE_ROOT="$SAVE_ROOT" "$BUILD/host_game_loader" "$CORE" \
    "ok|5,22,25|0|-|$FIX/Res320" >/dev/null
  echo "  with CPYMO_SAVE_ROOT=$SAVE_ROOT:"
  find "$SAVE_ROOT" -type f | sed "s|$SAVE_ROOT|    root|" | sort

  rm -rf "$FIX/Res800/save"
  env -u CPYMO_SAVE_ROOT "$BUILD/host_game_loader" "$CORE" \
    "ok|5,50,11|0|-|$FIX/Res800" >/dev/null
  echo "  with no save root (the shipped layout):"
  find "$FIX/Res800/save" -type f \
    | sed "s|$FIX/Res800|    game|" | sort
} | tee -a "$BUILD/result.txt"

# The isolated write must be under the ROOT and named for the GAME, and the
# in-game directory must not be the place a root-configured run writes to.
# The game id carries a path-hash suffix ("Res320-<16 hex>"), so match its
# prefix rather than the bare basename: the suffix is exactly what keeps two
# Chinese-named games from sharing one directory.
if grep -qE "root/Res320-" "$BUILD/result.txt" \
   && grep -q "game/save/" "$BUILD/result.txt"; then
  echo "  ok    isolated saves land under <root>/<game_id>; unset keeps <gamedir>/save"
else
  echo "  FAIL  save isolation layout" | tee -a "$BUILD/result.txt"
  exit 1
fi

echo
echo "PASS game loading: universal content path, per-game logical aspect,"
echo "     case diagnostics, and per-game save isolation (host; the device"
echo "     runs the same core code)."
