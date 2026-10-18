#!/usr/bin/env python3
"""Verify a CPyMO return package without trusting the local unzip.

    python3 verify_package.py dist/CPyMO_v022_review.zip

Why this exists
--------------
The package contains Chinese filenames. The zip marks them with the UTF-8
general-purpose flag (bit 11), which Python's `zipfile` honours -- and which
Info-ZIP's `unzip 6.00` does NOT on every build (the MSYS2 build here writes
mangled names for them, so a following `sha256sum -c` reports those files as
missing). Checking the digests *from inside the archive* sidesteps the local
unzip's charset entirely, and is what this script does.

Exit status: 0 only if every entry listed in the package's own
`MANIFEST.sha256` matches its bytes, and the manifest covers the archive.
"""
from __future__ import annotations

import hashlib
import sys
import zipfile
from pathlib import Path


def verify(zip_path: Path) -> int:
    with zipfile.ZipFile(zip_path) as z:
        names = set(z.namelist())
        if "MANIFEST.sha256" not in names:
            print("FAIL: archive has no MANIFEST.sha256", file=sys.stderr)
            return 1

        declared: dict[str, str] = {}
        for line in z.read("MANIFEST.sha256").decode("utf-8").splitlines():
            want, _, rel = line.partition(" *")
            if not want or not rel:
                print(f"FAIL: unparsable manifest line {line!r}", file=sys.stderr)
                return 1
            declared[rel] = want

        ok = bad = 0
        for rel, want in sorted(declared.items()):
            got = hashlib.sha256(z.read(rel)).hexdigest()
            if got == want:
                ok += 1
            else:
                bad += 1
                print(f"FAIL: digest {rel}", file=sys.stderr)

        # Every entry in the archive must be declared, except the manifest.
        extra = sorted(names - set(declared) - {"MANIFEST.sha256"})
        for rel in extra:
            print(f"FAIL: undeclared entry {rel}", file=sys.stderr)

        total = len(names)
        print(f"{zip_path.name}: {total} entries, {ok} verified, {bad} mismatched, "
              f"{len(extra)} undeclared")
        cap = (f"ALL {ok} PACKAGED FILES MATCH MANIFEST.sha256, "
               f"AND EVERY ENTRY IS DECLARED")
        if not (bad or extra):
            print(cap)
            return 0
        return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    p = Path(argv[1])
    if not p.is_file():
        print(f"FAIL: no such file {p}", file=sys.stderr)
        return 2
    return verify(p)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
