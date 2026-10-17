#!/usr/bin/env bash
# Full HOST-ONLY regression; genuine ARM executable is not built by this script.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo '[1/4] Complete CPyMO host integration, startup, save and LCD failure tests'
bash tests/run_all_host.sh
echo '[2/4] Bounded SDL_ttf font cache (ASan+UBSan)'
bash tests/run_v16_cache.sh
echo '[3/4] Read-only PyMO game preflight self-tests'
python3 tests/test_preflight_v16.py
echo '[4/4] Manual-launch script syntax (not executed on firmware)'
sh -n tools/launch_cpymo_stock.sh
echo 'PASS v0.16 host-only suite. ARM engine link and RG35XX hardware validation remain pending.'
