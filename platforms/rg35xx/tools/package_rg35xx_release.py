#!/usr/bin/env python3
"""Package the RG35XX installer from a matching ARM build."""
import argparse
from hashlib import sha256
from pathlib import Path
import subprocess
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[1]
VERSION = '1.0.0'
CORE = 'Roms/APPS/CPyMO/cpymo_libretro.so'
APP_FILES = (
    'Roms/APPS/CPyMO.sh',
    'Roms/APPS/CPyMO/licenses/FONT-NOTICE.txt',
    'Roms/APPS/CPyMO/licenses/OFL-SourceHanSans.txt',
    'Roms/APPS/CPyMO/retroarch-cpymo.cfg',
    'Roms/APPS/CPyMO/system/default.ttf',
    'Roms/APPS/CPyMO/system/startup.png',
    'Roms/APPS/Imgs/CPyMO.png',
)


def digest(data):
    return sha256(data).hexdigest()


def manifest(files):
    return ''.join(f'{digest(data)}  {name}\n' for name, data in sorted(files.items())).encode()


def package(build, output):
    provenance = (build / 'BUILD_PROVENANCE.txt').read_bytes()
    facts = dict(line.split('=', 1) for line in provenance.decode().splitlines())
    core = (build / Path(CORE).name).read_bytes()
    if facts['mode'] != 'softfp' or facts['sha256'] != digest(core):
        raise ValueError('Build provenance does not match the RG35XX softfp binary')
    commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    fingerprint = subprocess.check_output(
        ['python3', str(ROOT / 'tools/source_fingerprint.py')], text=True,
    ).strip()
    if (facts['source_git_commit'] != commit or facts['source_git_dirty'] != 'no'
            or facts['source_tree_sha256'] != fingerprint):
        raise ValueError('Build provenance does not match the checked-out source')
    if f'CPYMO_CORE version={VERSION} '.encode() not in core:
        raise ValueError(f'Expected a CPyMO {VERSION} core')
    files = {name: (ROOT / 'deploy' / name).read_bytes() for name in APP_FILES}
    files.update({
        CORE: core,
        'BUILD_PROVENANCE.txt': provenance,
        'LICENSE-AGPL-3.0.txt': (ROOT / 'source/CPyMO-main/LICENSE').read_bytes(),
        '安装指南.md': (ROOT / 'docs/INSTALL_RG35XX.md').read_bytes(),
    })
    artwork_docs = ROOT.parents[1] / 'docs/third-party'
    for name in ('PYMO-ICON-ATTRIBUTION.md', 'PYMO-ORIGINAL-MIT-LICENSE.txt'):
        files[name] = (artwork_docs / name).read_bytes()
    files['SHA256SUMS.txt'] = manifest(files)

    output.mkdir(parents=True, exist_ok=True)
    archive = output / f'CPyMO_RG35XX_{VERSION}_GarlicOS.zip'
    with ZipFile(archive, 'w', compression=ZIP_DEFLATED) as installer:
        for name, data in sorted(files.items()):
            entry = ZipInfo(name)
            entry.create_system = 3
            entry.external_attr = (0o100755 if name.endswith('.sh') else 0o100644) << 16
            entry.compress_type = ZIP_DEFLATED
            installer.writestr(entry, data)
    with ZipFile(archive) as installer:
        if installer.testzip() or set(installer.namelist()) != files.keys():
            raise ValueError('Installer archive verification failed')
        if any(installer.read(name) != data for name, data in files.items()):
            raise ValueError('Installer contents differ from the input files')
    (output / 'SHA256SUMS.txt').write_bytes(manifest({archive.name: archive.read_bytes()}))
    print(archive.resolve())
    print(f'Core SHA-256 {facts["sha256"]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, help='directory containing the ARM core and its provenance')
    parser.add_argument('output', type=Path, help='directory for the installer and checksum file')
    args = parser.parse_args()
    package(args.build, args.output)


if __name__ == '__main__':
    main()
