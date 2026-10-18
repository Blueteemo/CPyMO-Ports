#!/usr/bin/env python3
"""Regenerate the checkout-root SHA256SUMS so it describes THIS tree.

Why: the handoff shipped a `SHA256SUMS` that listed the v0.21-R1 files. After
the renderer work, a reviewer comparing the package against that file gets two
kinds of failure that are easy to misread as damage:

  * files it lists (the 5 edited sources + the Windows-adapted test) no longer
    match the recorded digests -- expected, that is the change under review;
  * files it never lists (the new reports, tools and candidates).

So the original stays available, byte-for-byte, as
`SHA256SUMS_v021R1_handoff.txt`, and the root `SHA256SUMS` becomes a complete
listing of the current tree. Generated trees (the extracted SDK, host capture
output, benchmark scratch, ARM object trees) are excluded and are rebuilt by the
documented commands.

    python3 tools/make_tree_manifest.py
"""
from __future__ import annotations

import hashlib
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXCLUDE_DIRS = {
    ".git", "research/local-sdk", "research/local-output", "research/bench",
    "__pycache__", "dist", "codeql", "dist-newstyle",
}
EXCLUDE_PREFIX = ("build_arm_", "build-stage-", "build-garlicos", "build/")
TEXT_NAME = "SHA256SUMS"
KEEP_ORIGINAL = "SHA256SUMS_v021R1_handoff.txt"
# Files not under git (dist/*.zip and dist/*.bundle are gitignored) but worth
# listing explicitly, so a reviewer can check a downloaded package against the
# tree. EVERY round's package is listed, including the previous one, so the
# digest of whichever artefact the reviewer has is in the file.
EXTRA_FILES = tuple(
    ["SHA256SUMS_v021R1_handoff.txt"]
    + [f"dist/{name}" for name in (
        "CPyMO_v022_review.zip", "CPyMO_v022_history.bundle",
        "CPyMO_v023_review.zip", "CPyMO_v023_history.bundle")]
)


def tracked() -> list[str]:
    """Every tracked path, in the ON-DISK spelling.

    `git ls-files` C-quotes any path with a non-ASCII byte by default:
    `00_请先阅读...md` comes back as
    `"00_\\350\\257\\267..."`. Such a name is not a path that exists, so
    `excluded()` dropped it, and the manifest silently described 512 files
    while the tree had 522 -- `sha256sum -c` then agreed with the manifest
    and reported zero failures, because the 10 missing lines were never
    written. `core.quotepath=false` is what makes the two agree.
    """
    git = "git"
    for cand in (r"C:\Program Files\Git\cmd\git.exe",):
        if Path(cand).is_file():
            git = cand
            break
    r = subprocess.run([git, "-c", "core.quotepath=false", "ls-files"],
                       cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8")
    if r.returncode:
        raise SystemExit("git ls-files failed; run inside the checkout")
    names = [line for line in r.stdout.splitlines() if line]
    quoted = [n for n in names if n.startswith('"') or "\\3" in n]
    if quoted:
        raise SystemExit(
            "git returned C-quoted paths ({}); the manifest would name "
            "files that do not exist: {}".format(len(quoted), quoted[:3]))
    return names


def excluded(rel: str) -> bool:
    if rel == TEXT_NAME:
        return True                  # the manifest must not list itself
    if any(rel == d or rel.startswith(d + "/") for d in EXCLUDE_DIRS):
        return True
    if any(rel.startswith(p) for p in EXCLUDE_PREFIX):
        return True
    return not (ROOT / rel).is_file()


def main() -> int:
    original = ROOT / TEXT_NAME
    keep = ROOT / KEEP_ORIGINAL
    if original.is_file() and not keep.is_file():
        # Preserve the handoff manifest verbatim before overwriting the name.
        keep.write_bytes(original.read_bytes())
        print(f"preserved handoff manifest as {KEEP_ORIGINAL}")

    # The preserved handoff manifest, then the (large) generated packages if
    # present, then every tracked file of the current tree. Keyed by path, so
    # a file that is BOTH tracked and in EXTRA_FILES is listed once --
    # `SHA256SUMS_v021R1_handoff.txt` was, and the duplicate made a 543-line
    # file with 542 names (harmless to `sha256sum -c`, confusing to a reader).
    by_path: dict[str, str] = {}
    for extra in EXTRA_FILES:
        p = ROOT / extra
        if p.is_file():
            by_path[extra] = hashlib.sha256(p.read_bytes()).hexdigest()

    files = sorted(f for f in tracked() if not excluded(f))
    for rel in files:
        digest = hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()
        by_path.setdefault(rel, digest)

    lines = [f"{d} *{rel}\n" for rel, d in by_path.items()]

    # Self-check: every tracked file that was NOT excluded must be in the
    # manifest. A C-quoted name used to slip out here silently, and because
    # the manifest is the thing `sha256sum -c` reads, BOTH SIDES agreed on
    # the omission and reported zero failures (512 listed, 522 in the tree).
    # The exclusion rule here is `excluded()` itself, so the check cannot
    # drift from what was written.
    listed = {f"{e}" for e in EXTRA_FILES}
    listed |= set(files)
    must_list = [f for f in tracked() if not excluded(f)]
    absent = [f for f in must_list if f not in listed]
    if absent:
        for f in absent[:10]:
            print(f"  NOT LISTED: {f}")
        raise SystemExit(f"{len(absent)} tracked file(s) missing from "
                        f"{TEXT_NAME}")

    # `newline="\n"` is NOT cosmetic. On Windows, Python's text mode turns the
    # "\n" in each line into "\r\n", and a manifest written that way no longer
    # survives `sha256sum -c`: GNU sha256sum keeps the CR as part of the file
    # name and reports every entry as "No such file or directory". The handoff
    # manifest is LF-only, so this also keeps the two diffable line by line.
    with open(original, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("".join(sorted(lines)))
    real = sum(1 for line in lines if " *dist/" not in line)
    # One line per path, always. A duplicate is harmless to sha256sum and
    # misleading to a reader (two identical lines read as two files).
    dupes = len(lines) - len({line.split(" *", 1)[1] for line in lines})
    if dupes:
        raise SystemExit(f"{dupes} duplicate path(s) in {TEXT_NAME}")
    print(f"wrote {TEXT_NAME}: {len(lines)} entries ({real} tree files) "
          f"(was {sum(1 for _ in keep.read_text(encoding='utf-8').splitlines())}"
          f" in the handoff, now at {KEEP_ORIGINAL})")
    return 0


if __name__ == "__main__":
    os.chdir(ROOT)
    raise SystemExit(main())
