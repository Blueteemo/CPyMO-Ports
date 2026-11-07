#!/bin/sh
# CPyMO for RG35XX GarlicOS (uClibc) -- GENERAL launcher.
#
# Unlike the v0.21 test launcher, this one is not tied to one game: it takes
# the game to run from its own name/argument or discovers the games sitting
# next to it. Persistent files stay in APPS; private runtime files use memory.
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
#   CPYMO_RUNTIME_ROOT    explicit temporary root for QA/special firmware;
#                         normally unset (automatic memory-filesystem roots)
#
# Exit codes: 0 ran, 41/42/43 missing files (see below), 44 no writable cwd,
#            45 could not decide which game, 46 log unavailable,
#            47 private launch configuration could not be prepared.
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
    for cand in "$APPDIR/$SELF/cpymo_libretro.so" \
                "$APPDIR/cpymo_libretro.so" \
                "$APPDIR/CPyMO_RG35XX_uClibc_Test/cpymo_libretro.so"; do
        if [ -r "$cand" ]; then CORE="$cand"; break; fi
    done
fi

# --- which game -----------------------------------------------------
GAME="${1:-${CPYMO_GAME:-}}"
MENU="${CPYMO_MENU:-1}"
# Packaged layout: the games live in <this script's own folder>/games. That
# keeps ONE entry in the APPS menu, ONE core instance, and game data apart from
# the engine. A game there opens the selector even when it is the only one.
# Games dropped directly into APPS keep working exactly as before.
if [ -z "$GAME" ] && [ "$MENU" != 0 ] && [ -d "$APPDIR/$SELF/games" ]; then
    for candidate in "$APPDIR/$SELF/games"/*/; do
        if [ -f "$candidate/gameconfig.txt" ]; then
            GAME="$APPDIR/$SELF/games"
            break
        fi
    done
fi
# A games-root explicitly supplied as an argument should open the selector;
# it is not a malformed game merely because it lacks gameconfig.txt.
if [ -n "$GAME" ] && [ "$MENU" != 0 ] && [ -d "$GAME" ] \
   && [ ! -f "$GAME/gameconfig.txt" ]; then
    for candidate in "$GAME"/*/; do
        if [ -f "$candidate/gameconfig.txt" ]; then
            GAMES_DIR="$(cd "$GAME" && pwd -P)"
            GAME="$GAMES_DIR"
            break
        fi
    done
fi
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

# When the general selector is launched, prefer the versioned core's
# packaged menu font over unrelated older tests on the TF card.
if [ -n "${GAMES_DIR:-}" ] && [ -z "${CPYMO_MENU_FONT:-}" ] \
   && [ ! -r "$GAMES_DIR/system/default.ttf" ]; then
    for menu_font in "$APPDIR/$SELF/system/default.ttf" \
                     "$APPDIR/CPyMO_RG35XX_uClibc_Test/system/default.ttf"; do
        if [ -r "$menu_font" ]; then
            CPYMO_MENU_FONT="$menu_font"
            export CPYMO_MENU_FONT
            break
        fi
    done
fi

RA_ROOT="${CPYMO_RETROARCH_HOME:-/mnt/mmc/CFW/retroarch}"
RA_BIN="${CPYMO_RETROARCH_BIN:-$RA_ROOT/retroarch}"
APPHOME="$APPDIR/CPyMO"
LOGDIR="$APPHOME/logs"
mkdir -p "$LOGDIR" || exit 46
LOG="${CPYMO_LOG:-$LOGDIR/launcher.log}"
CPYMO_AUDIO_REPORT="${CPYMO_AUDIO_REPORT:-$LOGDIR/audio-status.txt}"
export CPYMO_AUDIO_REPORT
# Recording is opt-in; leave the normal run free of the 10-second PCM capture.
if [ "${CPYMO_AUDIO_DIAGNOSTICS:-0}" = "1" ]; then
    CPYMO_AUDIO_CAPTURE="${CPYMO_AUDIO_CAPTURE:-$LOGDIR/audio-capture.wav}"
else
    CPYMO_AUDIO_CAPTURE="${CPYMO_AUDIO_CAPTURE:-}"
fi
export CPYMO_AUDIO_CAPTURE
# No source/eval of a writable settings file. Only a validated number is read.
if [ -z "${CPYMO_OUTPUT_PERCENT:-}" ]; then
    CPYMO_OUTPUT_PERCENT=50
    if [ -r "$APPHOME/output-volume.txt" ]; then
        IFS= read -r CPYMO_OUTPUT_PERCENT < "$APPHOME/output-volume.txt" || :
    fi
fi
case "$CPYMO_OUTPUT_PERCENT" in
    ''|*[!0-9]*) CPYMO_OUTPUT_PERCENT=50 ;;
    *) [ "$CPYMO_OUTPUT_PERCENT" -le 100 ] 2>/dev/null || CPYMO_OUTPUT_PERCENT=50 ;;
