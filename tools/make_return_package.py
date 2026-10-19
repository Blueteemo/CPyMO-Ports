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
    # The handoff's own research brief and the measured conclusions. They are
    # the documents the whole phase answers to, so a reviewer who wants to
    # check "did they do what was asked?" needs them; they live at the TOP of
    # research/ (not under reports/) and were missed by an earlier revision.
    "research/candidate_v022_soft",
    "research/candidate_v022_softfp",
    "research/candidate_v023_soft",
    "research/experimental",
    "research/logs",
    "research/baseline_frames",
    # The A/B structure dumps beside the experimental cores: small, and the
    # direct evidence for compare_arm_clang_lld.sh's 19 assertions.
    "research/build_arm_compare_clang",
    # The 19 Oct task folder. It holds the TASK DOC this whole phase answers
    # to AND the baseline device log the numbers are measured against, and
    # `*.log` is gitignored -- so the manifest could never show that the log
    # was missing, and the reports told the reviewer to parse a file that was
    # not in the package. 617 kB.
    "10.19开发任务",
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
    # The brief and the conclusions the phase was measured against.
    "research/研究任务与性能结论.md",
    "research/交给本地DeepSeek的执行提示词.md",
    "research/bench_baseline.log",
    # The handoff's own (v0.21-R1) manifest, so a reviewer can diff it against
    # the current SHA256SUMS and see exactly which lines this work changed. It is
    # shipped in addition to the package-relative SHA256SUMS written below.
    "SHA256SUMS_v021R1_handoff.txt",
]
EXCLUDE_SUFFIX = (".dis", ".byfunc")          # multi-MB raw dumps; kept in git
EXCLUDE_PREFIX = ("research/local-sdk/", "research/local-output/",
                  "research/bench/", "build_arm_", "build-game-loader/",
                  "build-garlic-paths/", "build-menu/", "build-stage")
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

    The entry is added with a FIXED timestamp. `zipfile.write()` stores the
    file's wall-clock mtime, and this manifest is the one member appended
    AFTER the others, so without a fixed date two builds of an UNCHANGED tree
    produced two different archives (same entries, same bytes, one timestamp)
    -- and the digest published in dist/SHA256SUMS moved on every rebuild.
    Measured: building twice from the same tree gave
    `a1a84301b451...` then `22da16fbc19a...`, differing in exactly one
    MANIFEST.sha256 header field. With a fixed date the archive is a fixed
    point of its own inputs, so `dist/SHA256SUMS` means something.

    Notes:
    * the checkout contains Chinese filenames, so the manifest is UTF-8 (which
      is also what `sha256sum -c` reads on a modern Linux);
    * each path is preceded by `*`, sha256sum's binary marker;
    * `newline="\\n"`: in Python's text mode Windows turns every "\\n" into
      "\\r\\n", and this machine's GNU sha256sum then keeps the CR as part of
      the NAME -- every entry reported as "No such file or directory". The
      in-package check reads the bytes through `splitlines()` and never
      noticed, but a reviewer running `sha256sum -c` would have.
    """
    inner = ROOT / "dist" / "MANIFEST.sha256"
    lines = []
    for rel in sorted(members):
        p = ROOT / rel
        if not p.is_file():
            continue
        digest = hashlib.sha256(p.read_bytes()).hexdigest()
        lines.append(f"{digest} *{rel}\n")
    with open(inner, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("".join(lines))

    # `ZipInfo` with an explicit date_time, because the default (the file's
    # mtime) makes two builds of an unchanged tree differ. 1980-01-01 is the
    # DOS epoch, which is what a zip can represent without overflow.
    info = zipfile.ZipInfo("MANIFEST.sha256", date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    with zipfile.ZipFile(zip_path, "a") as z:
        z.writestr(info, inner.read_bytes())

    outer = ROOT / "dist" / "SHA256SUMS"
    with open(outer, "w", encoding="ascii", newline="\n") as fh:
        fh.write(f"{hashlib.sha256(zip_path.read_bytes()).hexdigest()}  "
                 f"{zip_path.name}\n")
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
    #
    # `-c pack.threads=1` is NOT a speed knob, it is the reproducibility one.
    # `git bundle` packs the objects, and with pack.threads unset git picks a
    # thread count from the core count; measured on this 12-core host, FIVE
    # back-to-back `git bundle create ... --all` calls from an unchanged
    # repository produced FOUR DIFFERENT digests (33d23cc7, 20f78298, 71d5918a,
    # bbb3bca9...) at slightly different SIZES. With pack.threads=1 the same
    # five calls are byte-identical, so the digest recorded in SHA256SUMS means
    # something.
    common = ["-c", "pack.threads=1", "bundle", "create", str(out)]
    for args in (common + ["--all"], common + ["HEAD"]):
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
    # The bundle's digest goes into SHA256SUMS alongside the zip's, with the
    # SAME newline choice (`\n`) so a reviewer's `sha256sum -c` works.
    bpath = bundle()
    with open(ROOT / "dist" / "SHA256SUMS", "a", encoding="ascii",
              newline="\n") as fh:
        fh.write(f"{hashlib.sha256(bpath.read_bytes()).hexdigest()}  "
                 f"{bpath.name}\n")
    report(bpath)
