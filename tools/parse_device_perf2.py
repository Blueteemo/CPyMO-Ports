#!/usr/bin/env python3
"""Parse and summarise the v0.23 `CPYMO_LOAD` and extended `CPYMO_PERF` lines.

    python3 tools/parse_device_perf2.py <log>

Why a second parser instead of editing the first
--------------------------------------------
`tools/parse_device_perf.py` requires EXACTLY the 7 v0.21 fields, and
`research/tools/analyze_perf_v021.py` requires its 8 (including frames). Both
still work on a v0.22 device log, and both are used as historical evidence, so
they are left alone. This parser accepts the v0.21/v0.22 core AND the v0.23
one, which adds:

    outside_us_avg   wall time the interval did not attribute to any CPyMO
                     stage (RetroArch video/audio, the libretro calls, the
                     clock boundaries) -- the 71.3% the 10.19 log left open
    video_us_avg     inside video_cb
    audio_us_avg     inside cpymo_audio_copy_mixed_samples + audio_batch_cb

The extra fields are reported when present and simply omitted when absent, so
this file can be pointed at the user's existing v0.21 and v0.22 logs too.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

INT = r"(\d+)"


def parse_perf(text: str) -> list[dict[str, int]]:
    rows = []
    for line in text.splitlines():
        if "CPYMO_PERF" not in line:
            continue
        values = {k: int(v) for k, v in re.findall(r"([a-z0-9_]+)=(\d+)", line)}
        if "frames" in values and "draws" in values and "elapsed_ms" in values:
            rows.append(values)
    return rows


def parse_load(text: str) -> list[dict[str, object]]:
    rows = []
    for line in text.splitlines():
        if "CPYMO_LOAD" not in line:
            continue
        row: dict[str, object] = {}
        m = re.search(r"gamedir=(\S+)", line)
        if m:
            row["gamedir"] = m.group(1)
        for k in ("ok", "total_us", "engine_us", "font_us", "alloc_us"):
            m = re.search(rf"{k}=(\d+)", line)
            if m:
                row[k] = int(m.group(1))
        m = re.search(r"canvas=(\d+)x(\d+)", line)
        if m:
            row["canvas"] = (int(m.group(1)), int(m.group(2)))
        rows.append(row)
    return rows


def summarize(rows: list[dict[str, int]]) -> dict[str, object]:
    """Weight each record by what it measured, never by the number of records.

    In every CPYMO_PERF version, `draw_us_avg` and `convert_us_avg` are
    per-DRAW averages while `update_us_avg` is per-FRAME, and the records
    cover wildly different intervals (a 6-draw menu interval and a 96-draw
    opening interval). So a stage's share must be

        sum(draws_i * draw_us_avg_i) / sum(draws_i)

    -- summing the per-draw averages and dividing by the record count (or by
    the draws of a different interval) silently answers a different question.
    Verified against the 10.19 device log, which the hand-written report
    summarised as 117.117 ms of weighted draw: this reproduces it.
    """
    def total(field: str, weight: str) -> int:
        return sum(r.get(field, 0) * r.get(weight, 0) for r in rows)

    frames = sum(r.get("frames", 0) for r in rows)
    draws = sum(r.get("draws", 0) for r in rows)
    elapsed = sum(r.get("elapsed_ms", 0) for r in rows)
    out: dict[str, object] = {
        "samples": len(rows),
        "retro_run_calls": frames,
        "redraws": draws,
        "measured_elapsed_ms": elapsed,
        "effective_retro_run_hz": round(frames * 1000 / elapsed, 3) if elapsed else 0,
    }
    if not draws or not frames:
        return out

    out["weighted_draw_ms"] = round(total("draw_us_avg", "draws") / draws / 1000, 3)
    out["weighted_convert_ms"] = round(
        total("convert_us_avg", "draws") / draws / 1000, 3)
    out["weighted_update_ms"] = round(
        total("update_us_avg", "frames") / frames / 1000, 3)
    for key, name in (("outside_us_avg", "weighted_outside_ms"),
                      ("video_us_avg", "weighted_video_ms"),
                      ("audio_us_avg", "weighted_audio_ms")):
        if key in rows[0]:
            out[name] = round(total(key, "frames") / frames / 1000, 3)

    accounted_us = (total("update_us_avg", "frames")
                   + total("draw_us_avg", "draws")
                   + total("convert_us_avg", "draws")
                   + total("video_us_avg", "frames")
                   + total("audio_us_avg", "frames"))
    if elapsed:
        out["accounted_percent"] = round(accounted_us / 1000 / elapsed * 100, 2)
    if "outside_us_avg" in rows[0]:
        outside_us = total("outside_us_avg", "frames")
        if elapsed:
            out["outside_percent"] = round(outside_us / 1000 / elapsed * 100, 2)
    return out


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: parse_device_perf2.py <log>", file=sys.stderr)
        return 2
    text = Path(argv[1]).read_text(encoding="utf-8", errors="replace")

    loads = parse_load(text)
    if loads:
        print("== CPYMO_LOAD (one row per game load) ==")
        for r in loads:
            canvas = r.get("canvas")
            print(f"  ok={r.get('ok','?')} total={(r.get('total_us',0))/1000:8.3f}ms "
                  f"engine={(r.get('engine_us',0))/1000:8.3f}ms "
                  f"font={(r.get('font_us',0))/1000:7.3f}ms "
                  f"alloc={(r.get('alloc_us',0))/1000:6.3f}ms "
                  f"canvas={canvas} {r.get('gamedir','')}")
        print()

    rows = parse_perf(text)
    if not rows:
        print("no CPYMO_PERF rows", file=sys.stderr)
        return 1 if not loads else 0
    for k, v in summarize(rows).items():
        print(f"{k}: {v}")
    if "outside_us_avg" in rows[0]:
        names = [n for n, k in (("outside", "outside_us_avg"),
                                 ("video", "video_us_avg"),
                                 ("audio", "audio_us_avg")) if k in rows[0]]
        print(f"\nnote: this core reports per-frame {', '.join(names)}; "
              "`outside` is what no CPyMO stage accounted for.")
    else:
        print("\nnote: a v0.21/v0.22-style line; no outside/video/audio fields.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
