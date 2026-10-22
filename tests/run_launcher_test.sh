#!/usr/bin/env bash
# The launcher is POSIX sh and makes no device calls except `date` and `uname`,
# both optional, and the RETROARCH BINARY -- which is the one thing the test
# replaces with a recorder. Everything else (argument resolution, the core
# lookup, the save root, the exit codes and the log) is the real logic, so a
# device failure here is a failure of the script rather than of the test rig.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

APPS="$WORK/APPS"
FAKE_RA="$WORK/retroarch"
mkdir -p "$APPS/GameOne/script" "$APPS/GameOne/bg" \
         "$APPS/GameTwo/script" "$APPS/GameTwo/bg" "$WORK/rahome"
printf 'gametitle,A\nimagesize,800,600\n' > "$APPS/GameOne/gameconfig.txt"
printf 'gametitle,B\nimagesize,540,360\n' > "$APPS/GameTwo/gameconfig.txt"
printf '#wait 1\n' > "$APPS/GameOne/script/start.txt"
printf '#wait 1\n' > "$APPS/GameTwo/script/start.txt"
: > "$APPS/cpymo_libretro.so"

# A per-game launcher is a copy of the general one with the game's name.
cp "$ROOT/deploy/Roms/APPS/CPyMO.sh" "$APPS/CPyMO.sh"
chmod +x "$APPS/CPyMO.sh"
cp "$ROOT/deploy/Roms/APPS/CPyMO.sh" "$APPS/GameOne.sh"
cp "$ROOT/deploy/Roms/APPS/CPyMO.sh" "$APPS/Story.sh"
# Git bundle/Windows ZIP may preserve shell scripts as 0644. Test the script,
# not the packager's executable-bit behavior.
chmod +x "$APPS/GameOne.sh" "$APPS/Story.sh"
mkdir -p "$APPS/Story" && printf 'gametitle,S\nimagesize,320,240\n' \
  > "$APPS/Story/gameconfig.txt"

# The recorder answers like RetroArch: it echoes the -L core and the content.
cat > "$FAKE_RA" <<'EOF'
#!/bin/sh
printf 'retroarch_argv:'
for a in "$@"; do printf ' [%s]' "$a"; done
printf '\n'
echo 'CPYMO_PERF v0.21 frames=180 draws=6 elapsed_ms=3450 fps_x10=521 update_us_avg=9 draw_us_avg=73000 convert_us_avg=5100'
exit 0
EOF
chmod +x "$FAKE_RA"

failures=0
check() { # check <description> <expected-substring> <file>
    if grep -qF -- "$2" "$3"; then
        echo "  ok    $1"
    else
        echo "  FAIL  $1  (wanted '$2' in $(basename "$3"))"
        sed 's/^/        /' "$3"
        failures=$((failures + 1))
    fi
}
check_not() {
    if grep -qF -- "$2" "$3"; then
        echo "  FAIL  $1  (did not want '$2')"
        failures=$((failures + 1))
    else
        echo "  ok    $1"
    fi
}

export CPYMO_RETROARCH_BIN="$FAKE_RA"
export CPYMO_RETROARCH_HOME="$WORK/rahome"
export CPYMO_SAVE_ROOT="$WORK/saves"
# The host has no uClibc loader; the check itself is exercised separately.
export CPYMO_LOADER=none

echo '== 1. explicit argument (a directory)'
CPYMO_LOG="$WORK/a.log" "$APPS/CPyMO.sh" "$APPS/GameOne"
check    "game_dir is recorded"   "game_dir=$APPS/GameOne" "$WORK/a.log"
check    "the core was found next to the launcher" "core=$APPS/cpymo_libretro.so" "$WORK/a.log"
check    "retroarch ran with -L"  "[-L]" "$WORK/a.log"
check    "on the game directory" "$APPS/GameOne" "$WORK/a.log"
check    "the launcher announced the launch" "stage=launching_retroarch_libretro" "$WORK/a.log"
check    "and a clean exit"       "stage=retroarch_exited exit_code=0" "$WORK/a.log"

