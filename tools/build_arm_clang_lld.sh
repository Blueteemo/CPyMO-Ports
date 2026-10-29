#!/usr/bin/env bash
# Build an original-RG35XX / GarlicOS-1.4.9 ARM/uClibc Libretro core.
# Usage: SDK=/path/to/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot \
#          bash tools/build_arm_clang_lld.sh softfp
# The same script supports Linux and MSYS2; a real SDK/sysroot is mandatory.
set -euo pipefail

MODE="${1:-softfp}"
case "$MODE" in soft|softfp) ;; *) echo 'Usage: build_arm_clang_lld.sh soft|softfp' >&2; exit 2;; esac
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${SDK:-$ROOT/research/local-sdk/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot}"
BUILD="${BUILD:-$ROOT/build_arm_$MODE}"
CLANG="${CLANG:-clang}"
ARM_READELF="${ARM_READELF:-readelf}"
ARM_OBJDUMP="${ARM_OBJDUMP:-llvm-objdump}"
SYSROOT="$SDK/arm-miyoo-linux-uclibcgnueabi/sysroot"
GCCLIB="$SDK/lib/gcc/arm-miyoo-linux-uclibcgnueabi/9.4.0"
SRC="$ROOT/source/CPyMO-main"

fail() { echo "BLOCKED: $*" >&2; exit 3; }
command -v "$CLANG" >/dev/null 2>&1 || fail "missing clang: $CLANG"
command -v "$ARM_READELF" >/dev/null 2>&1 || fail "missing readelf: $ARM_READELF"
command -v "$ARM_OBJDUMP" >/dev/null 2>&1 || fail "missing ARM objdump: $ARM_OBJDUMP"
command -v ld.lld >/dev/null 2>&1 || fail 'missing lld linker'
[ -f "$SYSROOT/usr/include/stdio.h" ] || fail "real ARM/uClibc SDK sysroot not found at $SYSROOT (do not substitute host glibc)"
for f in "$SYSROOT/usr/lib/crti.o" "$SYSROOT/usr/lib/crtn.o" "$SYSROOT/lib/libc.so.0" "$GCCLIB/libgcc.a"; do
    [ -e "$f" ] || fail "incomplete SDK (missing $f)"
done
# Target check, even if the SDK folder has an arbitrary name.
"$ARM_READELF" -h "$SYSROOT/usr/lib/crti.o" | grep -qE 'Machine:.*ARM' || fail 'wrong sysroot architecture'

# Native Linux tools accept POSIX paths. MSYS2 passes Windows paths to native
# clang.exe/ld.lld.exe, which otherwise cannot open /c/... sources/sysroots.
native_path() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
case "$MODE" in
    soft) FPU_FLAGS=(-mfloat-abi=soft) ;;
    softfp) FPU_FLAGS=(-mfloat-abi=softfp -mfpu=vfpv3) ;;
esac
mkdir -p "$BUILD"
# A failed rebuild must not leave a seemingly new candidate from an old run.
rm -f "$BUILD/cpymo_libretro.so" "$BUILD/BUILD_PROVENANCE.txt"
FLAGS=(--target=armv7-unknown-linux-gnueabi --sysroot="$(native_path "$SYSROOT")"
       -mcpu=cortex-a9 "${FPU_FLAGS[@]}" -O2 -DNDEBUG -fPIC -std=gnu11
       -DTEXT_CHARACTER_W_SCALE=1 -DDISABLE_MOVIE
       -DDISABLE_FFMPEG_AUDIO -DGARLIC_NO_FFMPEG
       -DGARLIC_MP3_AUDIO ${EXTRA_DEFS:-} -Wall
       -Werror=implicit-function-declaration)

