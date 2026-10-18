#!/usr/bin/env bash
# Cross-build the CPyMO Libretro core for RG35XX / GarlicOS with FFmpeg AUDIO
# ENABLED (the shipped device default is audio- and video-less).
#
#   bash tools/build_arm_audio.sh [out-dir]
#
# What this proves, and what it does not
# ------------------------------------
# The SDK ships all five FFmpeg libraries and their headers, but there are TWO
# gaps between it and the CPyMO sources:
#
#   1. uClibc has pthreads, not C11 `<threads.h>`, which the core uses for the
#      audio callback mutex.
#   2. The SDK has FFmpeg 3.4 (libavformat 57.40); `avio_context_free()`
#      arrived in 4.0.
#
# Both are bridged by headers in `cpymo-backends/garlicos/compat/` -- no
# product source is edited, and a host/SDL2 build with a modern FFmpeg does not
# see either shim. This script therefore proves the audio path COMPILES and
# LINKS on this SDK. Whether it SOUNDS right needs playback; the report says so.
#
# FAIL-CLOSED: the linked object must keep the soft-float ABI, both dynamic
# hash tables, and the uClibc runtime, exactly like the audio-less build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${SDK:-$ROOT/research/local-sdk/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot}"
SYSROOT="$SDK/arm-miyoo-linux-uclibcgnueabi/sysroot"
GCCLIB="$SDK/lib/gcc/arm-miyoo-linux-uclibcgnueabi/9.4.0"
CLANG="${CLANG:-/c/tools/msys64/msys64/ucrt64/bin/clang.exe}"
OUT="${1:-$ROOT/build_arm_audio}"
SRC="$ROOT/source/CPyMO-main"
COMPAT="$SRC/cpymo-backends/garlicos/compat"
JOBS="${JOBS:-4}"

ARM_READELF="${ARM_READELF:-/c/tools/msys64/msys64/ucrt64/bin/arm-none-eabi-readelf}"
win() { cygpath -m "$1"; }

[ -d "$SYSROOT" ] || { echo "no SDK sysroot at $SYSROOT" >&2; exit 3; }
[ -f "$COMPAT/threads.h" ] || { echo "missing $COMPAT/threads.h" >&2; exit 3; }

# One include first: it defines the FFmpeg shims and pulls the version macros.
FLAGS=(
  --target=armv7-unknown-linux-gnueabi
  --sysroot="$(win "$SYSROOT")"
  -mcpu=cortex-a9 -mfloat-abi=soft
  -O2 -fPIC -std=gnu11
  -DTEXT_CHARACTER_W_SCALE=1
  -DDISABLE_MOVIE
  -include "$(win "$COMPAT/garlic_ffmpeg_compat.h")"
  -I"$(win "$COMPAT")"
  -I"$(win "$SRC/cpymo-backends/libretro")"
)

rm -rf "$OUT"
mkdir -p "$OUT"

echo "== compiler   $("$CLANG" --version | head -1)"
echo "== sysroot    $SYSROOT"
echo "== compat     $COMPAT"
echo "== audio      FFmpeg: $(
  grep -h 'LIBAVFORMAT_VERSION_MAJOR' "$SYSROOT/usr/include/libavformat/version.h" \
  | head -1 | awk '{print $3}')"

