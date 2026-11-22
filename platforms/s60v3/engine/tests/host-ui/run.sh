#!/usr/bin/env bash
set -euo pipefail
if (( $# != 3 )); then
    printf 'Usage: %s R1_FIXTURE_GAME_DIR SDL1_INCLUDE_DIR NEW_WORK_PARENT\n' "$0" >&2
    exit 2
fi
fixture=$(realpath "$1")
sdl_include=$(realpath "$2")
output_parent=$(realpath "$3")
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d "$output_parent/cpymo-s60-host.XXXXXXXX")
mkdir -p "$out/fonts" "$out/run-import" "$out/run-font"
cp "$root"/assets/fonts/{default.otf,story-pixel.ttf,story-fusion-12.ttf} "$out/fonts/"
for scenario in run-import run-font; do
    for id in 1 2 3 4 5 6; do
        target="$out/$scenario/E:/Python/game$id"
        mkdir -p "$target"
        cp -a "$fixture/." "$target/"
        sed -i "s/自制验证小样/验证小样 $id/" "$target/gameconfig.txt"
    done
done

mkdir -p "$out/run-import/E:/Python/game5/save"
python3 - "$out/run-import/E:/Python/game5/save" <<'PY'
from pathlib import Path
import sys

dest = Path(sys.argv[1])
lines = ['start.txt', '5', '?,?', '2026,11,21,10,30,0,5,325,0,',
         '移植验证', '', 'logo1,0,0', 'chara_y,', '测试',
         '存档点：在这里保存到一号槽，继续到下一段后读回。',
         'FIX_BRANCH,1,', '', 'message,name', '']
(dest / '1.sav').write_bytes(('\ufeff' + '\n'.join(lines) + '\n').encode())
(dest / '2.sav').write_bytes(('\ufeff' + '\n'.join(lines[:3])).encode())
PY
cc -std=gnu99 -O1 -DS60V3 -DSCREEN_BPP=16 -DDISABLE_FFMPEG_AUDIO -DDISABLE_AUDIO -DDISABLE_MOVIE \
    -DEXIT_TO_GAME_SELECTOR -DSTBI_NO_THREAD_LOCALS "-DCPYMO_S60_DATA_DIR=\"$out\"" \
    -I"$sdl_include" -I"$root/source" "$root"/source/cpymo/*.c "$root"/source/cpymo-backends/sdl1/*.c \
    "$root/tests/host-ui/native_stub.c" "$root/tests/host-ui/drive_ui.c" \
    -Wl,--wrap=SDL_GetKeyState,--wrap=SDL_PollEvent,--wrap=SDL_Flip \
    -l:libSDL-1.2.so.0 -lm -o "$out/cpymo-host-ui" >"$out/build.log" 2>&1
(
    cd "$out/run-import"
    sha256sum 'E:/Python/game5/save/'*.sav >original-saves.sha256
    SDL_VIDEODRIVER=dummy timeout 18s "$out/cpymo-host-ui" >runtime.log 2>&1
    sha256sum -c original-saves.sha256 >original-saves-check.txt
    test -s 'E:/Python/game5/save/save-01.csav'
    grep -q 'Malform.*occupied slot' runtime.log
    grep -q 'about about=1' stage.log
    grep -q 'dialogue-hidden.*hidden=1' stage.log
    grep -q 'restored-checkpoint.*存档点' stage.log
    grep -q 'loaded-dialogue.*读档检查' stage.log
    grep -q 'normal application exit requested' runtime.log
)
(
    cd "$out/run-font"
    printf 0 >"$out/font-profile.txt"
    CPYMO_TEST_FONT=1 SDL_VIDEODRIVER=dummy timeout 14s "$out/cpymo-host-ui" >runtime.log 2>&1
    grep -q 'font-restart-prompt' stage.log
    grep -q 'font-restart-cancelled' stage.log
    grep -q 'active profile 1' runtime.log
    grep -q 'normal application exit requested' runtime.log
)
printf '%s\n' "$out"
