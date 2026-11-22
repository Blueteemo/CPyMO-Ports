#!/usr/bin/env python3
"""Package ONLY a newly built, verified v0.23 SOFTFP core.

Usage: python3 tools/make_rg35xx_v023_softfp_zip.py [output.zip]
This tool refuses the historical v0.22 experiment, absent/mismatched build
provenance, hard-float ABI, missing dual hash, and missing Libretro exports.
"""
from __future__ import annotations
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent.parent
CORE = ROOT / 'build_arm_softfp/cpymo_libretro.so'
PROVENANCE = CORE.with_name('BUILD_PROVENANCE.txt')
SAMPLE = ROOT / 'deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test'
SCRIPT = ROOT / 'deploy/Roms/APPS/CPyMO.sh'
DEST = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'CPyMO_RG35XX_v023_SOFTFP_next_candidate.zip')


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify():
    for path in (CORE, PROVENANCE, SAMPLE / 'gameconfig.txt', SCRIPT):
        if not path.is_file():
            raise RuntimeError(f'Missing required file: {path}')
    data = dict(line.split('=', 1) for line in PROVENANCE.read_text().splitlines() if '=' in line)
    if data.get('mode') != 'softfp':
        raise RuntimeError('Core is not proven to be softfp')
    if data.get('sha256') != digest(CORE):
        raise RuntimeError('Core does not match build provenance SHA256')
    commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    if data.get('source_git_commit') != commit:
        raise RuntimeError('Source commit differs from the compiled core provenance')
    current_tree = subprocess.check_output([sys.executable, str(ROOT / 'tools/source_fingerprint.py')], text=True).strip()
    if data.get('source_tree_sha256') != current_tree:
        raise RuntimeError('Compilable source or launcher changed since the recorded build')
    subprocess.run([sys.executable, str(ROOT / 'tools/verify_uclibc_core.py'), str(CORE)], check=True)
    dyn = subprocess.check_output(['readelf', '-d', str(CORE)], text=True)
    if '(HASH)' not in dyn or '(GNU_HASH)' not in dyn:
        raise RuntimeError('Core must have both SysV/GNU ELF hash tables')
    s = subprocess.check_output(['strings', str(CORE)], text=True)
    if 'CPYMO_LOAD' not in s or 'CPYMO_SELECT' not in s or 'CPYMO_PERF' not in s:
        raise RuntimeError('Core does not contain the v0.23 game-select/perf features')
    return data


def zip_file(z: zipfile.ZipFile, destination: str, path: Path, executable: bool = False):
    zi = zipfile.ZipInfo(destination)
    zi.create_system = 3
    zi.external_attr = (0o100755 if executable else 0o100644) << 16
    zi.compress_type = zipfile.ZIP_DEFLATED
    z.writestr(zi, path.read_bytes())


def main():
    provenance = verify()
    DEST.parent.mkdir(parents=True, exist_ok=True)
    readme = '''CPyMO / Original RG35XX / GarlicOS 1.4.9 / v0.23 SOFTFP candidate\n\nTHIS PACKAGE IS UNTESTED ON REAL HARDWARE until its device logs are reviewed.\nThis is a current-source softfp build, not the historical v0.22 experiment.\n\nBefore installing, save your latest logs and back up saves & game folders.\nDelete only prior temporary test entries (when present):\n  CPyMO_v023_RC1.sh\n  CPyMO_v023_RC1/\n  CPyMO_v023_RC1_Demo/\n  CPyMO_FPU_AB_SOFT.sh / CPyMO_FPU_AB_SOFT/\n  CPyMO_FPU_AB_SOFTFP.sh / CPyMO_FPU_AB_SOFTFP/\nDO NOT delete Roms/APPS/saves, real games, or other apps.\n\nCopy Roms/APPS/ contents to the matching folder on the SD card.\nLaunch CPyMO.sh from GarlicOS APPS. The current version stays in CPyMO/.\nLogs: Roms/APPS/CPyMO.log.\nAudio is intentionally disabled. Do NOT overwrite a previously working core\nwithout first backing up your save files and launcher.\n'''
    with zipfile.ZipFile(DEST, 'w') as z:
        zi = zipfile.ZipInfo('README_安装和清理.txt')
        zi.create_system = 3
        zi.external_attr = 0o100644 << 16
        z.writestr(zi, readme.encode('utf-8'))
        zip_file(z, 'Roms/APPS/CPyMO.sh', SCRIPT, executable=True)
        zip_file(z, 'Roms/APPS/CPyMO/cpymo_libretro.so', CORE)
        zip_file(z, 'Roms/APPS/CPyMO/BUILD_PROVENANCE.txt', PROVENANCE)
        font = SAMPLE / 'system/default.ttf'
        if font.is_file():
            zip_file(z, 'Roms/APPS/CPyMO/system/default.ttf', font)
        for p in sorted(SAMPLE.rglob('*')):
            if p.is_file() and p.name != 'cpymo_libretro.so' and p.suffix != '.csav':
                zip_file(z, 'Roms/APPS/CPyMO_Demo/' + p.relative_to(SAMPLE).as_posix(), p)
    with zipfile.ZipFile(DEST) as z:
        bad = z.testzip()
        if bad:
            raise RuntimeError(f'Corrupted archive member: {bad}')
    print('PASS package:', DEST)
    print('CORE SHA256:', provenance['sha256'])
    print('ZIP SHA256:', digest(DEST))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, OSError, ValueError) as exc:
        print('BLOCKED:', exc, file=sys.stderr)
        sys.exit(3)
