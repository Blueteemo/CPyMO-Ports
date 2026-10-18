#!/usr/bin/env python3
"""Build the two return packages.

1. the review zip (README_FOR_REVIEWER.md, reports, patches, the candidate
   APPS trees, every ARM core, the new v0.23 launcher and its tests) -- this
   is what goes to the reviewer.
2. git bundle (full history) so a Linux/WSL user can clone and re-run
   everything exactly, including the SDK-based cross build.

Both are written to dist/. Sizes and sha256 are printed.

The package carries a VERSION, because the artefact a reviewer opens should say
which round it is; `PACKAGE_VERSION` below is the single place that changes.
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
VERSION = "v023"
(ROOT / "dist").mkdir(exist_ok=True)

INCLUDE_DIRS = [
    "source",
    "research/reports",
    "research/tools",
    "research/candidate_v022_soft",
    "research/candidate_v022_softfp",
    "research/candidate_v023_soft",
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
    # The handoff's own (v0.21-R1) manifest, so a reviewer can diff it against
    # the current SHA256SUMS and see exactly which lines this work changed. It is
    # shipped in addition to the package-relative SHA256SUMS written below.
    "SHA256SUMS_v021R1_handoff.txt",
]
EXCLUDE_SUFFIX = (".dis", ".byfunc")          # multi-MB raw dumps; kept in git
EXCLUDE_PREFIX = ("research/local-sdk/", "research/local-output/",
                  "research/bench/", "build_arm_", "build-game-loader/",
                  "build-garlic-paths/")
INCLUDE_TOP = 400 * 1024 * 1024   # a sanity cap, not a real limit


def wanted(rel: str) -> bool:
    if rel.endswith(EXCLUDE_SUFFIX):
        return False
    if rel.startswith(EXCLUDE_PREFIX):
        return False
    if rel.endswith(".pyc") or "__pycache__" in rel:
        return False
    return True


def review_zip() -> tuple[Path, list[str]]:
    # A NEW file per round, so a reviewer who already has the previous package
    # can keep both and diff them; the version is in the name.
    out = ROOT / "dist" / f"CPyMO_{VERSION}_review.zip"
    written: list[str] = []
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for f in INCLUDE_FILES:
            p = ROOT / f
            if p.is_file():
                z.write(p, f); written.append(f)
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
                z.write(p, rel); written.append(rel)
    return out, written


def write_manifest(zip_path: Path, members: list[str]) -> tuple[Path, int]:
    """A manifest that covers EVERY entry in the package, not the leftovers of an
    older handoff. Written next to the zip (dist/SHA256SUMS) and, in
    `sha256sum -c` format, INSIDE the zip as MANIFEST.sha256 with paths
    relative to the archive root. The manifest itself is excluded, and the
    check is: unzip anywhere, then `sha256sum -c MANIFEST.sha256`.

    A previous package shipped the v0.21 SHA256SUMS unchanged, so 6 listed
    files no longer matched and 89 packaged files were never listed at all.

    Notes:
    * the checkout contains Chinese filenames, so the manifest is UTF-8 (which
      is also what `sha256sum -c` reads on a modern Linux);
    * each path is preceded by `*`, sha256sum's binary marker, so checking is
      not affected by line-ending translation on a Windows checkout.
    """
    inner = ROOT / "dist" / "MANIFEST.sha256"
    lines = []
    for rel in sorted(members):
        p = ROOT / rel
        if not p.is_file():
            continue
        digest = hashlib.sha256(p.read_bytes()).hexdigest()
        lines.append(f"{digest} *{rel}\n")
    inner.write_text("".join(lines), encoding="utf-8")

    with zipfile.ZipFile(zip_path, "a", zipfile.ZIP_DEFLATED) as z:
        z.write(inner, "MANIFEST.sha256")

    outer = ROOT / "dist" / "SHA256SUMS"
    outer.write_text(
        f"{hashlib.sha256(zip_path.read_bytes()).hexdigest()}  {zip_path.name}\n",
        encoding="ascii")
    return inner, len(lines)


def verify_manifest(zip_path: Path) -> tuple[int, int]:
    """Re-open the zip and recompute every digest from its own bytes."""
    ok = bad = 0
    with zipfile.ZipFile(zip_path) as z:
        text = z.read("MANIFEST.sha256").decode("utf-8")
        for line in text.splitlines():
            want, _, rel = line.partition(" *")
            if not want or not rel:
                sys.stderr.write(f"MANIFEST UNPARSED {line!r}\n")
                bad += 1
                continue
            got = hashlib.sha256(z.read(rel)).hexdigest()
            if got == want:
                ok += 1
            else:
                bad += 1
                sys.stderr.write(f"MANIFEST MISMATCH {rel}\n")
    return ok, bad


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
    out = ROOT / "dist" / f"CPyMO_{VERSION}_history.bundle"
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
    if not (ROOT / "dist").is_dir():
        (ROOT / "dist").mkdir()
    # In-zip manifest of every packaged file, then the outer checksums.
    zpath, members = review_zip()
    inner, n = write_manifest(zpath, members)
    print(f"in-zip manifest: MANIFEST.sha256 covers {n} files")
    ok, bad = verify_manifest(zpath)
    print(f"manifest self-check (read back out of the zip): {ok} ok, {bad} bad")
    if bad:
        raise SystemExit(1)
    report(zpath)
    # The bundle's digest goes into SHA256SUMS alongside the zip's.
    bpath = bundle()
    with open(ROOT / "dist" / "SHA256SUMS", "a", encoding="ascii") as fh:
        fh.write(f"{hashlib.sha256(bpath.read_bytes()).hexdigest()}  {bpath.name}\n")
    report(bpath)
