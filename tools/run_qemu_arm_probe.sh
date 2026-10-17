#!/usr/bin/env bash
# Run on local Linux/WSL2 once qemu-user and matching SDK are installed.
# Does not emulate RG35XX GPU, input, firmware or RetroArch.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${CC:?Set CC=/absolute/sdk/bin/arm-miyoo-linux-uclibcgnueabi-gcc}"
if ! command -v qemu-arm >/dev/null 2>&1; then
  echo 'qemu-arm is missing. On Debian/Ubuntu: sudo apt install qemu-user' >&2
  exit 2
fi
SYSROOT="$($CC -print-sysroot)"
[[ -d "$SYSROOT" ]] || { echo 'SDK sysroot unavailable' >&2; exit 3; }
TARGET="${CORE:-$ROOT/target/cpymo_libretro.so}"
PROBE="${PROBE:-$ROOT/target/arm_core_probe}"
[[ -f "$TARGET" && -f "$PROBE" ]] || exit 4
# QEMU user mode runs ARM code under a virtual root lookup; not an RG35XX display emulator.
qemu-arm -L "$SYSROOT" "$PROBE" "$TARGET"