echo '== 2. a FILE inside the game resolves to its directory'
CPYMO_LOG="$WORK/b.log" "$APPS/CPyMO.sh" "$APPS/GameTwo/gameconfig.txt"
check "the directory is used, not the file" "game_dir=$APPS/GameTwo" "$WORK/b.log"
check "game_id is the directory's name"     "game_id=GameTwo" "$WORK/b.log"

echo '== 3. game_id changes with the game, so saves cannot mix'
check_not "GameOne's id is not GameTwo's" "game_id=GameOne" "$WORK/b.log"

echo '== 4. argument beats the environment'
CPYMO_GAME="$APPS/GameTwo" CPYMO_LOG="$WORK/c.log" "$APPS/CPyMO.sh" "$APPS/GameOne"
check "the argument wins" "game_id=GameOne" "$WORK/c.log"

echo '== 5. a launcher named after its game'
CPYMO_LOG="$WORK/d.log" "$APPS/GameOne.sh"
check "GameOne.sh found GameOne" "game_dir=$APPS/GameOne" "$WORK/d.log"

echo '== 6. the environment variable alone'
CPYMO_GAME="$APPS/GameTwo" CPYMO_LOG="$WORK/e.log" "$APPS/CPyMO.sh"
check "CPYMO_GAME was honoured" "game_id=GameTwo" "$WORK/e.log"

echo '== 7. unmatched launcher: it finds its own directory; ambiguous exits 45'
mkdir -p "$APPS/Rest"
set +e
CPYMO_LOG="$WORK/f.log" "$APPS/Story.sh" >/dev/null 2>&1
rc=$?
set -e
check "Story.sh found its own directory" "game_dir=$APPS/Story" "$WORK/f.log"

cp "$ROOT/deploy/Roms/APPS/CPyMO.sh" "$APPS/Ambiguous.sh"
chmod +x "$APPS/Ambiguous.sh" "$APPS/GameOne.sh" "$APPS/Story.sh"

echo "== 7b. several games, menu ON (default): the FOLDER is launched"
set +e
CPYMO_LOG="$WORK/g0.log" "$APPS/Ambiguous.sh" >/dev/null 2>&1
rc0=$?
set -e
if [ "$rc0" = "0" ]; then echo "  ok    the folder launch succeeds"; else
  echo "  FAIL  folder launch exited $rc0"; failures=$((failures+1)); fi
check "the games folder is what RetroArch is given" \
      "[$APPS]" "$WORK/g0.log"
check "the header says the games dir" "games_dir=$APPS" "$WORK/g0.log"
check "and that the selector is starting" "launching_game_selector games_dir=$APPS" "$WORK/g0.log"

echo "== 7b2. explicit games-root launches selector"
CPYMO_LOG="$WORK/g_explicit.log" "$APPS/CPyMO.sh" "$APPS"
check "explicit games folder selects the menu" \
      "stage=launching_game_selector games_dir=$APPS" "$WORK/g_explicit.log"
check "explicit root was given to core" "[$APPS]" "$WORK/g_explicit.log"
mkdir -p "$WORK/EmptyGame"
set +e
CPYMO_LOG="$WORK/g_empty.log" "$APPS/CPyMO.sh" "$WORK/EmptyGame" >/dev/null 2>&1
empty_rc=$?
set -e
if [ "$empty_rc" = 41 ]; then echo '  ok    empty game rejected'; else
    echo "  FAIL  empty game exited $empty_rc (expected 41)"; failures=$((failures+1)); fi

echo "== 7c. CPYMO_MENU=0 restores the refusal"
set +e
CPYMO_MENU=0 CPYMO_LOG="$WORK/g.log" "$APPS/Ambiguous.sh" >/dev/null 2>"$WORK/g.err"
rc=$?
set -e
if [ "$rc" = "45" ]; then echo "  ok    an ambiguous launch exits 45"; else
    echo "  FAIL  ambiguous launch exited $rc, wanted 45"; failures=$((failures+1)); fi
