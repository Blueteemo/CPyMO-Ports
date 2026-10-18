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
# Files not under git yet (dist/ is tracked only via SHA256SUMS) but worth
# listing explicitly.
EXTRA_FILES = (
    "SHA256SUMS_v021R1_handoff.txt",
    "dist/CPyMO_v022_review.zip",
    "dist/CPyMO_v022_history.bundle",
)


def tracked() -> list[str]:
    git = "git"
    for cand in (r"C:\Program Files\Git\cmd\git.exe",):
        if Path(cand).is_file():
            git = cand
            break
    r = subprocess.run([git, "ls-files"], cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("git ls-files failed; run inside the checkout")
    return [line for line in r.stdout.splitlines() if line]


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
    # present, then every tracked file of the current tree.
    lines: list[str] = []
    for extra in EXTRA_FILES:
        p = ROOT / extra
        if p.is_file():
            lines.append(f"{hashlib.sha256(p.read_bytes()).hexdigest()} *{extra}\n")

    files = sorted(f for f in tracked() if not excluded(f))
    for rel in files:
        digest = hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()
        lines.append(f"{digest} *{rel}\n")

    original.write_text("".join(sorted(lines)), encoding="utf-8")
    print(f"wrote {TEXT_NAME}: {len(lines)} entries "
          f"(was {sum(1 for _ in keep.read_text(encoding='utf-8').splitlines())}"
          f" in the handoff, now at {KEEP_ORIGINAL})")
    return 0


if __name__ == "__main__":
    os.chdir(ROOT)
    raise SystemExit(main())
