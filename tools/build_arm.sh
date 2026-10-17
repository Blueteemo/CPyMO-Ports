#!/usr/bin/env bash
# Genuine target build: hard failure unless a real, matching ARM hard-float glibc/SDL SDK is supplied.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$HERE/source/CPyMO-main"
: "${CC:=arm-linux-gnueabihf-gcc}"
: "${SDK_SYSROOT:?Set SDK_SYSROOT to a genuine ARM glibc 2.15-compatible sysroot (not GarlicOS/uClibc)}"
: "${SDL_INCLUDE_DIR:?Set SDL_INCLUDE_DIR to the parent containing SDL/SDL.h and SDL/SDL_ttf.h}"
: "${SDL_LIB_DIR:?Set SDL_LIB_DIR to firmware-extracted ARM SDL shared objects}"
: "${GLIBC_LIB_DIR:?Set GLIBC_LIB_DIR to firmware-extracted ARM glibc 2.15 shared objects}"
command -v "$CC" >/dev/null || { echo "Missing target compiler: $CC" >&2; exit 3; }
for h in "$SDL_INCLUDE_DIR/SDL/SDL.h" "$SDL_INCLUDE_DIR/SDL/SDL_ttf.h" "$SDK_SYSROOT/usr/include/stdio.h"; do
  test -f "$h" || { echo "Missing real SDK header: $h" >&2; exit 4; }
done
if grep -q 'CPYMO_SYNTAX_ONLY_SDL_H' "$SDL_INCLUDE_DIR/SDL/SDL.h"; then
  echo 'Refusing to link a program compiled with the compile-only SDL stub' >&2; exit 5
fi
for f in "$GLIBC_LIB_DIR/libc.so.6" "$GLIBC_LIB_DIR/libm.so.6" "$SDL_LIB_DIR/libSDL.so" "$SDL_LIB_DIR/libSDL_ttf.so"; do
 test -e "$f" || { echo "Missing genuine ARM library: $f" >&2; exit 6; }
done
for f in "$GLIBC_LIB_DIR/libc.so.6" "$SDL_LIB_DIR/libSDL.so" "$SDL_LIB_DIR/libSDL_ttf.so"; do
 readelf -h "$f" | grep -q 'Machine:.*ARM' || { echo "Non-ARM library: $f" >&2; exit 7; }
done
for s in "$GLIBC_LIB_DIR/libc.so.6" "$SDL_LIB_DIR/libSDL.so"; do
 readelf -h "$s" | grep -q 'ELF32' || { echo "Not ELF32: $s" >&2;exit 8;}
done
BUILD="$HERE/build-arm"
mkdir -p "$BUILD/obj"
FLAGS=(--sysroot="$SDK_SYSROOT" -std=gnu11 -O2 -fno-common -DCPYMO_RG35XX_STOCK \
 -DENABLE_SDL_TTF=2 -DDISABLE_MOVIE \
 -DDISABLE_FFMPEG_MOVIE -DSCREEN_BPP=16 \
 -Werror=implicit-function-declaration -Werror=implicit-int \
 -mfpu=vfpv3-d16 -mfloat-abi=hard -march=armv7-a \
 -I"$SDL_INCLUDE_DIR" -I"$REPO/cpymo")
AUDIO_MODE="${AUDIO_MODE:-wav}"
case "$AUDIO_MODE" in
  wav) AUDIO_CFLAGS=(-DCPYMO_RG35XX_WAV_AUDIO -DDISABLE_FFMPEG_AUDIO) ;;
  off) AUDIO_CFLAGS=(-DDISABLE_AUDIO -DDISABLE_FFMPEG_AUDIO) ;;
  *) echo "Unsupported AUDIO_MODE=$AUDIO_MODE (choose wav or off)" >&2; exit 9 ;;
esac
objects=()
for src in "$REPO"/cpymo/*.c "$REPO"/cpymo-backends/sdl1/*.c; do
 obj="$BUILD/obj/$(basename "$src" .c).o"
 echo "Compiling $(basename "$src")"
 "$CC" "${FLAGS[@]}" "${AUDIO_CFLAGS[@]}" -c "$src" -o "$obj"
 objects+=("$obj")
done
# Link against *stock firmware* SDL1 + SDL_ttf, not an unrelated SDL2 or uClibc runtime.
"$CC" --sysroot="$SDK_SYSROOT" -mfpu=vfpv3-d16 -mfloat-abi=hard \
 -Wl,--dynamic-linker,/lib/ld-linux-armhf.so.3 \
 "${objects[@]}" -L"$SDL_LIB_DIR" -L"$GLIBC_LIB_DIR" \
 -Wl,-rpath-link,"$SDL_LIB_DIR" -Wl,-rpath-link,"$GLIBC_LIB_DIR" \
 -lSDL_ttf -lSDL -lm -ldl -lpthread -o "$BUILD/cpymo.dge"
python3 "$HERE/tools/verify_target_elf.py" "$BUILD/cpymo.dge"
echo "ARM engine candidate: $BUILD/cpymo.dge (NOT hardware-verified, AUDIO_MODE=$AUDIO_MODE; wav=PCM WAV only, not MP3)"