check "the candidates are listed" "candidates:" "$WORK/g.err"
check "the first candidate is a real game" "$APPS/GameOne" "$WORK/g.err"

echo '== 8. failure paths are explicit, and distinct'
mv "$APPS/GameOne/gameconfig.txt" "$APPS/GameOne/gameconfig.bak"
set +e
CPYMO_LOG="$WORK/h.log" "$APPS/CPyMO.sh" "$APPS/GameOne" >/dev/null 2>&1
rc=$?
set -e
if [ "$rc" = "41" ]; then echo "  ok    a game with no gameconfig.txt exits 41"; else
    echo "  FAIL  no-gameconfig launch exited $rc, wanted 41"; failures=$((failures+1)); fi
check "and names the directory" "refused_no_gameconfig game_dir=$APPS/GameOne" "$WORK/h.log"
mv "$APPS/GameOne/gameconfig.bak" "$APPS/GameOne/gameconfig.txt"

set +e
CPYMO_CORE="$WORK/absent.so" CPYMO_LOG="$WORK/i.log" \
  "$APPS/CPyMO.sh" "$APPS/GameOne" >/dev/null 2>&1
rc=$?
set -e
if [ "$rc" = "41" ]; then echo "  ok    a missing core exits 41"; else
    echo "  FAIL  missing-core launch exited $rc, wanted 41"; failures=$((failures+1)); fi
check "and says so" "refused_missing_core core=$WORK/absent.so" "$WORK/i.log"

set +e
CPYMO_RETROARCH_BIN="$WORK/no-such-retroarch" CPYMO_LOG="$WORK/j.log" \
  "$APPS/CPyMO.sh" "$APPS/GameOne" >/dev/null 2>&1
rc=$?
set -e
if [ "$rc" = "42" ]; then echo "  ok    a missing RetroArch exits 42"; else
    echo "  FAIL  missing-retroarch exited $rc, wanted 42"; failures=$((failures+1)); fi

echo '== 9. the HEADER advertises the game and the save root before launching'
check "the log opens" "=== CPyMO RG35XX GarlicOS/uClibc launcher ===" "$WORK/a.log"
check "the game is named up front"   "game=$APPS/GameOne" "$WORK/a.log"
check "the save root is named"      "save_root=$WORK/saves" "$WORK/a.log"
check "the core's own perf line also lands in this log" "CPYMO_PERF" "$WORK/a.log"
check "gameconfig.txt is found"     "exists=yes path=$APPS/GameOne/gameconfig.txt" "$WORK/a.log"

echo '== 10. exit 43 when the uClibc loader is missing'
set +e
CPYMO_LOADER=/lib/ld-uClibc-so-that-is-not-here.so CPYMO_LOG="$WORK/k.log" \
  "$APPS/CPyMO.sh" "$APPS/GameOne" >/dev/null 2>&1
rc=$?
set -e
if [ "$rc" = "43" ]; then echo "  ok    a missing loader exits 43"; else
    echo "  FAIL  missing-loader exited $rc, wanted 43"; failures=$((failures+1)); fi
check "and names the path it looked for" \
      "refused_missing_uclibc_loader loader=/lib/ld-uClibc-so-that-is-not-here.so" \
      "$WORK/k.log"

echo '== 11. the shipped test launcher still parses'
sh -n "$ROOT/deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test.sh"
sh -n "$ROOT/deploy/Roms/APPS/CPyMO.sh"
echo "  ok    both launchers are valid POSIX sh"

echo
if [ "$failures" -eq 0 ]; then
    echo "PASS launcher: argument/environment/name discovery, file-vs-directory, "
    echo "     explicit failure codes, per-game save root."
else
    echo "FAIL launcher: $failures check(s)" >&2
    exit 1
fi
