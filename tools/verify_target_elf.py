#!/usr/bin/env python3
"""Fail-closed static ARM ELF check (ABI and interpreter; not hardware verification)."""
import sys,re,subprocess
from pathlib import Path
p=Path(sys.argv[1]); assert p.is_file(),p
R=lambda *argv: subprocess.check_output(['readelf',*argv,str(p)],text=True)
h=R('-h'); dyn=R('-d'); ph=R('-l'); attrs=R('-A'); vs=R('--version-info')
assert 'ELF32' in h and 'Machine:' in h and 'ARM' in h
assert 'hard-float ABI' in h, 'Not explicitly ARM hard-float'
assert '/lib/ld-linux-armhf.so.3' in ph
assert 'Tag_CPU_arch: v7' in attrs and 'VFPv3' in attrs
assert 'Tag_ABI_VFP_args: VFP registers' in attrs, 'Missing ARM hard-float calling-convention marker'
assert 'libSDL-1.2.so.0' in dyn and 'libSDL_ttf-2.0.so.0' in dyn
assert 'libc.so.6' in dyn
versions=[(int(a),int(b)) for a,b in re.findall(r'GLIBC_(\d+)\.(\d+)',vs)]
assert max(versions,default=(0,0)) <= (2,15),f'glibc import is too new: {max(versions)}'
print('PASS ELF32 ARMv7 VFP hard-float, stock interpreter, SDL1/TTF, GLIBC <=2.15')
print('LIMITATION: ELF static checks do not prove target device boot or game compatibility')
