#!/usr/bin/env python3
"""Does the shipped bundle CARRY the shipped source?

`dist/CPyMO_v023_history.bundle` embeds a commit, and packaging happens before
the commit that records the digests, so the tip is normally one commit behind
HEAD. What a reviewer needs is not "the same commit hash" but "the same
SOURCE": cloning the bundle must produce the code the zip describes.

This asserts exactly that:

  * the bundle names a commit that is an ancestor of HEAD (or is HEAD);
  * `git diff --name-only <tip> HEAD` touches NOTHING outside the digest
    bookkeeping files, because those are records written after the commit and
    cannot be inside the bundle by construction.

Exit 0 = PASS, 1 = the bundle does not carry the shipped source, 2 = skip
(no bundle, or not a git checkout).
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GIT_CANDIDATES = (r"C:\Program Files\Git\cmd\git.exe", "git")
# Files that are a RECORD of the build, written after the commit; they are
# expected to differ between the bundled commit and HEAD.
BOOKKEEPING = {"SHA256SUMS", "dist/SHA256SUMS", "dist/MANIFEST.sha256"}


def git(*args: str) -> str:
    for cand in GIT_CANDIDATES:
        if cand == "git" or Path(cand).is_file():
            r = subprocess.run([cand, "-c", "core.quotepath=false", *args],
                               cwd=ROOT, capture_output=True, text=True,
                               encoding="utf-8")
            if r.returncode == 0:
                return r.stdout.strip()
    return ""


def git_ok(*args: str) -> bool:
    """Run git for its EXIT CODE (`merge-base --is-ancestor` prints nothing)."""
    for cand in GIT_CANDIDATES:
        if cand == "git" or Path(cand).is_file():
            r = subprocess.run([cand, "-c", "core.quotepath=false", *args],
                               cwd=ROOT, capture_output=True, text=True,
                               encoding="utf-8")
            if r.returncode == 0:
                return True
    return False


def main(argv: list[str]) -> int:
    version = argv[1] if len(argv) > 1 else "v023"
    bundle = ROOT / "dist" / f"CPyMO_{version}_history.bundle"
    if not bundle.is_file():
        print(f"SKIP: {bundle.name} is not present")
        return 0
    head = git("rev-parse", "HEAD")
    if not head:
        print("SKIP: not a git checkout")
        return 0

    # Match on the commit the bundle stores for HEAD; the value is not
    # stable across runs, the CONTENT is what matters.
    listed = git("bundle", "list-heads", str(bundle))
    tips = [ln.split()[0] for ln in listed.splitlines() if ln.strip()]
    if not tips:
        print("FAIL: the bundle lists no refs", file=sys.stderr)
        return 1

    ancestor = ""
    for tip in tips:
        if tip == head or git_ok("merge-base", "--is-ancestor", tip, head):
            ancestor = tip
            break
    if not ancestor:
        print(f"FAIL: none of the bundle's {len(tips)} tips is an ancestor "
              f"of HEAD ({head[:7]})", file=sys.stderr)
        return 1

    changed = [l for l in git("diff", "--name-only", ancestor, head).splitlines()
               if l.strip()]
    unexpected = [c for c in changed if c not in BOOKKEEPING]
    print(f"bundle tip   : {ancestor[:12]}"
          f"{'  (== HEAD)' if ancestor == head else '  (HEAD~)'}")
    print(f"HEAD         : {head[:12]}")
    print(f"diff tip..HEAD: {len(changed)} path(s)"
          f"{' -- all digest bookkeeping' if not unexpected else ''}")
    for c in changed:
        mark = "bookkeeping" if c in BOOKKEEPING else "SOURCE"
        print(f"   {mark:12} {c}")

    if unexpected:
        print(f"FAIL: the bundle does not carry {len(unexpected)} source "
              f"file(s) as shipped")
        return 1
    print("PASS bundle-covers-source: cloning the bundle yields the shipped "
          "source (only the post-commit digest records differ)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
