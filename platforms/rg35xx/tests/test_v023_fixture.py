#!/usr/bin/env python3
"""Self-test for tests/build_resolution_fixture.py (the §2 fixture generator).

Checks the properties the loader harness RELIES on, so a later change to the
generator cannot silently weaken the acceptance test:

  * each game declares the resolution it claims to have;
  * every background PNG is at the game's OWN canvas size -- that is what
    makes a single frame-mean colour a meaningful assertion;
  * CaseUpper's background exists ONLY in the different-case spelling;
  * the fixture ASKS the filesystem about case behaviour instead of assuming.

    python3 tests/test_v023_fixture.py [path/to/font.ttf]
"""
from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

failures = 0


def check(cond: bool, msg: str) -> None:
    global failures
    if cond:
        print(f"  ok    {msg}")
    else:
        failures += 1
        print(f"  FAIL  {msg}")


def load_module():
    spec = importlib.util.spec_from_file_location(
        "build_resolution_fixture", HERE / "build_resolution_fixture.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def png_size(data: bytes) -> tuple[int, int]:
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    assert data[12:16] == b"IHDR", "first chunk is not IHDR"
    return struct.unpack(">II", data[16:24])


def main() -> int:
    mod = load_module()
    font = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        ROOT / "deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test/system/default.ttf")
    if not font.is_file():
        print(f"SKIP: no font at {font}")
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        dest = Path(tmp) / "fixture"
        rep = mod.build(dest, font, font.parent)

        for name, w, h, bgdir, bgfile in mod.GAMES:
            g = dest / name
            cfg = (g / "gameconfig.txt").read_text(encoding="utf-8")
            check(f"imagesize,{w},{h}" in cfg,
                  f"{name}: gameconfig.txt declares imagesize,{w},{h}")

            png = g / bgdir / bgfile
            check(png.is_file(), f"{name}: background {bgdir}/{bgfile} exists")
            if png.is_file():
                pw, ph = png_size(png.read_bytes())
                check((pw, ph) == (w, h),
                      f"{name}: background is the canvas size ({pw}x{ph} vs {w}x{h})")

        cu = dest / "CaseUpper"
        check((cu / "BG" / "TEST.PNG").is_file(),
              "CaseUpper: the different-case background exists")
        check(isinstance(rep.get("case_sensitive"), bool),
              "the fixture asked the filesystem about case behaviour")

        # `Path.exists()` is case-insensitive where the FILESYSTEM is, so the
        # "the other spelling is absent" claim can only be made on a
        # case-sensitive host. Ask the fixture's own probe, and say which
        # branch was taken, rather than pretending to check what cannot differ.
        if rep["case_sensitive"]:
            check(not (cu / "bg").exists(),
                  "CaseUpper: no lower-case bg/ directory (case-sensitive host)")
            check(not (cu / "bg" / "test.png").exists(),
                  "CaseUpper: the expected-case spelling is really absent")
        else:
            # On a case-insensitive host both names resolve to the one file;
            # the honest statement is about inodes, not about paths.
            same = (cu / "bg" / "test.png").stat().st_ino \
                == (cu / "BG" / "TEST.PNG").stat().st_ino
            check(same,
                  "CaseUpper: the two spellings are one file here "
                  "(case-insensitive host), so the case branch is untested"
                  " locally")
        check(rep.get("system") in (True, False),
              "the fixture recorded whether the system UI images were copied")

    print()
    print(f"{'FAIL' if failures else 'PASS'} test_v023_fixture "
          f"({failures} failure{'s' if failures != 1 else ''})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
