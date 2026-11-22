#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo '[1/4] v0.16 full-engine host, display/save, font-cache and preflight regression'
bash tests/run_stage_v16.sh
echo '[2/4] RG35XX SDL1 WAV backend host/ASan/UBSan tests'
bash tests/run_wav_audio_host.sh
echo '[3/4] Real ffmpeg conversion of a synthetic test game (source kept unmodified)'
python3 tests/test_audio_convert.py
echo '[4/4] Complete CPyMO engine: script #bgm -> PCM WAV -> host SDL double, ASan/UBSan'
UBSAN_OPTIONS=halt_on_error=1 HOST_SANITIZE=1 bash tests/run_full_engine_wav_host_sim.sh > "$ROOT/full-wav-host-replay.log" 2>&1
if [[ $(grep -c 'HOST_SIM: PASS real game #bgm WAV asset loaded' "$ROOT/full-wav-host-replay.log") -lt 2 ]]; then
  cat "$ROOT/full-wav-host-replay.log" >&2
  echo 'Expected both ordinary and UTF8 game audio paths to load' >&2
  exit 1
fi
grep 'HOST_SIM: PASS real game #bgm WAV asset loaded' "$ROOT/full-wav-host-replay.log"
echo 'PASS v0.17 host stage. ARM SDK / genuine target link / hardware audio remain PENDING.'
