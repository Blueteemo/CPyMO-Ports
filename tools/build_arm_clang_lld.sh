#!/usr/bin/env bash
# Cross-compile the CPyMO Libretro core for RG35XX / GarlicOS (ARM + uClibc).
#
#   bash tools/build_arm_clang_lld.sh soft
#   bash tools/build_arm_clang_lld.sh softfp
#   BUILD=/tmp/out bash tools/build_arm_clang_lld.sh softfp
#
# Why clang+lld instead of the SDK's own gcc
# ----------------------------------------
# The official MiyooCFW SDK for this target (Actions run 36049439393) ships a
# LINUX-host GCC: its `toolchain-wrapper`, `cc1/cc1plus`, `ld` and `as` are all
# x86-64 ELF. On a Windows host they cannot run, while the SDK still holds
# everything a cross LINK needs: the uClibc sysroot (headers, crt objects,
# `libc.so.0`, `ld-uClibc.so.1`, `uclibc_nonshared.a`) and `libgcc.a`.
# A native-Windows clang plus `-fuse-ld=lld` consumes that sysroot directly, and
# produces a core whose dynamic dependencies are the real firmware's names.
#
# Float ABI: `soft` and `softfp` here mean exactly what they mean in the
# handoff: both stay in the soft-float CALLING convention (no `Tag_ABI_VFP_args`
# in the ELF), and `softfp` additionally emits VFPv3 instructions. Compare with
# research/tools/softfp_call_evidence.sh.
#
# FAIL-CLOSED: after linking, the output is checked with
# tools/verify_uclibc_core.py and the ARM attributes are printed.
set -euo pipefail

MODE="${1:-soft}"
case "$MODE" in
  soft|softfp) ;;
  *) echo "usage: $0 soft|softfp" >&2; exit 2 ;;
esac

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${SDK:-$ROOT/research/local-sdk/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot}"
BUILD="${BUILD:-$ROOT/build_arm_$MODE}"
CLANG="${CLANG:-/c/tools/msys64/msys64/ucrt64/bin/clang.exe}"
LLD="${LLD:-/c/tools/msys64/msys64/ucrt64/bin/ld.lld.exe}"

SYSROOT="$SDK/arm-miyoo-linux-uclibcgnueabi/sysroot"
GCCLIB="$SDK/lib/gcc/arm-miyoo-linux-uclibcgnueabi/9.4.0"

# clang is a native Windows binary: give it Windows-form paths with forward
# slashes (the checkout path contains no characters that need quoting).
win() { cygpath -m "$1"; }

# `-mfloat-abi=softfp -mfpu=vfpv3` is the experiment under test in phase B;
# plain `soft` is the v0.21 shipping configuration.
case "$MODE" in
  soft)   FPU_FLAGS=(-mfloat-abi=soft) ;;
  softfp) FPU_FLAGS=(-mfloat-abi=softfp -mfpu=vfpv3) ;;
esac

if [ ! -d "$SYSROOT" ]; then
  echo "ERROR: no SDK sysroot at $SYSROOT" >&2
  echo "       run: bash research/tools/prepare_sdk.sh 'SDK (uClibc).zip' [dir]" >&2
  exit 3
fi
# `tar` on Windows cannot create the SDK's symlinks; the linker needs
# lib/ld-uClibc.so.1 in particular.
if [ ! -e "$SYSROOT/lib/ld-uClibc.so.1" ]; then
  echo "== repairing SDK symlinks (missing lib/ld-uClibc.so.1)"
  python3 "$ROOT/research/tools/repair_sdk_symlinks.py" "$SDK" || true
fi

SRC="$ROOT/source/CPyMO-main"
mkdir -p "$BUILD"
echo "== mode            $MODE"
echo "== compiler        $("$CLANG" --version | head -1)"
echo "== sysroot         $SYSROOT"
echo "== fpu flags       ${FPU_FLAGS[*]}"
echo "== build dir       $BUILD"

