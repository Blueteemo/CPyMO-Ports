#!/usr/bin/env python3
"""Self-test for tools/parse_device_perf2.py.

The point of these assertions is the WEIGHTING. Every CPYMO_PERF version
reports `draw_us_avg` and `convert_us_avg` per REDRAW and `update_us_avg` per
FRAME, over records that cover very different intervals (a 6-draw menu
interval, a 100-draw opening). Summing the per-record averages and dividing by
the total redraws then answers a different, meaningless question -- on the 10.19
device log it yields 8.324 ms where the hand-written report says 117.117 ms
for exactly the same records. The numbers below are pinned against that report.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "perf2", ROOT / "tools/parse_device_perf2.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

failures = 0


def check(cond: bool, msg: str) -> None:
    global failures
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond:
        failures += 1


# --- 1. the real 10.19 v0.22 device log -------------------------------
text = (ROOT / "10.19开发任务/CPyMO_RG35XX_uClibc_Test.log").read_text(
    encoding="utf-8", errors="replace")
rows = m.parse_perf(text)
check(len(rows) == 81, f"81 CPYMO_PERF records in the device log ({len(rows)})")
s = m.summarize(rows)
check(s["retro_run_calls"] == 14433, f"14433 retro_run calls ({s['retro_run_calls']})")
check(s["redraws"] == 766, f"766 redraws ({s['redraws']})")
check(abs(s["weighted_draw_ms"] - 117.117) < 0.05,
      f"weighted draw is 117.117 ms/redraw ({s['weighted_draw_ms']})")
check(abs(s["weighted_convert_ms"] - 5.128) < 0.05,
      f"weighted convert matches the report ({s['weighted_convert_ms']})")
check(abs(s["accounted_percent"] - 28.68) < 0.2,
      f"accounted share is 28.68% ({s['accounted_percent']})")
check("outside_us_avg" not in rows[0],
      "a v0.22 line has no outside/video/audio fields")
check("weighted_outside_ms" not in s,
      "so the summary omits them rather than inventing zeros")

# --- 2. the naive weighting really is different (the regression guard) --
naive = sum(r.get("draw_us_avg", 0) for r in rows) / s["redraws"] / 1000
check(abs(naive - 8.324) < 0.05,
      f"the naive mean-of-averages would say {naive:.3f} ms -- must not be used")
check(abs(naive - s["weighted_draw_ms"]) > 50,
      "the two weightings differ by an order of magnitude on this log")

# --- 3. the v0.21 excerpt, which a different parser pins at 1200-1700 --
v21 = (ROOT / "research/logs/real_rg35xx_v021_perf_excerpt.txt").read_text(
    encoding="utf-8")
s21 = m.summarize(m.parse_perf(v21))
check(s21["retro_run_calls"] == 1773, f"v0.21 excerpt frame count ({s21['retro_run_calls']})")
check(1200 < s21["weighted_draw_ms"] < 1700,
      f"v0.21 weighted draw stays in the established band ({s21['weighted_draw_ms']})")
check(s21["effective_retro_run_hz"] < 7,
      f"v0.21 rate (~6/s) ({s21['effective_retro_run_hz']})")

# --- 4. the new fields are read when the core provides them ----------
synthetic = "\n".join([
    "CPYMO_PERF v0.21 screen=menu frames=180 draws=2 elapsed_ms=1000 "
    "fps_x10=1800 update_us_avg=100 draw_us_avg=200000 convert_us_avg=5000 "
    "outside_us_avg=3000 video_us_avg=8000 audio_us_avg=700",
])
r = m.parse_perf(synthetic)
check(len(r) == 1 and r[0]["audio_us_avg"] == 700,
      "the extended line parses (v0.23 core)")
check(r[0]["outside_us_avg"] == 3000, "outside_us is captured")

# The v0.21 parser must SKIP the new `screen=` word, not be confused by it:
# it is not a number and has no '=' value the regex would swallow.
check(r[0]["frames"] == 180 and r[0]["fps_x10"] == 1800,
      "a leading non-numeric field does not shift the others")

# --- 5. the load report -------------------------------------------
loadtext = ("CPYMO_LOAD gamedir=/media/APPS/Game ok=1 total_us=250000 "
            "engine_us=200000 font_us=40000 alloc_us=10000 canvas=1280x720")
loads = m.parse_load(loadtext)
check(len(loads) == 1, "the CPYMO_LOAD line parses")
check(loads and loads[0]["total_us"] == 250000, "total_us is captured")
check(loads and loads[0]["canvas"] == (1280, 720), "canvas size is captured")
check(m.parse_load("nothing here") == [], "a log without loads yields no rows")

print()
print(f"{'FAIL' if failures else 'PASS'} test_parse_perf2 "
      f"({failures} failure{'s' if failures != 1 else ''})")
raise SystemExit(1 if failures else 0)
