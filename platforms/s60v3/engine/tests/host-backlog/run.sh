#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 )); then
    printf 'Usage: %s SDL1_INCLUDE_DIR NEW_OUTPUT_PARENT\n' "$0" >&2
    exit 2
fi
sdl_include=$(realpath "$1")
output_parent=$(realpath "$2")
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d "$output_parent/cpymo-s60-backlog.XXXXXXXX")
mkdir -p "$out/fonts"
cp "$root"/assets/fonts/{default.otf,story-pixel.ttf,story-fusion-12.ttf} "$out/fonts/"
flags=(-std=gnu99 -O1 -DS60V3 -DSCREEN_BPP=16 -DDISABLE_FFMPEG_AUDIO -DDISABLE_AUDIO
    -DDISABLE_MOVIE -DEXIT_TO_GAME_SELECTOR -DSTBI_NO_THREAD_LOCALS
    "-DCPYMO_S60_DATA_DIR=\"$out\"" -I"$sdl_include" -I"$root/source")
cc "${flags[@]}" -Dmain=cpymo_unused_application_main \
    -c "$root/source/cpymo-backends/sdl1/main.c" -o "$out/main.o"
files=()
for file in "$root"/source/cpymo-backends/sdl1/*.c; do
    if [[ "$file" != */main.c ]]; then files+=("$file"); fi
done
cc "${flags[@]}" "$out/main.o" "$root"/source/cpymo/*.c "${files[@]}" \
    "$root/tests/host-ui/native_stub.c" "$root/tests/host-backlog/check_history.c" \
    -Wl,--wrap=cpymo_backend_text_create,--wrap=cpymo_backend_image_draw,--wrap=cpymo_backend_image_fill_rects,--wrap=cpymo_backend_text_draw,--wrap=strftime -l:libSDL-1.2.so.0 -lm -o "$out/check-history"
(
    cd "$out"
    python3 - <<'PYFIXTURE'
import os
from datetime import datetime, timezone
from pathlib import Path
from struct import pack
from zlib import compress, crc32

def solid_png(path, width, height, color):
    def chunk(kind, data):
        return pack(">I", len(data)) + kind + data + pack(">I", crc32(kind + data))
    pixels = (b"\x00" + color * width) * height
    header = pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                     chunk(b"IDAT", compress(pixels)) + chunk(b"IEND", b""))

for name in ("bare", "skin"):
    system = Path("menu-fixtures") / name / "system"
    system.mkdir(parents=True)
    solid_png(system / "sel_highlight.png", 32, 14, bytes((90, 101, 116, 255)))
solid_png(Path("menu-fixtures/skin/system/menu.png"), 245, 225, bytes((47, 57, 68, 255)))

save_dir = Path("save-fixture/save")
save_dir.mkdir(parents=True)
slots = (
    ("放学后的图书馆", "小春", "我推开门，外面是熟悉的长廊。她并没有抬头，只是把手里的书轻轻合上。窗外的雨还在下，细细密密，像是有人在很远的地方低声说话。"),
    ("站台", "明子", "等雨停了再走吧。"),
    ("约好的地方", "小春", "路灯亮了起来。她看着路口走过的人，仿佛还在等谁。等风小了一些，我们又沿着河岸走了很久，最后在桥边停了下来。"),
)
for i, fields in enumerate(slots):
    slot = save_dir / f"save-{i:02}.csav"
    header = b""
    for value in fields:
        data = value.encode()
        header += pack("<H", len(data)) + data
    slot.write_bytes(header)
    stamp = datetime(2026, 11, 22, 17, 52 + i, tzinfo=timezone.utc).timestamp()
    os.utime(slot, (stamp, stamp))
PYFIXTURE
    TZ=UTC SDL_VIDEODRIVER=dummy ./check-history | tee history.log
)
printf '%s\n' "$out"
