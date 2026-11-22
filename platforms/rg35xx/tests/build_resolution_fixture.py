"""Build the §2 fixture set: runnable games at the logical resolutions the
v0.23 task names (320x240, 540x360, 800x600), plus a case-variant.

    python3 tests/build_resolution_fixture.py <dest> [--font PATH] [--system DIR]

Fixtures
--------
Res320 / Res540 / Res800
    A complete, minimal, runnable game at each logical resolution. The
    background is a SOLID PNG at the game's OWN canvas size, so it covers the
    whole canvas and the resulting frame has a predictable mean colour --
    which is what the loader harness checks to prove the art really loaded.
    The `system/` UI images (message, name, cursor, highlight) are copied
    from the shipped test game when available, so the engine is not left
    logging avoidable "can not load" warnings that could hide a real failure.

CaseUpper
    800x600, and the background exists ONLY as `BG/TEST.PNG` while the script
    asks for plain `bg/test.png`. Whether that loads depends on the host
    filesystem's case behaviour, so the fixture ASKS the filesystem and prints
    the answer instead of assuming.
"""
from __future__ import annotations

import argparse
import os
import shutil
import struct
import zlib
from pathlib import Path

COLOURS = {
    "Res320": (40, 88, 200),
    "Res540": (200, 88, 40),
    "Res800": (40, 200, 88),
    "CaseUpper": (150, 60, 180),
}

# name, width, height, background directory, background file
GAMES = (
    ("Res320", 320, 240, "bg", "test.png"),
    ("Res540", 540, 360, "bg", "test.png"),
    ("Res800", 800, 600, "bg", "test.png"),
    ("CaseUpper", 800, 600, "BG", "TEST.PNG"),
)

SYSTEM_IMAGES = ("message.png", "message_cursor.png", "name.png",
                 "sel_highlight.png")


def png_solid(w: int, h: int, rgb: tuple[int, int, int]) -> bytes:
    # One filter byte per scanline, then the row, repeated.
    raw = (b"\x00" + bytes(rgb) * w) * h

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6))
            + chunk(b"IEND", b""))


def host_is_case_sensitive(dest: Path) -> bool:
    """Ask the destination filesystem, inside its own directory."""
    probe = dest / "CaseProbe.tmp"
    probe.write_bytes(b"x")
    try:
        other = dest / "caseprobe.tmp"
        try:
            return other.stat().st_ino != probe.stat().st_ino
        except OSError:
            return True
    finally:
        try:
            probe.unlink()
        except OSError:
            pass


def build(dest: Path, font: Path, system_src: Path | None) -> dict:
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    report = {"case_sensitive": host_is_case_sensitive(dest), "games": [],
              "system": bool(system_src)}

    for name, w, h, bgdir, bgfile in GAMES:
        g = dest / name
        for d in ("script", "system", bgdir):
            (g / d).mkdir(parents=True, exist_ok=True)

        (g / "gameconfig.txt").write_text(
            "gametitle,%s\nengineversion,1.2\nplatform,pygame\n"
            "scripttype,pymo\nimagesize,%d,%d\nstartscript,start\n"
            "bgformat,png\nfontsize,24\n" % (name, w, h),
            encoding="utf-8")
        # `#wait 2000` keeps the background up while the harness samples,
        # instead of racing the end of the script.
        (g / "script" / "start.txt").write_text(
            "#bg test\n#wait 2000\n", encoding="utf-8")
        (g / bgdir / bgfile).write_bytes(png_solid(w, h, COLOURS[name]))
        shutil.copy2(font, g / "system" / "default.ttf")

        if system_src and system_src.is_dir():
            for img in SYSTEM_IMAGES:
                src = system_src / img
                if src.is_file():
                    shutil.copy2(src, g / "system" / img)

        report["games"].append((name, w, h, f"{bgdir}/{bgfile}"))
    return report


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dest", type=Path)
    ap.add_argument("--font", type=Path, default=None)
    ap.add_argument("--system", type=Path, default=None,
                    help="directory of UI images to copy (default: the font's)")
    a = ap.parse_args()

    font = a.font or Path(os.environ.get(
        "CPYMO_TEST_FONT",
        "deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test/system/default.ttf"))
    if not font.is_file():
        raise SystemExit(f"no font at {font}; pass --font or set CPYMO_TEST_FONT")

    system_src = a.system
    if system_src is None and (font.parent / "message.png").is_file():
        system_src = font.parent

    rep = build(a.dest, font, system_src)
    print("PASS resolution fixture:", a.dest)
    print("   host filesystem is "
          + ("CASE-SENSITIVE" if rep["case_sensitive"] else "case-insensitive"))
    for name, w, h, bg in rep["games"]:
        print(f"   {name}: imagesize={w}x{h} background={bg} (solid, canvas-sized)")
    print(f"   system UI images copied: {'yes' if rep['system'] else 'no'}")
    if not rep["case_sensitive"]:
        print("   note: CaseUpper still loads here; on the RG35XX (case-sensitive)"
              " it must be rejected BY NAME with its on-disk spelling")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
