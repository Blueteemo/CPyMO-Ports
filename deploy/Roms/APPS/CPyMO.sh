#!/bin/sh
# CPyMO for RG35XX GarlicOS (uClibc) -- GENERAL launcher.
#
# Unlike the v0.21 test launcher, this one is not tied to one game: it takes
# the game to run from its own name/argument or discovers the games sitting
# next to it. It writes nothing outside the APPS partition.
#
# Usage, in the order the launcher tries:
#
#   1. an explicit argument:           CPyMO.sh /mnt/mmc/Roms/APPS/MyGame
#   2. the environment:                CPYMO_GAME=/mnt/mmc/Roms/APPS/MyGame
#   3. a directory next to this script with the SAME NAME (so a per-game
#      launcher can be a one-line symlink/copy of this file:
#      MyGame.sh -> CPyMO.sh finds ./MyGame)
#   4. if exactly ONE directory next to this script has a gameconfig.txt,
#      that one.
#
# Otherwise it prints the candidates and exits 45. A game directory OR a file
# inside it (normally gameconfig.txt) is accepted, because the core resolves
# either.
#
# Environment (all optional)
#   CPYMO_RETROARCH_HOME  RetroArch root (default /mnt/mmc/CFW/retroarch)
#   CPYMO_RETROARCH_BIN   the binary, if it is not $HOME/retroarch
#   CPYMO_CORE            the core, if it is not found next to this script
#   CPYMO_SAVE_ROOT       where saves live; defaults to a writable directory
#                         on the same card, see below
#   CPYMO_LOG             the log file
#
# Exit codes: 0 ran, 41/42/43 missing files (see below), 44 no writable cwd,
#            45 could not decide which game, 46 the log is not writable.
set -u

APPDIR="${0%/*}"
[ "$APPDIR" = "$0" ] && APPDIR="."
APPDIR="$(cd "$APPDIR" 2>/dev/null && pwd -P)" || exit 44
SELF="${0##*/}"
SELF="${SELF%.sh}"

# --- the core ------------------------------------------------------
# Prefer a core next to the launcher, else the core shipped with the v0.21
# test game, which is the same core this project builds.
CORE="${CPYMO_CORE:-}"
if [ -z "$CORE" ]; then
    for cand in "$APPDIR/cpymo_libretro.so" \
                "$APPDIR/$SELF/cpymo_libretro.so" \
                "$APPDIR/CPyMO_RG35XX_uClibc_Test/cpymo_libretro.so"; do
        if [ -r "$cand" ]; then CORE="$cand"; break; fi
    done
fi

# --- which game -----------------------------------------------------
GAME="${1:-${CPYMO_GAME:-}}"
MENU="${CPYMO_MENU:-1}"
if [ -z "$GAME" ] && [ -e "$APPDIR/$SELF/gameconfig.txt" ]; then
    GAME="$APPDIR/$SELF"
fi
if [ -z "$GAME" ]; then
    # Count the sibling games.
    found=''
    count=0
    for d in "$APPDIR"/*/; do
        # The launcher's own directory (when it is named after a game) and
        # the save root are not games.
        case "${d%/}" in "$APPDIR/saves") continue ;; esac
        [ -f "$d/gameconfig.txt" ] || continue
        count=$((count + 1))
        found="${d%/}"
    done
    if [ "$count" = "1" ]; then
        # One game: the unambiguous case, exactly as before.
        GAME="$found"
    elif [ "$MENU" != "0" ]; then
        # Several (or none): hand the FOLDER to the core and let the built-in
        # selector choose. That is what §4 exists for; the core verifies the
        # directory and falls back to a plain load if it is not one.
        GAME="$APPDIR"
        GAMES_DIR="$APPDIR"
    else
        {
            echo 'stage=refused_ambiguous_game'
            echo "usage: $0 <game-directory-or-gameconfig.txt>"
            echo 'candidates:'
            for d in "$APPDIR"/*/; do
                [ -f "$d/gameconfig.txt" ] && echo "  ${d%/}"
            done
            echo 'set CPYMO_GAME, name a launcher after the game, or leave'
            echo 'CPYMO_MENU unset to let the built-in selector choose.'
        } >&2
        exit 45
    fi
fi

# A file inside the game is fine: the core takes the directory from it.
case "$GAME" in
    */gameconfig.txt) GAMEDIR="${GAME%/gameconfig.txt}" ;;
    *) GAMEDIR="$GAME" ;;
