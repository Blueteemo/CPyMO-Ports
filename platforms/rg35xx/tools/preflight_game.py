#!/usr/bin/env python3
"""Read-only preflight for a CPyMO SDL1 RG35XX game directory.

This is a preliminary *file/layout* check, not a proof of full game script
compatibility, font glyph coverage, decompression, or target performance.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import sys

def preflight(root: Path) -> tuple[list[str],list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    if not root.is_dir():
        return [f"game directory not found: {root}"], warnings
    cfg = root / "gameconfig.txt"
    if not cfg.is_file():
        return [f"missing gameconfig.txt: {cfg}"], warnings
    config: dict[str,list[str]] = {}
    try:
        for n,line in enumerate(cfg.read_text(encoding='utf-8-sig').splitlines(),1):
            line = line.strip()
            if not line or line.startswith(('#',';')):
                continue
            # The PyMO parser accepts comma-separated key/value fields.
            fields = [s.strip() for s in line.split(',')]
            if len(fields)<2:
                warnings.append(f"unrecognized config line {n}: {line[:90]}")
            else:
                config[fields[0].lower()] = fields[1:]
    except UnicodeError:
        errors.append("gameconfig.txt cannot be read as UTF-8; check encoding against the target CPyMO parser")
        return errors,warnings
    dims = config.get('imagesize',[])
    if len(dims)!=2 or any(not s.isdecimal() for s in dims):
        errors.append("imagesize must provide two decimal integers")
    else:
        w,h = map(int,dims)
        if min(w,h)<=0:
            errors.append("imagesize dimensions must be positive")
        elif max(w,h)>2048:
            warnings.append("very large logical canvas; RG35XX allocation/performance requires extra review")
        elif w>640 or h>480:
            warnings.append("logical resolution exceeds RG35XX screen; downscale performance requires review")
    scripts = root / 'script'
    if not scripts.is_dir(): errors.append("missing script/ directory")
    else:
        start = config.get('startscript',['start'])[0] or 'start'
        if not re.fullmatch(r'[\w\-]+',start,flags=re.UNICODE):
            warnings.append("nonstandard startscript name; check the exact CPyMO script-loading rules")
        if not (scripts / (start+'.txt')).is_file():
            errors.append(f"missing startup script: script/{start}.txt")
    if not any((root/'system'/name).is_file() for name in ('default.ttf','default.otf','default.fnt')):
        errors.append("missing system/default.ttf, .otf or .fnt (SDL_ttf backend cannot start without game font)")
    if not (root/'save').is_dir():
        warnings.append("save/ absent; launcher will attempt to create it (SD card must be writable)")
    else:
        warnings.append("save/ exists; real target SD write permissions still require hardware validation")
    return errors,warnings

def main() -> int:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('game_dir',type=Path)
    args=ap.parse_args()
    errors,warnings=preflight(args.game_dir)
    for t in errors: print('ERROR:',t)
    for t in warnings: print('WARN: ',t)
    print('PASS: layout preflight only (NOT game runtime validation)' if not errors else 'FAIL: game layout preflight')
    return 1 if errors else 0
if __name__=='__main__': sys.exit(main())
