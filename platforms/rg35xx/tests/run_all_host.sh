#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HERE/results"
echo '[1/4] Real CPyMO core game loader, 40 units'
bash "$HERE/tests/build_test.sh" 2>&1 | tee "$HERE/results/core-and-game-io.log"
echo '[2/4] Full SDL1 backend compile with HOST-ONLY declaration shim, 12 units'
bash "$HERE/tests/compile_backend_syntax.sh" 2>&1 | tee "$HERE/results/backend-syntax.log"
echo '[3/4] Full CPyMO host program link, synthetic image/text/gameplay/failure paths + ASan/UBSan'
HOST_SANITIZE=1 bash "$HERE/tests/run_full_engine_host_sim.sh" 2>&1 | tee "$HERE/results/full-host-simulation.log"
echo '[4/4] Real ARM SDK readiness probe (a missing SDK is a known BLOCKER, not a smoke test failure)'
python3 "$HERE/tools/check_build_readiness.py" | tee "$HERE/results/sdk-status.log"
echo 'PASS host-only integration suite. Real ARM SDK/link + hardware test remain outstanding.'
