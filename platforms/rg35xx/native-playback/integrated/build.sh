#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")"; pwd -P)"
: "${SDK:?Set SDK to the ARM/uClibc toolchain directory}"
: "${ARM_TOOL_PREFIX:?Set ARM_TOOL_PREFIX to the Arm GNU bin prefix}"
src="$root/../../source/CPyMO-main"
native="$(cd "$root/.."; pwd -P)"
sysroot="$SDK/arm-miyoo-linux-uclibcgnueabi/sysroot"
gcc_lib="$SDK/lib/gcc/arm-miyoo-linux-uclibcgnueabi/9.4.0"
build="${BUILD:-$root/build}"
mkdir -p "$build"
options=(--target=armv7-unknown-linux-gnueabi --sysroot="$sysroot"
    -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=vfpv3 -O2 -DNDEBUG -fPIC
    -std=gnu11 -Wall -Werror=implicit-function-declaration
    -DTEXT_CHARACTER_W_SCALE=1 -DDISABLE_MOVIE -DDISABLE_FFMPEG_AUDIO
    -DGARLIC_NO_FFMPEG -DGARLIC_MP3_AUDIO -DGARLIC_EXPERIMENTAL_MOVIE
    -DGARLIC_NATIVE_MOVIE -I"$native" -I"$src/cpymo" -I"$src/cpymo-backends/libretro")
sources=("$src"/cpymo-backends/libretro/{libretro,cpymo_audio_mp3,stb_vorbis}.c
         "$src"/cpymo-backends/garlicos/{garlic_adapter,garlic_paths}.c
         "$src"/cpymo-backends/software/*.c "$src"/cpymo/*.c "$native/client.c")
objects=()
for source in "${sources[@]}"; do
    obj="$build/$(basename "${source%.c}").o"
    "${CLANG:-clang}" "${options[@]}" -c "$source" -o "$obj"
    objects+=("$obj")
done
"${ARM_TOOL_PREFIX}ld" -m armelf --sysroot="$sysroot" -shared -z noexecstack \
    --hash-style=both --no-undefined --version-script="$src/cpymo-backends/libretro/link.T" \
    "$sysroot/usr/lib/crti.o" "${objects[@]}" "$sysroot/usr/lib/crtn.o" \
    -L"$sysroot/usr/lib" -L"$sysroot/lib" -L"$gcc_lib" -lc -lm -lgcc \
    -o "$build/cpymo_libretro.so"
"${ARM_TOOL_PREFIX}readelf" -hAd "$build/cpymo_libretro.so"
shasum -a 256 "$build/cpymo_libretro.so"
