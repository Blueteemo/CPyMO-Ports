#!/usr/bin/env python3
"""Every shell script in the return package must be executable on Linux.

Windows' `zipfile` writes `create_system=0` (FAT) and `external_attr=0666`, so
after `unzip` on Linux a `*.sh` has no execute bit and a test that runs it
directly fails with `Permission denied`. That is what an independent reviewer
hit. The archive is shipped to Linux/WSL reviewers, so it has to carry the
mode.

Exit 0 = every script has an exec bit, 2 = no package (SKIP), 1 = a script does
not.
"""
from __future__ import annotations

import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main(argv: list[str]) -> int:
    version = argv[1] if len(argv) > 1 else "v023"
    zpath = ROOT / "dist" / f"CPyMO_{version}_review.zip"
    if not zpath.is_file():
        print(f"SKIP: no package at {zpath}")
        return 0

    bad: list[str] = []
    fat: list[str] = []
    scripts = 0
    with zipfile.ZipFile(zpath) as z:
        for n in z.namelist():
            info = z.getinfo(n)
            mode = (info.external_attr >> 16) & 0o777
            if info.create_system != 3:
                fat.append(n)
            if not n.endswith((".sh", ".py", ".bat", ".command")):
                continue
            scripts += 1
            if not (mode & 0o111):
                bad.append(f"{n} (mode {mode:04o})")

    print(f"package       : {zpath.name}")
    print(f"scripts       : {scripts}")
    print(f"non-UNIX entries (create_system != 3): {len(fat)}")
    if bad:
        print(f"WITHOUT an exec bit: {len(bad)}")
        for b in bad[:10]:
            print("   ", b)
        print("\nFAIL zip-modes: a Linux reviewer would get Permission denied")
        return 1
    print("\nPASS zip-modes: every script in the package is executable on Linux")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