esac
export CPYMO_OUTPUT_PERCENT
[ ! -f "$APPHOME/debug.flag" ] || CPYMO_DEBUG=1
export CPYMO_DEBUG
APPEND_CONFIG="$APPHOME/retroarch-cpymo.cfg"
RA_CONFIG="${CPYMO_RETROARCH_CONFIG:-$RA_ROOT/.retroarch/retroarch.cfg}"
[ -f "$RA_CONFIG" ] || RA_CONFIG="$RA_ROOT/retroarch.cfg"

# GarlicOS saves the device's RA gain independently. The APPS shell branch
# skips the frontend merge, so read it ourselves, without editing either file.
VOLUME_CONFIG="$RA_ROOT/.retroarch/volume.cfg"
RUNTIME_DIR=''
RA_PID=''
cleanup_runtime() {
    [ -n "$RUNTIME_DIR" ] || return 0
    rm -f "$RUNTIME_DIR/volume.input" "$RUNTIME_DIR/session.cfg"
    rmdir "$RUNTIME_DIR" 2>/dev/null || :
}
stop_retroarch() {
    if [ -n "$RA_PID" ]; then
        kill -TERM "$RA_PID" 2>/dev/null || :
        wait "$RA_PID" 2>/dev/null || :
        RA_PID=''
    fi
}
runtime_filesystem() {
    # Select the longest mountpoint prefix, including /tmp bind mounts. This
    # keeps automatic fallback away from TF-backed system/game directories.
    [ -r /proc/mounts ] || return 1
    LC_ALL=C awk -v target="$1" '
        {
            mountpoint=$2
            gsub(/\\040/, " ", mountpoint)
            if (mountpoint == "/" || target == mountpoint ||
                index(target, mountpoint "/") == 1) {
                if (length(mountpoint) >= longest) {
                    longest=length(mountpoint); filesystem=$3
                }
            }
        }
        END { if (!longest) exit 1; print filesystem }
    ' /proc/mounts
}
prepare_runtime_dir() {
    # No mktemp dependency: some target systems return EIO in that command.
    # mkdir is the exclusive-create operation: never reuse/delete a collision.
    echo 'runtime_allocator=atomic_mkdir'
    runtime_first=1
    for runtime_base in "${CPYMO_RUNTIME_ROOT:-/tmp}" /dev/shm /run /var/run /dev; do
        runtime_explicit=0
        if [ "$runtime_first" = 1 ] && [ -n "${CPYMO_RUNTIME_ROOT:-}" ]; then
            runtime_explicit=1
        fi
        runtime_first=0
        case "$runtime_base" in
            /*) ;;
            *) echo "runtime_skip=nonabsolute root=$runtime_base"; continue ;;
        esac
        case "$runtime_base" in
            *'|'*) echo "runtime_skip=append_separator root=$runtime_base"; continue ;;
        esac
        if [ ! -d "$runtime_base" ]; then
            echo "runtime_skip=missing root=$runtime_base"; continue
        fi
        runtime_base="$(cd "$runtime_base" 2>/dev/null && pwd -P)" || continue
        runtime_fs="$(runtime_filesystem "$runtime_base" 2>/dev/null)" || runtime_fs='unknown'
        if [ "$runtime_explicit" != 1 ]; then
            case "$runtime_fs" in
                tmpfs|ramfs|rootfs|devtmpfs) ;;
                *) echo "runtime_skip=nonmemory root=$runtime_base fs=$runtime_fs"; continue ;;
            esac
        fi
        runtime_try=0
        while [ "$runtime_try" -lt 32 ]; do
            runtime_try=$((runtime_try + 1))
            runtime_candidate="$runtime_base/cpymo-ra.$$.$runtime_try"
            if runtime_error="$(umask 077; mkdir "$runtime_candidate" 2>&1)"; then
                # Prove the private config can be created here before reading
                # independent volume. Only this newly owned directory is ours.
                if runtime_error="$( (umask 077; printf '# CPyMO runtime probe\n' > "$runtime_candidate/session.cfg") 2>&1)"; then
                    RUNTIME_DIR="$runtime_candidate"
                    echo "runtime_root=$runtime_base"
                    echo "runtime_fs=$runtime_fs"
                    echo "runtime_explicit=$runtime_explicit"
                    echo "runtime_dir=$RUNTIME_DIR"
                    return 0
                fi
                printf 'runtime_failure=config_write root=%s detail=%s\n' "$runtime_base" "$runtime_error"
                rm -f "$runtime_candidate/session.cfg" 2>/dev/null || :
                rmdir "$runtime_candidate" 2>/dev/null || :
                break
            elif [ -e "$runtime_candidate" ] || [ -L "$runtime_candidate" ]; then
                echo "runtime_collision=$runtime_candidate"
            else
                printf 'runtime_failure=mkdir root=%s detail=%s\n' "$runtime_base" "$runtime_error"
                break
            fi
        done
    done
    echo 'runtime_failure=no_private_writable_memory_directory'
    return 1
}
prepare_session_config() {
    # Keep the stable volume parser/precedence; only allocation changes.
    prepare_runtime_dir || return 1
    trap 'cleanup_runtime' 0
    trap 'stop_retroarch; exit 129' HUP
    trap 'stop_retroarch; exit 130' INT
    trap 'stop_retroarch; exit 143' TERM
    SESSION_CONFIG="$RUNTIME_DIR/session.cfg"
    VOLUME_DB='0.000000'
    VOLUME_STATUS='missing_or_unreadable'
    if [ -f "$VOLUME_CONFIG" ] && [ -r "$VOLUME_CONFIG" ]; then
        # Snapshot at most 1025 bytes once. Concurrent/partial firmware writes
        # are rejected, never executed or passed through as configuration.
        VOLUME_STATUS='read_error'
        if (umask 077; dd if="$VOLUME_CONFIG" of="$RUNTIME_DIR/volume.input" bs=1025 count=1 2>/dev/null); then
            bytes="$(wc -c < "$RUNTIME_DIR/volume.input")"
            VOLUME_STATUS='too_large'
            if [ "$bytes" -le 1024 ]; then
                bad_bytes="$(LC_ALL=C tr -d '\011\012\015\040-\176' < "$RUNTIME_DIR/volume.input" | wc -c)"
                VOLUME_STATUS='invalid_bytes'
                if [ "$bad_bytes" -eq 0 ]; then
                    VOLUME_STATUS='invalid_format_or_range'
                    parsed_db="$(LC_ALL=C awk '
                        NR > 16 { bad = 1 }
                        { sub(/\r$/, "") }
                        /^[ \t]*$/ { next }
                        /^[ \t]*#[ \t]*(include|reference)([ \t]|$)/ { bad = 1; next }
                        /^[ \t]*#/ { next }
                        {
                            if ($0 !~ /^[ \t]*audio_volume[ \t]*=[ \t]*"[-+]?[0-9]+([.][0-9]+)?"[ \t]*(#.*)?$/) {
                                bad = 1; next
                            }
                            value = $0
                            sub(/^[^"]*"/, "", value)
                            sub(/".*$/, "", value)
                            digits = value
                            sub(/^[-+]/, "", digits)
                            split(digits, parts, /[.]/)
                            if (length(parts[1]) > 3 || length(parts[2]) > 6 ||
                                value + 0 < -80 || value + 0 > 12) bad = 1
                            count++
                        }
                        END {
                            if (bad || count != 1) exit 1
                            printf "%.6f\n", value + 0
                        }
                    ' "$RUNTIME_DIR/volume.input" 2>/dev/null)" && {
                        VOLUME_DB="$parsed_db"
                        VOLUME_STATUS='inherited'
                    }
                fi
            fi
        fi
    fi
    # A damaged/missing independent value must not resurrect positive stale
    # global gain. Neutral RA 0 dB is the fallback; CPyMO stays at its 50% gain.
    (umask 077; printf 'audio_volume = "%s"\nconfig_save_on_exit = "false"\nauto_overrides_enable = "false"\nquit_on_close_content = "1"\n' "$VOLUME_DB" > "$SESSION_CONFIG") || return 1
    return 0
}

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
    echo 'launcher_revision=0.24.5-layout12'
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
    prepare_session_config || {
        echo 'stage=refused_private_config'; exit 47;
    }
    echo "ra_config=$RA_CONFIG"
    echo "volume_config=$VOLUME_CONFIG"
    echo "volume_status=$VOLUME_STATUS"
    echo "ra_audio_volume_db=$VOLUME_DB"
    echo "cpymo_output_percent=$CPYMO_OUTPUT_PERCENT"
    # In RA 1.14.0, ONE append argument holds a | separated list. The last
    # file wins. Repeated --appendconfig options would discard earlier ones.
    # Auto overrides are disabled only here, so they cannot replace this gain
    # or turn config_save_on_exit back on after the append pass.
    case "$APPEND_CONFIG$SESSION_CONFIG" in
        *'|'*) echo 'stage=refused_append_path_separator'; exit 47 ;;
    esac
    set -- -L "$CORE"
    [ ! -r "$RA_CONFIG" ] || set -- "$@" --config "$RA_CONFIG"
    append_list="$SESSION_CONFIG"
    [ ! -r "$APPEND_CONFIG" ] || append_list="$APPEND_CONFIG|$SESSION_CONFIG"
    set -- "$@" --appendconfig "$append_list"
    "$RA_BIN" "$@" "$GAMEDIR" >/dev/null 2>&1 &
    RA_PID=$!
    wait "$RA_PID"
    result=$?
    RA_PID=''
    echo "stage=retroarch_exited exit_code=$result"
    exit "$result"
} > "$LOG" 2>&1
