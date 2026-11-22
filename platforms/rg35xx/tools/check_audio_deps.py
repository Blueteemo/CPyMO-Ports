#!/usr/bin/env python3
"""Resolve the audio core's undefined symbols against the SDK's own libraries.

    python3 tools/check_audio_deps.py <core.so> [--sdk <sysroot>]

Why this exists
--------------
A shared object with one symbol the loader cannot resolve fails at `dlopen`, which
on the RG35XX looks like "the core does nothing". `readelf` output is the only
evidence available off-device, so this checks the whole contract at once:

  * every symbol the core imports (UND in .dynsym) is either
      - provided by one of the libraries named in the core's DT_NEEDED, or
      - provided by a library in the sysroot (so a NEEDED entry could be
        added without a rebuild),
  * and no symbol is left over.

The libraries' exported symbols are read from the ARM .so files themselves, so
the answer is about the binaries the device will load, not about headers.

Exit codes: 0 all resolved, 1 unresolved symbols, 2 usage/parse failure.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

READELF_NAMES = (
    r"C:\tools\msys64\msys64\ucrt64\bin\arm-none-eabi-readelf.exe",
    r"C:\msys64\ucrt64\bin\arm-none-eabi-readelf.exe",
    "/c/tools/msys64/msys64/ucrt64/bin/arm-none-eabi-readelf.exe",
)


def find_readelf():
    """The ARM binutils, from ARM_READELF or the usual MSYS2 locations.

    Both path spellings are listed because a Git-Bash Python and a native
    Windows Python disagree about which one is a valid path.
    """
    import os
    import shutil
    for cand in (os.environ.get("ARM_READELF", ""),) + READELF_NAMES:
        if cand and Path(cand).is_file():
            return Path(cand)
    found = shutil.which("arm-none-eabi-readelf")
    return Path(found) if found else None


READELF = find_readelf()

DEFAULT_SDK = Path("research/local-sdk/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot"
                  "/arm-miyoo-linux-uclibcgnueabi/sysroot")

# Symbol names that a dynamic loader resolves to the executable/loader itself.
WEAK_OK = {"__cxa_finalize", "__gmon_start__", "_ITM_deregisterTMCloneTable",
           "_ITM_registerTMCloneTable", "__deregister_frame_info",
           "__register_frame_info"}


def run(*args: str) -> str:
    out = subprocess.run(args, capture_output=True, text=True)
    if out.returncode != 0 and not out.stdout:
        raise RuntimeError(f"{' '.join(args)}: {out.stderr.strip()}")
    return out.stdout


def dynsyms(path: Path) -> tuple[set[str], set[str]]:
    """(defined, undefined) symbol names in a .so."""
    text = run(str(READELF), "--dyn-syms", "--wide", str(path))
    defined, undefined = set(), set()
    for line in text.splitlines():
        m = re.match(r"\s*\d+:\s+([0-9a-f]+)\s+(\d+)\s+(\w+)\s+(\w+)\s+(\w+)\s+(\S+)\s*(.*)$",
                     line)
        if not m:
            continue
        value, _size, _type, bind, _vis, ndx, name = m.groups()
        name = name.split("@")[0].strip()
        if not name or bind == "LOCAL":
            continue
        if ndx == "UND":
            undefined.add(name)
        elif value != "0" or ndx != "UND":
            defined.add(name)
    return defined, undefined


def needed(path: Path) -> list[str]:
    text = run(str(READELF), "-d", str(path))
    return re.findall(r"Shared library: \[([^\]]+)\]", text)


def find_in_sysroot(sysroot: Path, soname: str) -> Path | None:
    for base in (sysroot / "usr/lib", sysroot / "lib",
                 sysroot / "usr/lib/arm-miyoo-linux-uclibcgnueabi"):
        hit = base / soname
        if hit.exists():
            return hit
        glob = sorted(base.glob(soname + ".*"))
        if glob:
            return glob[-1]
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("core", type=Path)
    ap.add_argument("--sdk", type=Path, default=DEFAULT_SDK)
    a = ap.parse_args()
    if READELF is None or not READELF.exists():
        print(f"no ARM readelf found (set ARM_READELF); tried {READELF}",
              file=sys.stderr)
        return 2
    if not a.core.is_file():
        print(f"no such core: {a.core}", file=sys.stderr)
        return 2

    _defined, undefined = dynsyms(a.core)
    undefined -= WEAK_OK
    deps = needed(a.core)
    print(f"core: {a.core}")
    print(f"NEEDED: {' '.join(deps)}")
    print(f"undefined symbols: {len(undefined)}")

    provided: dict[str, str] = {}
    for soname in deps:
        lib = find_in_sysroot(a.sdk, soname)
        if lib is None:
            print(f"  {soname}: NOT FOUND in the sysroot")
            continue
        defined, _u = dynsyms(lib)
        for sym in defined:
            provided.setdefault(sym, soname)

    # The loader is not in DT_NEEDED but is mapped before the core; on uClibc
    # it owns the TLS helper, which is exactly the symbol a link check like
    # this would otherwise report as unresolved.
    loader = find_in_sysroot(a.sdk, "ld-uClibc.so.1")
    if loader is not None:
        defined, _u = dynsyms(loader)
        for sym in defined:
            provided.setdefault(sym, "ld-uClibc.so.1 (loader)")

    resolved = {s: provided[s] for s in sorted(undefined) if s in provided}
    unresolved = sorted(s for s in undefined if s not in provided)

    from collections import Counter
    per_lib = Counter(resolved.values())
    for lib, n in per_lib.most_common():
        print(f"  resolved by {lib}: {n}")
    runtime = [s for s in resolved if not s.startswith("av_")
               and not s.startswith("avcodec_")
               and not s.startswith("avformat_")
               and not s.startswith("swr_")
               and not s.startswith("sws_")]
    print(f"  CPyMO/runtime symbols resolved: {len(runtime)}"
          f" (e.g. {', '.join(runtime[:4])})")

    if unresolved:
        print()
        print("UNRESOLVED (the loader would fail on these):")
        for s in unresolved:
            print(f"  {s}")
        return 1

    print()
    print("OK: every undefined symbol resolves against the core's own runtime.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
