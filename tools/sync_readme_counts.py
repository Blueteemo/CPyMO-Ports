#!/usr/bin/env python3
"""Fix the README's count claims to the current artefacts, then say what moved.

Three numbers in README_FOR_REVIEWER.md describe the SHIPPING artefacts:
the zip's entry count, `MANIFEST.sha256`'s coverage, and the root
manifest's line count. All three move when the packaged file set moves (a new
test is added), and a stale one reads exactly like a true one. This rewrites
exactly those three values and leaves everything else alone.

    python3 tools/sync_readme_counts.py [--check]

--check exits 1 if anything would change (for a gate); the default edits.
"""
from __future__ import annotations

import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
README = ROOT / "README_FOR_REVIEWER.md"


def actual(version: str = "v023") -> dict[str, int]:
    zpath = ROOT / "dist" / f"CPyMO_{version}_review.zip"
    with zipfile.ZipFile(zpath) as z:
        names = z.namelist()
        entries = len(names)
        covered = len(z.read("MANIFEST.sha256").decode("utf-8").splitlines())
    root = len((ROOT / "SHA256SUMS").read_text(
        encoding="utf-8").splitlines()) if (ROOT / "SHA256SUMS").is_file() else 0
    return {"entries": entries, "covered": covered, "root": root}


def main(argv: list[str]) -> int:
    check_only = "--check" in argv
    want = actual()
    text = README.read_text(encoding="utf-8")
    before = text

    # Each pattern is anchored on the surrounding words, so no other number in
    # the document is touched.
    subs = [
        (r"zip 里 \*\*\d+ 个条目\*\*",
         f"zip 里 **{want['entries']} 个条目**"),
        (r"`MANIFEST\.sha256` 覆盖其中 \*\*\d+\*\* 条",
         f"`MANIFEST.sha256` 覆盖其中 **{want['covered']}** 条"),
        (r"sha256sum -c SHA256SUMS\s*->\s*\d+ entries",
         f"sha256sum -c SHA256SUMS                 ->  {want['root']} entries"),
        (r"sha256sum -c MANIFEST\.sha256 \)\s*->\s*\d+ entries",
         f"sha256sum -c MANIFEST.sha256 ) ->  "
         f"{want['covered']} entries"),
    ]
    used = 0
    for pat, rep in subs:
        text, n = re.subn(pat, rep, text)
        used += n

    missing = []
    if f"**{want['entries']} 个条目**" not in text:
        missing.append("entry count")
    if f"**{want['covered']}** 条" not in text:
        missing.append("manifest coverage")
    if f"->  {want['root']} entries" not in text:
        missing.append("root manifest count")
    if f"{want['covered']} entries" not in text:
        missing.append("unpacked manifest count")

    print(f"actual: entries={want['entries']} covered={want['covered']} "
          f"root={want['root']}   (rewrote {used} spot(s))")
    if missing:
        print("STILL STALE: " + ", ".join(missing))
        return 1
    if text != before:
        if check_only:
            print("STALE: README_FOR_REVIEWER.md needs updating")
            return 1
        README.write_text(text, encoding="utf-8")
        print("updated README_FOR_REVIEWER.md")
    else:
        print("README_FOR_REVIEWER.md already matches")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
