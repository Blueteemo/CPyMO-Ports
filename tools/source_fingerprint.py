#!/usr/bin/env python3
"""Stable fingerprint for the Libretro compilation inputs and launcher."""
from pathlib import Path
import hashlib
import sys
ROOT = Path(__file__).resolve().parent.parent
files = [ROOT / 'tools/build_arm_clang_lld.sh', ROOT / 'deploy/Roms/APPS/CPyMO.sh']
for d in ('source/CPyMO-main/cpymo', 'source/CPyMO-main/cpymo-backends/garlicos',
          'source/CPyMO-main/cpymo-backends/software', 'source/CPyMO-main/cpymo-backends/libretro'):
    files.extend(p for p in (ROOT / d).rglob('*') if p.suffix in ('.c', '.h', '.T') and p.is_file())
h = hashlib.sha256()
for p in sorted(files):
    h.update(str(p.relative_to(ROOT)).encode() + b'\x00')
    h.update(hashlib.sha256(p.read_bytes()).digest())
print(h.hexdigest())