sources() {
  printf '%s\n' \
    "$SRC/cpymo-backends/libretro/libretro.c" \
    "$SRC/cpymo-backends/garlicos/garlic_adapter.c" \
    "$SRC/cpymo-backends/software/cpymo_backend_image.c" \
    "$SRC/cpymo-backends/software/cpymo_backend_software.c" \
    "$SRC/cpymo-backends/software/cpymo_backend_text.c" \
    "$SRC/cpymo-backends/software/cpymo_backend_masktrans.c"
  ls -1 "$SRC/cpymo"/*.c
}

# Same compile definitions and link script the project's CMake target uses, so the
# resulting core is comparable with the v0.21 device build.
COMMON_FLAGS=(
  --target=armv7-unknown-linux-gnueabi
  --sysroot="$(win "$SYSROOT")"
  -mcpu=cortex-a9
  "${FPU_FLAGS[@]}"
  -O2 -DNDEBUG -fPIC -std=gnu11
  -DTEXT_CHARACTER_W_SCALE=1
  -Wall
)
COMMON_FLAGS+=(-DDISABLE_AUDIO -DDISABLE_MOVIE -DGARLIC_NO_FFMPEG)

OBJECTS=()
rm -f "$BUILD"/*.o
while IFS= read -r src; do
  obj="$BUILD/$(basename "${src%.c}").o"
  "$CLANG" "${COMMON_FLAGS[@]}" -c "$(win "$src")" -o "$(win "$obj")"
  OBJECTS+=("$(win "$obj")")
done < <(sources)

OUT="$BUILD/cpymo_libretro.so"
"$CLANG" "${COMMON_FLAGS[@]}" -shared \
  -fuse-ld=lld \
  -nostdlib \
  "$(win "$SYSROOT/usr/lib/crti.o")" \
  "${OBJECTS[@]}" \
  "$(win "$SYSROOT/usr/lib/crtn.o")" \
  -Wl,--version-script="$(win "$SRC/cpymo-backends/libretro/link.T")" \
  -Wl,--no-undefined \
  -L"$(win "$SYSROOT/usr/lib")" -L"$(win "$SYSROOT/lib")" \
  -L"$(win "$GCCLIB")" \
  -lc -lm -lgcc \
  -o "$(win "$OUT")"

echo "== linked $(wc -c < "$OUT") bytes"

# --- verification -----------------------------------------------------------
ARM_READELF="${ARM_READELF:-/c/tools/msys64/msys64/ucrt64/bin/arm-none-eabi-readelf}"
ARM_OBJDUMP="${ARM_OBJDUMP:-/c/tools/msys64/msys64/ucrt64/bin/arm-none-eabi-objdump}"

# The per-OBJECT attributes are the ones that show our codegen: the SDK's
# `libgcc.a` members are all `Tag_CPU_arch: v5TEJ`, so a linked-object dump
# would report v5TEJ even though every core object here says v7. Likewise the
# softfp object must show `Tag_FP_arch: VFPv3` while NOT gaining
# `Tag_ABI_VFP_args` (which would mean the hard-float calling convention).
echo "== object-level codegen evidence"
for obj in "${OBJECTS[@]}"; do
  base="$(basename "$obj")"
  attrs="$("$ARM_READELF" -A "$obj" | grep -E 'Tag_(CPU_name|CPU_arch|CPU_arch_profile|FP_arch|ABI_VFP_args)' \
           | tr -s ' ' | paste -sd';' -)"
  insns="$("$ARM_OBJDUMP" -m arm -d "$obj" | grep -cE '\bv(mul|add|div|cvt|mov|ldr|str)\.[fsd][0-9]+' || true)"
  printf '   %-34s vfp_insns=%-6s %s\n' "$base" "$insns" "$attrs"
done

# The whole-.so dump is still worth recording: it is what the device loader and
# the project's verifier see.
echo "== linked object"
"$ARM_READELF" -h "$OUT" | grep -E 'Class:|Machine:|Type:|Flags:' | sed 's/^/   /'
"$ARM_READELF" -A "$OUT" | grep -E 'Tag_(CPU_arch|CPU_arch_profile|FP_arch|ABI_VFP_args)' | sed 's/^/   /' || true
"$ARM_READELF" -d "$OUT" | grep NEEDED | sed 's/^/   /'
sha256sum "$OUT"
python3 "$ROOT/tools/verify_uclibc_core.py" "$OUT"
echo "PASS ARM/$MODE: ARM/uClibc core built with clang+lld; ABI verified."
echo "NOTE: no RG35XX hardware run yet for this artifact."
