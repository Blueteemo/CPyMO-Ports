#!/usr/bin/env python3
"""Is the return package STALE?

`tools/verify_package.py` proves the zip is internally consistent (every entry
matches the manifest written beside it). It cannot notice that a file changed
AFTER packaging, because the manifest travelled with the zip.

This compares every packaged file against the working tree, byte for byte, and
lists both directions:

  changed   in the zip and in the tree, but different bytes  -> STALE
  missing   in the zip, and the tree has it (and it is one of the
            roots the packager includes)                    -> STALE
  extra     in the zip, not in the tree any more           -> informational

Exit code 1 when anything is stale, so it can gate a "ready to send" step.
"""
from __future__ import annotations

import hashlib
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Keep in step with tools/make_return_package.py's INCLUDE_DIRS. The 19 Oct
# folder belongs here: it holds the TASK DOC and the baseline device log, and
# `*.log` is gitignored, so a "did you package the log?" question could not
# be answered by the manifest at all.
TREE_ROOTS = ("source", "research", "deploy", "docs", "tests", "target",
              "tools", "10.19开发任务")


def wanted(rel: str) -> bool:
    if not rel.startswith(TREE_ROOTS):
        return False
    # EXCLUDE_PREFIX in the packager, verbatim.
    if any(rel.startswith(p) for p in (
            "research/local-sdk/", "research/local-output/", "research/bench/",
            "build_arm_", "build-game-loader/", "build-garlic-paths/",
            "build-menu/", "build-stage")):
        return False
    if rel.endswith((".dis", ".byfunc", ".pyc")) or "__pycache__" in rel:
        return False
    return True


def check_manifest_newlines(zpath: Path) -> list[str]:
    """A manifest must use LF, or `sha256sum -c` cannot read it.

    Python's text mode on Windows turns every "\\n" into "\\r\\n"; GNU
    sha256sum then keeps the CR as part of the file NAME and reports every
    single entry as "No such file or directory". The in-package check uses
    `splitlines()` and would never notice, so it is checked here explicitly.
    """
    bad = []
    dist_manifest = ROOT / "dist" / "MANIFEST.sha256"
    if dist_manifest.is_file() and b"\r\n" in dist_manifest.read_bytes():
        bad.append("dist/MANIFEST.sha256 uses CRLF")
    with zipfile.ZipFile(zpath) as z:
        if b"\r\n" in z.read("MANIFEST.sha256"):
            bad.append("in-zip MANIFEST.sha256 uses CRLF")
    root = ROOT / "SHA256SUMS"
    if root.is_file() and b"\r\n" in root.read_bytes():
        bad.append("root SHA256SUMS uses CRLF")
    for label in bad:
        print(f"  NEWLINE {label}; `sha256sum -c` would fail on it")
    return bad


def main(argv: list[str]) -> int:
    zpath = Path(argv[1]) if len(argv) > 1 else (
        ROOT / "dist" / "CPyMO_v023_review.zip")
    print(f"package: {zpath.name}")

    stale: list[str] = check_manifest_newlines(zpath)
    with zipfile.ZipFile(zpath) as z:
        names = [n for n in z.namelist() if not n.endswith("/")]
        packed = set(names)
        for rel in sorted(names):
            if rel == "MANIFEST.sha256":
                continue
            p = ROOT / rel
            if not p.is_file():
                print(f"  EXTRA   {rel}   (in the zip, gone from the tree)")
                stale.append(rel)
                continue
            a = hashlib.sha256(z.read(rel)).hexdigest()
            b = hashlib.sha256(p.read_bytes()).hexdigest()
            if a != b:
                print(f"  CHANGED {rel}   zip={a[:12]} tree={b[:12]}")
                stale.append(rel)

    on_tree = {p.relative_to(ROOT).as_posix()
               for r in TREE_ROOTS for p in (ROOT / r).rglob("*")
               if p.is_file() and wanted(p.relative_to(ROOT).as_posix())}
    absent = sorted(on_tree - packed)
    if absent:
        print(f"  {len(absent)} file(s) in the tree are NOT in the zip:")
        for rel in absent[:20]:
            print(f"  MISSING {rel}")
        if len(absent) > 20:
            print(f"          ... and {len(absent) - 20} more")
        stale.extend(absent)

    print()
    if stale:
        print(f"STALE: {len(stale)} difference(s); rebuild with "
              f"python3 tools/make_return_package.py")
        return 1
    print(f"FRESH: all {len(packed) - 1} packaged files match the tree, no "
          f"tree file under {', '.join(TREE_ROOTS)} is missing, and every "
          f"manifest is LF-terminated.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
