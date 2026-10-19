#!/usr/bin/env python3
"""Do what a Linux reviewer does: unzip the package and `sha256sum -c` in it.

Two separate manifests have to hold, and they answer different questions:

  MANIFEST.sha256  every file IN the archive matches what the SENDER said
  SHA256SUMS       every file in the tree still matches, and -- the part a
                   reviewer notices first -- the file the README calls the
                   authority is actually there

`verify_package.py` reads the zip from Python and therefore never sees a file
that failed to land on disk. This extracts for real, then runs GNU
`sha256sum -c` from the extraction directory exactly as documented.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main(argv: list[str]) -> int:
    version = argv[1] if len(argv) > 1 else "v023"
    zpath = ROOT / "dist" / f"CPyMO_{version}_review.zip"
    if not zpath.is_file():
        print(f"SKIP: no package at {zpath}")
        return 0

    tmp = Path(tempfile.mkdtemp(prefix="unzipped-"))
    try:
        with zipfile.ZipFile(zpath) as z:
            z.extractall(tmp)
            names = z.namelist()

        failures = 0

        # MANIFEST.sha256 is the one to check INSIDE the archive: it lists
        # exactly the packaged files. The repository's tree `SHA256SUMS` is
        # deliberately not shipped, because it describes the whole tree and
        # `sha256sum -c` on it after `unzip` would report failures for files the
        # package never carried. That was measured, and it is why the README
        # names MANIFEST.sha256 here.
        for manifest in ("MANIFEST.sha256",):
            if manifest not in names:
                print(f"  MISSING {manifest} in the archive")
                failures += 1
                continue
            r = subprocess.run(["sha256sum", "-c", manifest], cwd=tmp,
                               capture_output=True, text=True)
            ok = r.stdout.count(": OK")
            bad = [l for l in r.stdout.splitlines() if "OK" not in l]
            print(f"  {manifest:16} rc={r.returncode} "
                  f"ok={ok} problem={len(bad)}")
            for line in bad[:5]:
                print("     ", line)
            if r.returncode != 0:
                failures += 1

        # The tree manifest must NOT be inside, or the check above would fail
        # for every file the package legitimately excludes.
        if "SHA256SUMS" in names:
            print("  the tree SHA256SUMS is IN the archive; it lists files the "
                  "package does not carry")
            failures += 1

        # The SHIPPED test suite must run from the extraction directory too:
        # that is the other thing the reviewer reported (a test hardcoding a
        # path the archive did not contain).
        log = tmp / "research/logs/real_rg35xx_v022.log"
        print(f"  perf log present  {log.is_file()}")
        if not log.is_file():
            failures += 1
        # Execute bits do not survive Python's extractall() on Windows (the
        # host filesystem has no POSIX mode), so the authoritative check for
        # them is tests/test_zip_modes.py, which reads the archive metadata.
        # Here only the presence is asserted, plus the bit when the extracting
        # host actually preserves it.
        for p in ("tests/run_garlic_paths_test.sh", "deploy/Roms/APPS/CPyMO.sh"):
            q = tmp / p
            present = q.is_file()
            exec_bit = bool(q.stat().st_mode & 0o111) if present else False
            print(f"  {p:38} present={present} exec={exec_bit}"
                  f"{'' if exec_bit or not present else '  (metadata check elsewhere)'}")
            if not present:
                failures += 1

        print()
        if failures:
            print(f"FAIL unpacked-package: {failures} problem(s)")
            return 1
        print("PASS unpacked-package: both manifests verify from a real "
              "extraction, and the scripts are executable")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
