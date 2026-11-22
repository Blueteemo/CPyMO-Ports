#!/system/bin/sh
# NON-INSTALLING manual launch helper for original RG35XX Stock/CFW MOD.
# Invoke from an already-established debug shell. No dmenu, flash or system edits.
# Usage: /system/bin/sh launch_cpymo_stock.sh /mnt/mmc/CPyMO/cpymo.dge /mnt/mmc/CPyMO/game
if [ "$#" -ne 2 ]; then
 echo 'Usage: launch_cpymo_stock.sh /absolute/path/cpymo.dge /absolute/path/game' >&2
 exit 2
fi
BIN="$1"
GAME="$2"
case "$BIN" in /*) ;; *) echo 'Executable path must be absolute' >&2; exit 2;; esac
case "$GAME" in /*) ;; *) echo 'Game path must be absolute' >&2; exit 2;; esac
if [ ! -f "$BIN" ] || [ ! -x "$BIN" ]; then
 echo "CPyMO binary missing or not executable: $BIN" >&2
 exit 3
fi
if [ ! -r "$GAME/gameconfig.txt" ]; then
 echo "Missing readable gameconfig.txt: $GAME" >&2
 exit 4
fi
if [ ! -r "$GAME/system/default.ttf" ] && [ ! -r "$GAME/system/default.otf" ] && [ ! -r "$GAME/system/default.fnt" ]; then
 echo "No usable game-local default font: $GAME/system" >&2
 exit 5
fi
# No writes outside the user-chosen game, no environment/library injection,
# no modification to /system, dmenu, SD partition tables or boot settings.
exec "$BIN" "$GAME"
