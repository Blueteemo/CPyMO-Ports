: 'Cross-compile the real CPyMO Libretro core for ARM/uClibc from this Windows host.

Approach (and why)
------------------
The handoff's official MiyooCFW SDK is a LINUX-host x86-64 GCC 9.4.0 (its
toolchain-wrapper, ld and as are all x86-64 ELF), so it cannot run in this
Windows environment without a Linux kernel. The SDK still contains exactly what a
cross-link needs: the ARM target compiler (cc1/cc1plus), the assembler/Linker for
AArch32 (GNU as can target ARM) and the complete uClibc sysroot.

So: drive CLANG (native Windows) as the C compiler, but use the SDK sysroot and
link with `-fuse-ld=lld`. The .so therefore uses `libc.so.0` and
`ld-uClibc.so.1`, matching the real firmware.

This script is FAIL-CLOSED: it validates the resulting ELF with the project's
tools/verify_uclibc_core.py.

Usage
-----
    bash tools/build_arm_clang_lld.sh soft
    bash tools/build_arm_clang_lld.sh softfp
'
set -euo pipefail

MODE="${1:-soft}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SDK="${SDK:-$ROOT/research/local-sdk/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot}"
CC_BIN="${CSDK_CC:-${SDK}/bin/arm-miyoo-linux-uclibcgnueabi-gcc}"
SYSROOT="${SDK}/arm-miyoo-linux-uclibcgnueabi/sysroot"
CLANG="${CLANG:-/c/tools/msys64/msys64/ucrt64/bin/clang.exe}"
LLD="${LLD:-/c/tools/msys64/msys64/ucrt64/bin/ld.lld.exe}"

# MSYS2 passes Windows-form paths to a native .exe; the SDK directory contains `~`
# and `+` risk, so quote everything and pass forward-slash Windows paths.
win() { cygpath -w "$1" 2>/dev/null || printf '%s' "$1"; }

echo "== ARM/uClibc cross build (clang + lld), mode=$MODE"
echo "== sysroot: $SYSROOT"

mkdir -p "$ROOT/research/build_arm_clang/$MODE"
cd "$ROOT/research/build_arm_clang/$MODE"

cp "$SDK/arm-miyoo-linux-uclibcgnueabi/include/linux" "$S?" 2>/dev/null || true
echo "$SDK" > sdk-path.txt
echo "$CLANG" > compiler-path.txt
"$CLANG" --version 2>&1 | head -2 >> compiler-path.txt
echo "sysroot/host anchors:" >> compiler-path.txt
echo "  toolchain-wrapper is Linux x86-64: $(file "$SDK/bin/toolchain-wrapper" 2>/dev/null | head -1)" >> compiler-path.txt