sources() {
    printf '%s\n' \
       "$SRC/cpymo-backends/libretro/libretro.c" \
       "$SRC/cpymo-backends/libretro/cpymo_audio_mp3.c" \
       "$SRC/cpymo-backends/libretro/stb_vorbis.c" \
       "$SRC/cpymo-backends/garlicos/garlic_adapter.c" \
       "$SRC/cpymo-backends/garlicos/garlic_paths.c" \
       "$SRC/cpymo-backends/software/cpymo_backend_image.c" \
       "$SRC/cpymo-backends/software/cpymo_backend_software.c" \
       "$SRC/cpymo-backends/software/cpymo_backend_text.c" \
       "$SRC/cpymo-backends/software/cpymo_backend_masktrans.c"
    find "$SRC/cpymo" -maxdepth 1 -name '*.c' -type f | sort
}

printf 'building RG35XX original: mode=%s\ncompiler=%s\nsysroot=%s\n' "$MODE" "$CLANG" "$SYSROOT"
OBJECTS=()
i=0
while IFS= read -r source; do
    # Numeric prefix prevents silently overwriting same-basename objects.
    obj="$BUILD/$(printf '%03d' "$i")_$(basename "${source%.c}").o"
    "$CLANG" "${FLAGS[@]}" -c "$(native_path "$source")" -o "$(native_path "$obj")"
    OBJECTS+=("$(native_path "$obj")")
    i=$((i+1))
done < <(sources)
[ "$i" -gt 20 ] || fail "unexpectedly few source files: $i"

OUT="$BUILD/cpymo_libretro.so"
"$CLANG" "${FLAGS[@]}" -shared -fuse-ld=lld -nostdlib \
    -Wl,--hash-style=both \
    "$(native_path "$SYSROOT/usr/lib/crti.o")" \
    "${OBJECTS[@]}" \
    "$(native_path "$SYSROOT/usr/lib/crtn.o")" \
    -Wl,--version-script="$(native_path "$SRC/cpymo-backends/libretro/link.T")" \
    -Wl,--no-undefined \
    -L"$(native_path "$SYSROOT/usr/lib")" -L"$(native_path "$SYSROOT/lib")" \
    -L"$(native_path "$GCCLIB")" \
    -lc -lm -lgcc -o "$(native_path "$OUT")"

python3 "$ROOT/tools/verify_uclibc_core.py" "$OUT"
DYN="$("$ARM_READELF" -d "$OUT")"
for tag in HASH GNU_HASH; do
    printf '%s\n' "$DYN" | grep -q "($tag)" || fail "core missing $tag"
done
ATTR="$("$ARM_READELF" -A "$OUT")"
printf '%s\n' "$ATTR" | grep -q 'Tag_ABI_VFP_args' && fail 'hard-float argument ABI is unsupported on target'
if [ "$MODE" = softfp ]; then
    # Validate OUR OBJECTS too. SDK libgcc can carry older ARM attributes.
    has_vfp=0
    for obj in "$BUILD"/*.o; do
        "$ARM_READELF" -A "$obj" | grep -q 'Tag_FP_arch: VFPv3' && has_vfp=1
    done
    [ "$has_vfp" -eq 1 ] || fail 'softfp objects contain no VFPv3 attribute'
fi
{
    echo "mode=$MODE"
    echo 'target=original-RG35XX/GarlicOS-1.4.9/uClibc/ARM-EABI-soft-call'
    echo "source_git_commit=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "source_tree_sha256=$(python3 "$ROOT/tools/source_fingerprint.py")"
    echo "source_git_dirty=$(if [ -n "$(git -C "$ROOT" status --porcelain 2>/dev/null)" ]; then echo yes; else echo no; fi)"
    echo "compiler=$($CLANG --version | head -1)"
    echo "sysroot=$SYSROOT"
    echo "fpu_flags=${FPU_FLAGS[*]}"
    echo "source_files=$i"
    echo "sha256=$(sha256sum "$OUT" | awk '{print $1}')"
} > "$BUILD/BUILD_PROVENANCE.txt"
cat "$BUILD/BUILD_PROVENANCE.txt"
echo "PASS: built and statically verified $OUT"
echo 'NOTE: This does NOT establish hardware compatibility or measured frame rate.'
