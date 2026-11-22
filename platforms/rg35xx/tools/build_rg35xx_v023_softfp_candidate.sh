#!/usr/bin/env bash
# Single locally runnable job. NEVER touch the TF card or existing real saves.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
: "${SDK:?Set SDK to the extracted ARM/uClibc SDK directory (arm-miyoo-linux-uclibcgnueabi_sdk-buildroot)}"
BUILD="$ROOT/build_arm_softfp"  # fixed path so provenance/package checks cannot drift
echo '== Host regression: paths, launcher, game loading, menu'
for t in tests/run_garlic_paths_test.sh tests/run_launcher_test.sh tests/run_game_loader_test.sh tests/run_menu_test.sh; do
    echo "RUN $t"
    bash "$t"
done
echo '== Rendering parity regression (pixel-for-pixel)'
bash research/tools/verify_render_parity.sh
echo '== ARM/uClibc v0.23 SOFTFP build'
SDK="$SDK" BUILD="$BUILD" bash tools/build_arm_clang_lld.sh softfp
echo '== Package checked core, NOT historical v0.22 core'
python3 tools/make_rg35xx_v023_softfp_zip.py "$ROOT/CPyMO_RG35XX_v023_SOFTFP_待实机验收.zip"
echo 'All locally available gates passed. The produced ZIP is still NOT hardware-verified.'
