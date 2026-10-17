#!/usr/bin/env python3
"""Build the two return packages.

1. small review zip (README_FOR_REVIEWER.md, reports, patches, both candidate
   APPS trees, both ARM cores) -- this is what goes to the reviewer.
2. git bundle (full history) so a Linux/WSL user can clone and re-run
   everything exactly, including the SDK-based cross build.

Both are written to the checkout root. Sizes and sha256 are printed.
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import zipfile
from pathlib import Path

# tools/ lives directly under the checkout root.
ROOT = Path(__file__).resolve().parents[1]
(ROOT / "dist").mkdir(exist_ok=True)

INCLUDE_DIRS = [
    "source",
    "research/reports",
    "research/tools",
    "research/candidate_v022_soft",
    "research/candidate_v022_softfp",
    "research/experimental",
    "research/logs",
    "research/baseline_frames",
    "deploy",
    "docs",
    "tests",
    "target",
    "tools",
]
INCLUDE_FILES = [
    "README_FOR_REVIEWER.md",
    "README.md",
    "00_请先阅读_本地研发入口.md",
    "SHA256SUMS",
]
EXCLUDE_SUFFIX = (".dis", ".byfunc")          # multi-MB raw dumps; kept in git
EXCLUDE_PREFIX = ("research/local-sdk/", "research/local-output/",
                  "research/bench/", "build_arm_")
INCLUDE_TOP = 400 * 1024 * 1024   # a sanity cap, not a real limit


def wanted(rel: str) -> bool:
    if rel.endswith(EXCLUDE_SUFFIX):
        return False
    if rel.startswith(EXCLUDE_PREFIX):
        return False
    if rel.endswith(".pyc") or "__pycache__" in rel:
        return False
    return True


def review_zip() -> Path:
    out = ROOT / "dist" / "CPyMO_v022_review.zip"
    n = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for f in INCLUDE_FILES:
            p = ROOT / f
            if p.is_file():
                z.write(p, f); n += 1
        for d in INCLUDE_DIRS:
            base = ROOT / d
            if not base.is_dir():
                continue
            for p in sorted(base.rglob("*")):
                if not p.is_file():
                    continue
                rel = p.relative_to(ROOT).as_posix()
                if not wanted(rel):
                    continue
                z.write(p, rel); n += 1
    return out


def git_exe() -> str:
    """`git` is not necessarily on a native-Windows Python's PATH, so probe the
    usual install locations before giving up."""
    import shutil
    found = shutil.which("git")
    if found:
        return found
    for cand in (r"C:\Program Files\Git\cmd\git.exe",
                 r"C:\Program Files (x86)\Git\cmd\git.exe"):
        if Path(cand).is_file():
            return cand
    return "git"


def bundle() -> Path:
    out = ROOT / "dist" / "CPyMO_v022_history.bundle"
    if out.exists():
        out.unlink()
    git = git_exe()
    r = None
    # `--all` needs >= 2.45 for the branch set we have; `HEAD` works everywhere
    # and still carries every commit reachable from the current branch.
    for args in (["bundle", "create", str(out), "--all"],
                 ["bundle", "create", str(out), "HEAD"]):
        r = subprocess.run([git, *args], cwd=ROOT, capture_output=True, text=True)
        if r.returncode == 0:
            return out
    sys.stderr.write((r.stderr if r else "git bundle failed\n"))
    raise SystemExit(1)


def report(p: Path) -> None:
    h = hashlib.sha256(p.read_bytes()).hexdigest()
    print(f"{p.name:34} {p.stat().st_size/1e6:8.2f} MB  sha256 {h}")


if __name__ == "__main__":
    os.chdir(ROOT)
    report(review_zip())
    report(bundle())
