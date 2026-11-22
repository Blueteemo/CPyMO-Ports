#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$ROOT/source/CPyMO-main"
BUILD="$ROOT/build-host-sim"
mkdir -p "$BUILD/obj" "$BUILD/game/script" "$BUILD/game/system"
cat > "$BUILD/game/gameconfig.txt" <<'GAME'
gametitle,RG35XX synthetic full-engine test
engineversion,1.2
platform,pygame
scripttype,pymo
imagesize,320,240
startscript,start
bgformat,.png
bgmformat,wav
fontsize,20
GAME
python3 - "$BUILD/game" <<'GENPNG'
import sys, os, struct, zlib
from pathlib import Path
root=Path(sys.argv[1]); (root/'bg').mkdir(exist_ok=True)
def chunk(k, d):
 return struct.pack('>I',len(d))+k+d+struct.pack('>I',zlib.crc32(k+d)&0xffffffff)
w,h=32,24
p=bytes([0x20,0x58,0xc8]*w)
raw=b''.join(b'\x00'+p for _ in range(h))
img=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
for n in ('logo1','logo2'):(root/'bg'/f'{n}.png').write_bytes(img)
for n in ('message','name','message_cursor'):(root/'system'/f'{n}.png').write_bytes(img)
GENPNG
python3 - "$BUILD/game" <<'GENWAV'
import sys,wave,struct
from pathlib import Path
p=Path(sys.argv[1])/'bgm';p.mkdir(exist_ok=True)
with wave.open(str(p/'song.wav'),'wb') as w:
 w.setnchannels(2);w.setframerate(22050);w.setsampwidth(2)
 w.writeframes(struct.pack('<hh',1350,-1350)*11025)
GENWAV
cat > "$BUILD/game/script/start.txt" <<'SCRIPT'
#bgm song,1
#say Test,Hello from real CPyMO core
#wait 100
SCRIPT
cc=(gcc -O1 -g -std=gnu11 -fno-common -DCPYMO_RG35XX_WAV_AUDIO -DDISABLE_MOVIE -DDISABLE_FFMPEG_AUDIO -DCPYMO_RG35XX_STOCK -DENABLE_SDL_TTF=2 -I"$ROOT/tests/sdl_stub" -I"$REPO/cpymo")
if [[ "${HOST_SANITIZE:-0}" == 1 ]]; then
  cc+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
objects=()
for f in "$REPO"/cpymo/*.c "$REPO"/cpymo-backends/sdl1/*.c; do
    o="$BUILD/obj/$(basename "$f" .c).o"
    "${cc[@]}" -c "$f" -o "$o"
    objects+=("$o")
done
"${cc[@]}" -c "$ROOT/tests/host_sdl_runtime.c" -o "$BUILD/obj/host_sdl_runtime.o"
objects+=("$BUILD/obj/host_sdl_runtime.o")
"${cc[@]}" -c "$ROOT/tests/host_sdl_wav_loader.c" -o "$BUILD/obj/host_sdl_wav_loader.o"
objects+=("$BUILD/obj/host_sdl_wav_loader.o")
"${cc[@]}" "${objects[@]}" -Wl,--wrap=fwrite -Wl,--wrap=fclose -lm -o "$BUILD/host_cpymo"
(cd "$BUILD" && timeout 12s ./host_cpymo "$BUILD/game") 2>&1 | tee "$BUILD/smoke.log"
# Test non-ASCII game paths through the complete game entry and file loader.
mkdir -p "$BUILD/中文目录"
cp -a "$BUILD/game/." "$BUILD/中文目录/"
(cd "$BUILD" && timeout 12s ./host_cpymo "$BUILD/中文目录") 2>&1 | tee "$BUILD/utf8.log"
# Font failure must NOT proceed to framebuffer allocation or interactive loop.
set +e
(cd "$BUILD" && HOST_SIM_TTF_FAIL=1 HOST_SIM_EXPECT_FAILURE=1 ./host_cpymo "$BUILD/game") >"$BUILD/font-failure.log" 2>&1
status=$?
set -e
if [[ "$status" != 255 ]]; then echo "Unexpected font failure exit: $status" >&2; cat "$BUILD/font-failure.log"; exit 1; fi
grep -q 'PASS early failure returned before display initialization' "$BUILD/font-failure.log"
echo "PASS host-only font failure exits without starting screen, exit=$status"
# Invalid game path must fail cleanly as well.
set +e
(cd "$BUILD" && HOST_SIM_EXPECT_FAILURE=1 ./host_cpymo "$BUILD/game-does-not-exist") >"$BUILD/config-failure.log" 2>&1
status=$?
set -e
if [[ "$status" != 255 ]]; then echo "Unexpected missing config exit: $status" >&2; cat "$BUILD/config-failure.log"; exit 1; fi
grep -q 'PASS early failure returned before display initialization' "$BUILD/config-failure.log"
echo "PASS host-only missing gameconfig exits cleanly, exit=$status"

# Fault-inject the actual patched RG35XX present path in the complete host-linked engine.
for fail_mode in LOGICAL_LOCK LCD_LOCK FLIP; do
    set +e
    (cd "$BUILD" && env "HOST_SIM_FAIL_${fail_mode}=1" HOST_SIM_EXPECT_PRESENT_FAILURE=1 \
      timeout 12s ./host_cpymo "$BUILD/game") >"$BUILD/present-failure-${fail_mode}.log" 2>&1
    rc=$?
    set -e
    if [[ "$rc" != 255 ]] || ! grep -q 'PASS present failure detected' "$BUILD/present-failure-${fail_mode}.log"; then
      cat "$BUILD/present-failure-${fail_mode}.log" >&2
      echo "FAIL injected present error $fail_mode, rc=$rc" >&2; exit 1
    fi
    echo "PASS injected present error $fail_mode exits cleanly"
done

# Exercise the genuine CPyMO global save routines, not a mock save module.
(cd "$BUILD" && HOST_SIM_TEST_SAVE=1 timeout 12s ./host_cpymo "$BUILD/game") \
    >"$BUILD/global-save-retry.log" 2>&1
grep -q 'PASS real global save' "$BUILD/global-save-retry.log"
echo 'PASS real global save write-failure retry and reload'
