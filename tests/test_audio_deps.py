#!/usr/bin/env python3
"""Self-test for tools/check_audio_deps.py.

The tool exists because a `.so` with one symbol the loader cannot resolve fails
at `dlopen`, which on the RG35XX is indistinguishable from "the core does
nothing". A check that always says OK would be worse than none, so this
exercises both directions:

  positive  the real audio core resolves completely -- against the SDK's own
            libav* and the loader's TLS helper;
  negative  the SAME core, with a DT_NEEDED soname renamed so it can no
            longer be found, must name the symbols that became unreachable.

The negative control edits a COPY of the binary. Renaming the string in place
keeps every offset valid, so the rewritten file is a real ELF that still
dlopen-fails for exactly the intended reason.

    python3 tests/test_audio_deps.py [build_arm_audio/cpymo_libretro_audio.so]
"""
from __future__ import annotations

import importlib.util
import io
import shutil
import subprocess
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "build_arm_audio/cpymo_libretro_audio.so"

failures = 0


def check(cond: bool, msg: str) -> None:
    global failures
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond:
        failures += 1


def load_tool():
    spec = importlib.util.spec_from_file_location(
        "check_audio_deps", ROOT / "tools/check_audio_deps.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run_main(mod, argv: list[str]) -> tuple[int, str]:
    saved = sys.argv
    sys.argv = ["check_audio_deps.py", *argv]
    buf = io.StringIO()
    try:
        with redirect_stdout(buf):
            rc = mod.main()
    finally:
        sys.argv = saved
    return rc, buf.getvalue()


def main() -> int:
    mod = load_tool()

    check(mod.READELF is not None and Path(mod.READELF).exists(),
          f"the ARM readelf was located ({mod.READELF})")

    if not CORE.is_file():
        print(f"\nSKIP: build the audio core first ({CORE})")
        return 0

    # --- positive -----------------------------------------------------
    rc, out = run_main(mod, [str(CORE)])
    check(rc == 0, "the real audio core resolves completely")
    check("OK: every undefined symbol resolves" in out,
          "and says so explicitly")
    check("libswscale.so.4" in out,
          "libswscale is named (the movie scaler is in the audio build)")
    # `__tls_get_addr` is defined by BOTH the uClibc loader and libc.so.0 and
    # is first-come in the attribution, so either name is a correct answer;
    # what matters is that a runtime half owns it and nothing is left over.
    check("ld-uClibc.so.1" in out,
          "the uClibc runtime (libc and/or the loader) is credited")
    check("(loader)" in out or "resolved by libc.so.0:" in out,
          "the TLS helper is attributed to the runtime, not left over")
    check("_tls_get_addr" not in out.split("UNRESOLVED")[-1] if "UNRESOLVED" in out
          else True,
          "and it does not appear among the unresolved")
    check("UNRESOLVED" not in out, "nothing is reported unresolved")
    # Named symbol classes, so a regression names WHICH half broke.
    for probe in ("libc.so.0:", "libavcodec.so.57:", "libswscale.so.4:"):
        check(probe in out, f"{probe} is reported as a provider")

    # --- negative control ---------------------------------------------
    with tempfile.TemporaryDirectory() as tmp:
        cut = Path(tmp) / "cut.so"
        shutil.copy2(CORE, cut)
        data = bytearray(cut.read_bytes())
        needle = b"libswscale.so.4\x00"
        at = data.find(needle)
        check(at > 0, "the libswscale soname string is present in the file")
        if at > 0:
            data[at:at + len(needle)] = b"libswscale-cut.so\x00"[:len(needle)]
            cut.write_bytes(data)

            rc2, out2 = run_main(mod, [str(cut)])
            check(rc2 == 1, "the renamed dependency makes the check fail")
            check("UNRESOLVED" in out2, "and it prints an UNRESOLVED section")
            check("sws_getContext" in out2 or "libswscale" in out2,
                  "the failure names the scaler symbols that were lost")

    # --- a missing file is a usage error, not a pass -------------------
    rc3, _ = run_main(mod, [str(ROOT / "no-such-core.so")])
    check(rc3 == 2, "a missing core is a usage error (exit 2)")

    print()
    print(f"{'FAIL' if failures else 'PASS'} test_audio_deps "
          f"({failures} failure{'s' if failures != 1 else ''})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
