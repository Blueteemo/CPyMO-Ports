#!/usr/bin/env python3
"""Is the v022 package still needed, now that v023 exists?

Two questions, answered from the files:

  1. Does the v023 zip contain every FILE the v022 zip had? (If yes, nothing
     is lost by sending only v023.)
  2. Does the v023 bundle contain every COMMIT the v022 bundle had?
  3. Can a reviewer still see what v022 measured? (The v022 report, the
     baseline frames and the device log must be inside the v023 zip.)
"""
from __future__ import annotations

import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GIT = (r"C:\Program Files\Git\cmd\git.exe"
       if Path(r"C:\Program Files\Git\cmd\git.exe").is_file() else "git")


def zfiles(p: Path) -> set[str]:
    with zipfile.ZipFile(p) as z:
        return {n for n in z.namelist() if not n.endswith("/")}


def refs(bundle: Path) -> dict[str, str]:
    out = {}
    r = subprocess.run([GIT, "bundle", "list-heads", str(bundle)],
                       cwd=ROOT, capture_output=True, text=True)
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2:
            out[parts[1]] = parts[0]
    return out


def main() -> int:
    a = ROOT / "dist" / "CPyMO_v022_review.zip"
    b = ROOT / "dist" / "CPyMO_v023_review.zip"
    if not a.is_file() or not b.is_file():
        print("need both packages in dist/")
        return 2

    fa, fb = zfiles(a), zfiles(b)
    lost = sorted(fa - fb)
    print(f"v022 files : {len(fa)}")
    print(f"v023 files : {len(fb)}")
    print(f"in v022 but NOT in v023: {len(lost)}")
    for n in lost[:10]:
        print("   ", n)

    ba = ROOT / "dist" / "CPyMO_v022_history.bundle"
    bb = ROOT / "dist" / "CPyMO_v023_history.bundle"
    ra, rb = refs(ba), refs(bb)
    print()
    print("v022 bundle refs:")
    for k, v in sorted(ra.items()):
        print(f"   {k} {v[:12]}")
    print("v023 bundle refs:")
    for k, v in sorted(rb.items()):
        print(f"   {k} {v[:12]}")
    missing_refs = sorted(set(ra) - set(rb))
    moved = [k for k in set(ra) & set(rb) if ra[k] != rb[k]]
    print(f"refs dropped: {missing_refs or 'none'}")
    for k in sorted(moved):
        print(f"ref moved forward: {k} {ra[k][:8]} -> {rb[k][:8]}")

    # The commit the v022 package was built from must still be reachable.
    for probe in ("54c507a",):
        r = subprocess.run([GIT, "cat-file", "-t", probe], cwd=ROOT,
                           capture_output=True, text=True)
        print(f"commit {probe} still in the v023 repo: "
              f"{'yes' if r.stdout.strip() == 'commit' else 'NO'}")

    print()
    keep = [
        ("research/reports/v022_ledger.md", "v0.22's own numbers"),
        ("research/reports/patches/source_only.diff",
         "the diff the v0.22 audit reviewed"),
        ("research/baseline_frames", "the pixel baselines"),
        ("10.19开发任务/CPyMO_RG35XX_uClibc_Test.log",
         "the device log v0.23 is measured against"),
        ("docs/实机日志_v0.20_原始.txt", "the v0.20 device log"),
    ]
    print("does the v023 zip still carry the v022 evidence?")
    ok = True
    for probe, why in keep:
        hit = probe in fb or any(n.startswith(probe + "/") for n in fb)
        ok &= hit
        print(("  ok      " if hit else "  MISSING ") + probe + f"  <- {why}")

    print()
    verdict = not lost and not missing_refs and ok
    print("VERDICT: " + ("v023 alone is complete -- sending it loses nothing"
                         if verdict else
                         "send BOTH: something in v022 is not in v023"))
    return 0 if verdict else 1


if __name__ == "__main__":
    sys.exit(main())