esac
GAME_ID="${GAMEDIR##*/}"
export CPYMO_GAMES_DIR="${GAMES_DIR:-}"
if [ -z "$CPYMO_GAMES_DIR" ]; then unset CPYMO_GAMES_DIR; fi

RA_ROOT="${CPYMO_RETROARCH_HOME:-/mnt/mmc/CFW/retroarch}"
RA_BIN="${CPYMO_RETROARCH_BIN:-$RA_ROOT/retroarch}"
LOG="${CPYMO_LOG:-$APPDIR/$SELF.log}"

# --- saves ---------------------------------------------------------
# Android/GarlicOS cards are mounted read-only in places; the historical
# in-game "save" directory needs the GAME directory to be writable. If a
# save root is not given, use one next to the APPS (the user's partition)
# and let the core keep everything per game under it.
if [ -z "${CPYMO_SAVE_ROOT:-}" ]; then
    for cand in "$APPDIR/saves" "/mnt/mmc/Roms/CPyMO_saves"; do
        if mkdir -p "$cand" 2>/dev/null && [ -w "$cand" ]; then
            CPYMO_SAVE_ROOT="$cand"
            break
        fi
    done
fi
export CPYMO_SAVE_ROOT

# --- the uClibc loader ---------------------------------------------
# This core is linked against ld-uClibc, so a missing loader is worth an
# explicit message before RetroArch's own error. CPYMO_LOADER overrides the
# path checked; CPYMO_LOADER=none skips the check (used by the host test).
LOADER="${CPYMO_LOADER:-/lib/ld-uClibc.so.0}"
if [ "$LOADER" = "none" ]; then LOADER=''; fi

{
    echo '=== CPyMO RG35XX GarlicOS/uClibc launcher ==='
    echo "time=$(date 2>/dev/null)"
    echo "script=$0"
    echo "launcher=$SELF"
    echo "app_dir=$APPDIR"
    echo "game=$GAME"
    echo "game_dir=$GAMEDIR"
    echo "game_id=$GAME_ID"
    echo "menu=${CPYMO_MENU:-1}"
    echo "games_dir=${CPYMO_GAMES_DIR:-<the game directory>}"
    echo "core=${CORE:-<not found>}"
    echo "save_root=${CPYMO_SAVE_ROOT:-<unset: in-game save/>}"
    echo "retroarch=$RA_BIN"
    echo "uclibc_loader=${LOADER:-<check skipped>}"
    echo "original_pwd=$(pwd)"
    for path in "${LOADER:-/dev/null}" /lib/ld-uClibc.so.1 /lib/libc.so.0 \
                "$RA_BIN" "${CORE:-/nonexistent}" "$GAMEDIR/gameconfig.txt" \
                "$GAMEDIR/system/default.ttf"; do
        if [ -e "$path" ]; then
            echo "exists=yes path=$path"
        else
            echo "exists=no path=$path"
        fi
    done

    [ -n "$CORE" ] && [ -r "$CORE" ] || {
        echo "stage=refused_missing_core core=${CORE:-<not found>}"; exit 41; }
    # Either a real game (its own gameconfig.txt) or the games folder the
    # built-in selector will read.
    if [ ! -f "$GAMEDIR/gameconfig.txt" ]; then
        if [ -z "${CPYMO_GAMES_DIR:-}" ]; then
            echo "stage=refused_no_gameconfig game_dir=$GAMEDIR"; exit 41
        fi
        echo "stage=launching_game_selector games_dir=$CPYMO_GAMES_DIR"
    fi
    if [ ! -x "$RA_BIN" ]; then
        echo 'stage=refused_missing_retroarch_binary'; exit 42
    fi
    if [ -n "$LOADER" ] && [ ! -e "$LOADER" ]; then
        echo "stage=refused_missing_uclibc_loader loader=$LOADER"; exit 43
    fi

    echo 'stage=launching_retroarch_libretro'
    cd "$RA_ROOT" || exit 44
    export HOME="$RA_ROOT"
    # The DIRECTORY is passed, so the same core works for every game.
    "$RA_BIN" -v -L "$CORE" "$GAMEDIR"
    result=$?
    echo "stage=retroarch_exited exit_code=$result"
    exit "$result"
} > "$LOG" 2>&1
