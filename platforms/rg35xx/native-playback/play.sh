#!/bin/sh
set -u

APPDIR="${0%/*}"
[ "$APPDIR" = "$0" ] && APPDIR=.
APPDIR="$(cd "$APPDIR" && pwd -P)" || exit 1
ASSETS="$APPDIR"
if [ ! -d "$ASSETS" ]; then
    exit 1
fi
[ "$#" -eq 2 ] || exit 2
exec >>"$ASSETS/playback.log" 2>&1
echo "native_movie_begin time=$(date +%s)"
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
unset LD_PRELOAD LD_LIBRARY_PATH LD_AUDIT LD_DEBUG
cd "$ASSETS" || exit 1

loaded=0
stage=preflight

cleanup() {
    status=$1
    trap - 0 HUP INT TERM
    if [ "$loaded" -eq 1 ]; then
        echo "stage=unload"
        if /sbin/rmmod vde_drv; then
            echo "module_unload=ok"
        else
            echo "module_unload=failed"
            [ "$status" -ne 0 ] || status=30
        fi
    fi
    echo "kernel_log_tail:"
    dmesg | tail -n 90
    echo "last_playback_stage=$stage exit_status=$status"
    sync
    exit "$status"
}

fail() {
    echo "$2"
    exit "$1"
}

begin() {
    stage=$1
    echo "stage=$stage"
    sync
}

trap 'cleanup "$?"' 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

echo "CPyMO RG35XX 1.1.0 H.264 playback"
uname -a
begin preflight
[ "$(uname -r)" = 3.10.37 ] || fail 10 "kernel_release=unsupported"
[ "$(id -u)" = 0 ] || fail 11 "launcher_user=nonroot"

vde_found=0
for base in /proc/device-tree /sys/firmware/devicetree/base; do
    node="$base/vde@b0280000"
    [ -r "$node/compatible" ] || continue
    if tr '\000' '\n' <"$node/compatible" | grep -qx 'actions,atm7059a-vde'; then
        if [ -r "$node/status" ]; then
            case "$(tr -d '\000' <"$node/status")" in
                okay|ok) ;;
                *) continue ;;
            esac
        fi
        vde_found=1
        echo "vde_node=$node"
        break
    fi
done
[ "$vde_found" -eq 1 ] || fail 12 "vde_node=not_enabled"
if [ -e /dev/mali0 ] || [ -e /dev/vde ] || grep -q '^vde_drv ' /proc/modules; then
    fail 13 "vde_device_or_module=already_present"
fi
for required in ion_probe native-video vde_drv.ko runtime/ld-linux-armhf.so.3; do
    [ -r "$required" ] || fail 14 "missing=$required"
done

begin runtime
loader="$ASSETS/runtime/ld-linux-armhf.so.3"
[ -r "$1" ] && [ -r "$2" ] || fail 15 "native_movie_input_missing"
"$loader" --library-path "$ASSETS/runtime" --list "$ASSETS/native-video" ||
    fail 15 "native_movie_runtime_failed"

begin ion
"$ASSETS/ion_probe" || fail 16 "ion_probe=failed"

begin load
/sbin/insmod "$ASSETS/vde_drv.ko" || fail 17 "module_load=failed"
loaded=1
echo "module_load=ok"
begin bind
attempt=0
while [ ! -c /dev/mali0 ] && [ "$attempt" -lt 3 ]; do
    sleep 1
    attempt=$((attempt + 1))
done
[ -c /dev/mali0 ] || fail 18 "vde_device=missing"
ls -l /dev/mali0
echo "vde_interrupts_before:"
grep 'vde_isr' /proc/interrupts || :

begin continuous
"$loader" --library-path "$ASSETS/runtime" "$ASSETS/native-video" "$1" "$2"
status=$?
echo "native_movie_exit=$status"
echo "vde_interrupts_after:"
grep 'vde_isr' /proc/interrupts || :
exit "$status"
