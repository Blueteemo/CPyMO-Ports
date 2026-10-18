#!/usr/bin/env python3
"""Diff two return packages, so "what changed since the last one?" is answered
from the ARTEFACTS rather than from memory.

    python3 tools/diff_packages.py dist/CPyMO_v022_review.zip \
                                  dist/CPyMO_v023_review.zip

Reports, per file: ADDED / REMOVED / CHANGED (with both digests), plus the
counts and the size difference. Files that are identical are not printed.
"""
from __future__ import annotations

import hashlib
import sys
import zipfile
from pathlib import Path


def entries(z: zipfile.ZipFile) -> dict[str, str]:
    out = {}
    for n in z.namelist():
        if n.endswith("/") or n == "MANIFEST.sha256":
            continue
        out[n] = hashlib.sha256(z.read(n)).hexdigest()
    return out


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    a, b = (Path(x) for x in argv[1:])
    for p in (a, b):
        if not p.is_file():
            print(f"no such package: {p}", file=sys.stderr)
            return 2

    with zipfile.ZipFile(a) as za, zipfile.ZipFile(b) as zb:
        ea, eb = entries(za), entries(zb)

    added = sorted(set(eb) - set(ea))
    removed = sorted(set(ea) - set(eb))
    changed = sorted(n for n in set(ea) & set(eb) if ea[n] != eb[n])
    same = len(ea) - len(removed) - len(changed)

    print(f"A = {a.name}  ({len(ea)} files, {a.stat().st_size/1e6:.2f} MB)")
    print(f"B = {b.name}  ({len(eb)} files, {b.stat().st_size/1e6:.2f} MB)")
    print(f"added {len(added)}, removed {len(removed)}, changed {len(changed)}, "
          f"identical {same}")
    print()

    for label, names in (("ADDED", added), ("REMOVED", removed)):
        if not names:
            continue
        print(f"== {label} ({len(names)})")
        for n in names:
            print(f"   {n}")
        print()

    if changed:
        print(f"== CHANGED ({len(changed)})")
        for n in changed:
            print(f"   {n}")
            print(f"      A {ea[n][:16]}")
            print(f"      B {eb[n][:16]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
