#!/usr/bin/env python3
"""The package must CONTAIN what the reports tell a reviewer to open.

An independent review found that the reports instructed the reviewer to parse
`10.19开发任务/CPyMO_RG35XX_uClibc_Test.log` while that file was not in the ZIP,
and that `dist/SHA256SUMS` -- which the README calls the authority -- was also
absent. Both were true, and neither could be caught by `sha256sum -c`, because
a manifest can only check the entries it lists.

So this checks the FORWARD direction: named "must be inside" files are inside.
It is deliberately a small explicit list, not a rule, so adding a requirement
to the README means adding it here.
"""
from __future__ import annotations

import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

MUST_BE_IN_PACKAGE = [
    # The requirement documents the phase answers to.
    ("research/研究任务与性能结论.md", "the handoff's research brief"),
    ("10.19开发任务/正式客户端第一阶段开发任务.md", "the 19 Oct task document"),
    # The baseline the numbers are measured against. Gitignored (*.log), so it
    # can only be present if the packager names it explicitly.
    ("10.19开发任务/CPyMO_RG35XX_uClibc_Test.log", "the real v0.22 device log"),
    ("research/logs/real_rg35xx_v022.log",
     "the repository copy the perf test reads"),
    # The artifact authority the README points at. NOTE: the repository's
    # top-level `SHA256SUMS` is deliberately NOT packaged -- it describes the
    # whole tree, including files the package does not carry, so `sha256sum -c`
    # on it after `unzip` reports ~9 "FAILED open or read" for things that were
    # never meant to be there. Measuring that changed the answer: the file a
    # reviewer should verify inside the archive is MANIFEST.sha256, which lists
    # exactly the packaged files, and the README now says so.
    ("MANIFEST.sha256", "the in-zip manifest (the one to verify here)"),
    ("SHA256SUMS_v021R1_handoff.txt", "the original handoff manifest"),
    # The deliverable.
    ("research/candidate_v023_soft/CANDIDATE.txt", "the candidate"),
    ("research/candidate_v023_soft/APPS/CPyMO_RG35XX_uClibc_Test/cpymo_libretro.so",
     "the candidate core"),
    ("deploy/Roms/APPS/CPyMO.sh", "the launcher"),
    # The reports.
    ("research/reports/v023_ledger.md", "the ledger"),
    ("research/reports/v023_audio_feasibility.md", "audio"),
    ("research/reports/v023_game_selector_assessment.md", "the selector"),
]


def main(argv: list[str]) -> int:
    version = argv[1] if len(argv) > 1 else "v023"
    zpath = ROOT / "dist" / f"CPyMO_{version}_review.zip"
    if not zpath.is_file():
        print(f"SKIP: no package at {zpath}")
        return 0

    with zipfile.ZipFile(zpath) as z:
        names = set(z.namelist())

    missing = 0
    for rel, why in MUST_BE_IN_PACKAGE:
        ok = rel in names
        print(("  ok      " if ok else "  MISSING ") + f"{rel}   <- {why}")
        if not ok:
            missing += 1

    print()
    if missing:
        print(f"FAIL package-contents: {missing} required file(s) absent")
        return 1
    print(f"PASS package-contents: all {len(MUST_BE_IN_PACKAGE)} required "
          f"files are in the archive")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
