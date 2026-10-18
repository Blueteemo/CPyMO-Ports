#!/usr/bin/env bash
# Host unit test for the shared RG35XX/CPyMO path, save-isolation and
# case-diagnostic helpers (source/CPyMO-main/cpymo-backends/garlicos/
# garlic_paths.c), which the Libretro core uses for §1 and §3 of the v0.23 task.
#
# Pure POSIX C: no SDK, no device, no framework.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build-garlic-paths}"

mkdir -p "$BUILD"
gcc -O1 -g -std=gnu11 -Wall -Wextra -Werror \
  -I"$ROOT/source/CPyMO-main/cpymo-backends/garlicos" \
  "$ROOT/tests/test_garlic_paths.c" \
  "$ROOT/source/CPyMO-main/cpymo-backends/garlicos/garlic_paths.c" \
  -o "$BUILD/test_garlic_paths"

"$BUILD/test_garlic_paths"
echo 'PASS garlic_paths: universal content path, save isolation, case diagnostics, directory scan.'
