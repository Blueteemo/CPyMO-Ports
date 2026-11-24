#!/bin/bash
set -euo pipefail
source_dir="$(cd "$(dirname "$0")"; pwd -P)"
: "${FFMPEG_SOURCE:?Set FFMPEG_SOURCE to the FFmpeg 2.6 source tree}"
: "${BUILD:?Set BUILD to the output directory}"
mkdir -p "$BUILD"
ffmpeg_libs="${FFMPEG_STATIC_BUILD:-$FFMPEG_SOURCE}"
if [ "${1:-arm}" = host ]; then
    neon=()
    case "$(uname -m)" in arm64|aarch64) neon=(-DNATIVE_NEON "$source_dir/blit_neon.c") ;; esac
    clang -std=gnu11 -O2 -Wall -Wextra -I"$FFMPEG_SOURCE" -I"$ffmpeg_libs" -Wno-deprecated-declarations \
        "$source_dir/worker.c" "$source_dir/host_video.c" "$source_dir/blit.c" "${neon[@]}" \
        "$ffmpeg_libs"/{libavformat,libavcodec,libswresample,libavutil}/*.a \
        -lm -liconv -lz -pthread -o "$BUILD/native-video-host"
    exit
fi
: "${LEMEDIA_SDK:?Set LEMEDIA_SDK to the LeMedia image sysroot}"
: "${RUNTIME:?Set RUNTIME to the matching firmware libraries}"
: "${ARM_TOOL_PREFIX:?Set ARM_TOOL_PREFIX to the Arm GNU tool prefix}"
crt="$LEMEDIA_SDK/usr/lib/arm-linux-gnueabihf"
includes="$LEMEDIA_SDK/usr/include"
gcc="${ARM_TOOL_PREFIX}gcc"
arm_flags=(-nostdinc -isystem "$("$gcc" -print-file-name=include)"
    -isystem "$includes/arm-linux-gnueabihf" -isystem "$includes" -idirafter "$FFMPEG_SOURCE"
    -idirafter "$ffmpeg_libs"
    -D__linux__ -D_DEFAULT_SOURCE -DNATIVE_NEON -march=armv7-a -mfloat-abi=hard -marm
    -fno-short-enums -O2 -Wall -Wextra -Werror=implicit-function-declaration -Wno-deprecated-declarations)
for file in worker vde_video blit; do
    "$gcc" "${arm_flags[@]}" -mfpu=vfpv3-d16 -c "$source_dir/$file.c" -o "$BUILD/$file.o"
done
"$gcc" "${arm_flags[@]}" -mfpu=neon -c "$source_dir/blit_neon.c" -o "$BUILD/blit_neon.o"
if [ -n "${FFMPEG_STATIC_BUILD:-}" ]; then
    av_libs=(--start-group "$ffmpeg_libs"/{libavformat,libavcodec,libswresample,libavutil}/*.a --end-group
        -l:libm.so.6 -l:librt.so.1 -l:libz.so.1)
else
    av_libs=(-l:libavformat.so.56 -l:libavcodec.so.56 -l:libswresample.so.1 -l:libavutil.so.54)
fi
"${ARM_TOOL_PREFIX}ld" -m armelf -Ttext-segment=0x10000 -z noexecstack \
    --dynamic-linker /lib/ld-linux-armhf.so.3 -rpath-link "$RUNTIME" \
    "$crt/crt1.o" "$crt/crti.o" "$BUILD/worker.o" "$BUILD/vde_video.o" "$BUILD/blit.o" "$BUILD/blit_neon.o" \
    -L"$RUNTIME" "${av_libs[@]}" \
    -l:libpthread.so.0 -l:libdl.so.2 -l:libc.so.6 -l:ld-linux-armhf.so.3 \
    -l:libgcc_s.so.1 "$crt/libc_nonshared.a" "$crt/crtn.o" -o "$BUILD/native-video"
"${ARM_TOOL_PREFIX}readelf" -hAd "$BUILD/native-video"
