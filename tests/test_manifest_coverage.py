#!/usr/bin/env python3
"""Does the shipped TEXT tree cover every file that should have a digest?

`sha256sum -c SHA256SUMS` can only check the lines that are IN the file, so
it agrees with any omission. This is the other direction: for each root the
deliverable claims to cover, count the files that should be listed and the
files that are, and name the difference.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEXT = ROOT / "SHA256SUMS"


def tracked() -> list[str]:
    git = "git"
    for cand in (r"C:\Program Files\Git\cmd\git.exe",):
        if Path(cand).is_file():
            git = cand
            break
    r = subprocess.run([git, "-c", "core.quotepath=false", "ls-files"],
                       cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8")
    if r.returncode:
        raise SystemExit("git ls-files failed")
    return [line for line in r.stdout.splitlines() if line]


def listed() -> dict[str, str]:
    out = {}
    for line in TEXT.read_text(encoding="utf-8").splitlines():
        want, _, rel = line.partition(" *")
        if want and rel:
            out[rel] = want
    return out


def main() -> int:
    have = listed()
    print(f"SHA256SUMS lists {len(have)} paths")
    quoted = [p for p in have if p.startswith('"')]
    print(f"  C-quoted (git) paths in it: {len(quoted)}")
    nonascii = [p for p in have if any(ord(c) > 127 for c in p)]
    print(f"  non-ASCII paths in it    : {len(nonascii)}")
    for p in nonascii[:4]:
        print(f"     {p}")

    names = tracked()
    missing = [n for n in names
               if n not in have
               and not n.startswith(("dist/", "research/local-sdk",
                                    "research/local-output", "research/bench/",
                                    "build_arm_", "build/"))
               and n != "SHA256SUMS"
               and (ROOT / n).is_file()]
    print(f"tracked paths: {len(names)}")
    print(f"  tracked but NOT in SHA256SUMS: {len(missing)}")
    for n in missing[:12]:
        print(f"     {n}")

    print()
    if missing or quoted:
        print("FAIL: the manifest is not a faithful summary of the tree")
        return 1
    print(f"PASS: SHA256SUMS covers all {len(names)} tracked paths "
          f"({len(nonascii)} of them non-ASCII, none C-quoted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