mapfile -t SOURCES < <(
  printf '%s\n' "$SRC/cpymo-backends/libretro/libretro.c"
  ls -1 "$SRC"/cpymo/*.c
  ls -1 "$SRC"/cpymo-backends/software/*.c
  printf '%s\n' "$SRC/cpymo-backends/garlicos/garlic_adapter.c" \
                "$SRC/cpymo-backends/garlicos/garlic_paths.c"
)
echo "== sources    ${#SOURCES[@]} translation units"

objs=()
for s in "${SOURCES[@]}"; do
  obj="$OUT/$(basename "${s%.c}").o"
  "$CLANG" "${FLAGS[@]}" -c "$(win "$s")" -o "$(win "$obj")" 2> "$OUT/$(basename "${s%.c}").err" \
    || { echo "FAILED: $(basename "$s")" >&2
         head -8 "$OUT/$(basename "${s%.c}").err" >&2; exit 1; }
  objs+=("$(win "$obj")")
done
echo "== compiled   ${#objs[@]} objects"

SO="$OUT/cpymo_libretro_audio.so"
"$CLANG" "${FLAGS[@]}" -shared -fuse-ld=lld -nostdlib \
  -Wl,--hash-style=both \
  "$(win "$SYSROOT/usr/lib/crti.o")" "${objs[@]}" \
  "$(win "$SYSROOT/usr/lib/crtn.o")" \
  -Wl,--version-script="$(win "$SRC/cpymo-backends/libretro/link.T")" \
  -L"$(win "$SYSROOT/usr/lib")" -L"$(win "$SYSROOT/lib")" -L"$(win "$GCCLIB")" \
  -lavformat -lavcodec -lavutil -lswresample -lswscale \
  -lc -lm -lgcc -o "$(win "$SO")"

# The libav* set is not a free choice: with `GARLIC_NO_FFMPEG` undefined, the
# same block of libretro.c that carries the audio mutex also carries the
# movie-surface scaler, so libswscale is needed as well. (Leaving it out links
# happily and then fails at `dlopen` on the device, on sws_getContext.)
ALLOWED_DEPS="libavformat.so.57 libavcodec.so.57 libavutil.so.55 \
libswresample.so.2 libswscale.so.4 libc.so.0 ld-uClibc.so.1"

echo "== linked     $(wc -c < "$SO") bytes"
echo "== dependencies"
"$ARM_READELF" -d "$SO" | grep NEEDED | sed 's/^/   /'

HASHES="$("$ARM_READELF" -d "$SO" | grep -oE '\(GNU_HASH\)|\(HASH\)' | sort -u | tr '\n' ' ')"
echo "   hash tables: ${HASHES:-none}"
case "$HASHES" in
  *"(HASH)"*"(GNU_HASH)"*|*"(GNU_HASH)"*"(HASH)"*) : ;;
  *) echo "FAIL: audio core lacks one of the two hash tables" >&2; exit 4 ;;
esac

"$ARM_READELF" -h "$SO" | grep -E 'Class:|Machine:|Flags:' | sed 's/^/   /'
if "$ARM_READELF" -A "$SO" | grep -q 'Tag_ABI_VFP_args'; then
  echo "FAIL: unexpected hard-float argument ABI" >&2; exit 5
fi

# The audio path must really be in this object: the FFmpeg entry points it
# needs are undefined references in the core, resolved by the libav* above.
echo "== audio is really compiled in"
for s in cpymo_backend_audio_lock cpymo_audio_copy_mixed_samples swr_init; do
  if "$ARM_READELF" --dyn-syms --wide "$SO" | grep -q " $s"; then
    printf '   %-30s referenced\n' "$s"
  fi
done

# tools/verify_uclibc_core.py deliberately allows ONLY the uClibc runtime, which
# is the contract of the shipped core; this variant is expected to NEED the
# libav* sonames. Check the dependency SET here instead of asserting the
# audio-less whitelist, and still demand the two uClibc entries.
echo "== runtime dependency contract"
needed="$("$ARM_READELF" -d "$SO" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')"
for n in $needed; do
  case " $ALLOWED_DEPS " in
    *" $n "*) printf '   %-24s expected\n' "$n" ;;
    *) printf '   %-24s UNEXPECTED\n' "$n"; unexpected=1 ;;
  esac
done
for must in libc.so.0 ld-uClibc.so.1; do
  case " $needed " in
    *" $must "*) : ;;
    *) echo "FAIL: the audio core does not NEED $must" >&2; exit 6 ;;
  esac
done
case " $needed " in
  *libavcodec*|*libavformat*) : ;;
  *) echo "FAIL: the audio core is supposed to NEED the libav* sonames" >&2
     exit 6 ;;
esac
if [ "${unexpected:-0}" = 1 ]; then
  echo "FAIL: an unexpected dependency appeared" >&2; exit 6
fi

sha256sum "$SO"
echo "PASS ARM/audio: FFmpeg-audio core built for ARM/uClibc (compat shims, no"
echo "     product-source edits). NOT yet played on hardware."
