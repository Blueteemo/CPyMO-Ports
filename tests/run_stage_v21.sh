#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$ROOT/tests/test_blit_v21.py"
python3 "$ROOT/tests/check_v21_assets.py"
# v0.23 additions: the portable path/save helpers, and loading games that are
# not the shipped test game (three logical resolutions).
bash "$ROOT/tests/run_garlic_paths_test.sh"
python3 "$ROOT/tests/test_v023_fixture.py"
bash "$ROOT/tests/run_game_loader_test.sh"
BUILD="${BUILD:-$ROOT/build-stage-v21}"
BUILD="$BUILD" bash "$ROOT/tests/run_stage_v20.sh"
python3 "$ROOT/tools/verify_uclibc_core.py" "$ROOT/target/cpymo_libretro.so" >/dev/null
# Perf parser only needs a synthetic log. Do not mistake host perf for RG35XX metrics.
echo 'CPYMO_PERF v0.21 frames=180 draws=9 elapsed_ms=9000 fps_x10=200 update_us_avg=1200 draw_us_avg=9200 convert_us_avg=2500' > "$BUILD/host_perf.log"
python3 "$ROOT/tools/parse_device_perf.py" "$BUILD/host_perf.log" | grep -q 'wall_fps=20.0'
sh -n "$ROOT/deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test.sh"
echo 'PASS v0.21+v0.23: RGB565 parity, locale font, host loader, target ABI, instrumentation parser.'
