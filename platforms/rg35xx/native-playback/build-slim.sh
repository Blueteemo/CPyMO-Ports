#!/bin/bash
set -euo pipefail

source_dir="$(cd "$(dirname "$0")" && pwd -P)"
: "${BUILD:?Set BUILD to the output directory}"
: "${FFMPEG_SOURCE:?Set FFMPEG_SOURCE to the FFmpeg n2.6 source tree}"
: "${LEMEDIA_SDK:?Set LEMEDIA_SDK to the LeMedia build sysroot}"
: "${RUNTIME:?Set RUNTIME to the firmware library directory}"
: "${ARM_TOOL_PREFIX:?Set ARM_TOOL_PREFIX to the Arm GNU tool prefix}"
mkdir -p "$BUILD"/{ffmpeg,link-libs,bin}
BUILD="$(cd "$BUILD" && pwd -P)"
FFMPEG_SOURCE="$(cd "$FFMPEG_SOURCE" && pwd -P)"
export FFMPEG_SOURCE LEMEDIA_SDK RUNTIME
export CPYMO_ARM_BUILD="$BUILD"
export CPYMO_ARM_GCC="${ARM_TOOL_PREFIX}gcc"
export PATH="$BUILD/bin:$(dirname "$ARM_TOOL_PREFIX"):$PATH"
export CPYMO_ARM_LINK_LIBS="$BUILD/link-libs"

for library in libc.so.6 libm.so.6 libpthread.so.0 libz.so.1 libdl.so.2 librt.so.1; do
    alias_name="${library%.so.*}.so"
    ln -sf "$RUNTIME/$library" "$CPYMO_ARM_LINK_LIBS/$alias_name"
done

cat > "$BUILD/bin/cpymo-arm-cc" <<'CC'
#!/bin/bash
set -eu
crt="$LEMEDIA_SDK/usr/lib/arm-linux-gnueabihf"
headers="$LEMEDIA_SDK/usr/include"
flags=(-nostdinc -isystem "$("$CPYMO_ARM_GCC" -print-file-name=include)"
    -isystem "$headers/arm-linux-gnueabihf" -isystem "$headers"
    -D__linux__ -D__unix__ -D_DEFAULT_SOURCE -mfloat-abi=hard -mfpu=neon -marm
    "-ffile-prefix-map=$FFMPEG_SOURCE=ffmpeg"
    "-ffile-prefix-map=$CPYMO_ARM_BUILD=build"
    "-ffile-prefix-map=$LEMEDIA_SDK=sysroot")
for arg in "$@"; do
    case "$arg" in
        -c|-E|-S|-MM|-M|-print-*|--version|-dump*)
            exec "$CPYMO_ARM_GCC" "${flags[@]}" "$@"
            ;;
    esac
done
exec "$CPYMO_ARM_GCC" "${flags[@]}" -nostdlib -Wl,--as-needed \
    -Wl,--dynamic-linker=/lib/ld-linux-armhf.so.3 \
    -L"$CPYMO_ARM_LINK_LIBS" -L"$RUNTIME" -Wl,-rpath-link,"$RUNTIME" \
    "$crt/crt1.o" "$crt/crti.o" "$@" \
    -lc -lgcc -l:ld-linux-armhf.so.3 "$crt/libc_nonshared.a" "$crt/crtn.o"
CC
chmod 755 "$BUILD/bin/cpymo-arm-cc"

(
    cd "$BUILD/ffmpeg"
    "$FFMPEG_SOURCE/configure" --enable-cross-compile --arch=arm --cpu=cortex-a9 \
        --target-os=linux --cross-prefix="$(basename "$ARM_TOOL_PREFIX")" \
        --cc=cpymo-arm-cc --as=cpymo-arm-cc --ld=cpymo-arm-cc \
        --disable-all --disable-network --disable-doc --disable-debug --disable-programs \
        --enable-avformat --enable-avcodec --enable-avutil --enable-swresample \
        --enable-static --disable-shared --enable-demuxer=mov --enable-protocol=file \
        --enable-decoder=aac --enable-parser=h264,aac --enable-bsf=h264_mp4toannexb \
        --enable-zlib --disable-bzlib --disable-libvorbis
    "${MAKE:-make}" -j"${JOBS:-8}"
)

FFMPEG_STATIC_BUILD="$BUILD/ffmpeg" BUILD="$BUILD/player" bash "$source_dir/build.sh" arm
"${ARM_TOOL_PREFIX}strip" --strip-unneeded "$BUILD/player/native-video"
shasum -a 256 "$BUILD/player/native-video"
