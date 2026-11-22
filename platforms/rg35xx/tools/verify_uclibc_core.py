#!/usr/bin/env python3
"""Fail-closed ELF sanity check for original RG35XX GarlicOS Libretro core.

Does not prove machine compatibility; compare floating ABI and exports with
an actual working core extracted from the user's SD card before deployment.
"""
from pathlib import Path
import re,subprocess,sys

def readelf(binary,*opts):
    p=subprocess.run(['readelf',*opts,str(binary)],text=True,capture_output=True)
    if p.returncode:raise RuntimeError('readelf '+str(opts)+' failed: '+p.stderr.strip())
    return p.stdout

def verify(path):
    header=readelf(path,'-h');dyn=readelf(path,'-d');symbols=readelf(path,'-Ws');attrs=readelf(path,'-A')
    if not re.search(r'Class:\s+ELF32',header):raise RuntimeError('Expected ELF32')
    if not re.search(r'Machine:\s+ARM',header):raise RuntimeError('Expected ARM')
    if not re.search(r'Type:\s+DYN',header):raise RuntimeError('Expected shared library (ET_DYN)')
    if 'libc.so.6' in dyn or 'ld-linux-armhf' in dyn or 'GLIBC_' in symbols:
        raise RuntimeError('REFUSED: glibc-linked binary; incompatible with observed uClibc firmware')
    if not re.search(r'Flags:.*soft-float ABI',header):
        raise RuntimeError('REFUSED: expected ARM EABI soft-float calling convention for candidate GarlicOS SDK')
    if 'Tag_ABI_VFP_args:' in attrs:
        raise RuntimeError('REFUSED: unexpected hard-float VFP register calling convention')
    needed=set(re.findall(r'Shared library: \[([^]]+)\]',dyn))
    if not {'libc.so.0','ld-uClibc.so.1'}.issubset(needed):
        raise RuntimeError('REFUSED: missing expected uClibc runtime NEEDED entries')
    unsupported=needed-{'libc.so.0','ld-uClibc.so.1'}
    if unsupported:raise RuntimeError('REFUSED: unknown required runtime dependencies: '+', '.join(sorted(unsupported)))
    exports=readelf(path,'--dyn-syms','--wide')
    for name in ('retro_api_version','retro_init','retro_deinit','retro_get_system_info','retro_get_system_av_info','retro_load_game','retro_run','retro_unload_game'):
        if not any(re.search(r'\bFUNC\s+GLOBAL\s+DEFAULT\s+\d+\s+'+re.escape(name)+r'$',line) for line in exports.splitlines()):
            raise RuntimeError('Missing exported dynamic Libretro function: '+name)
    print('PASS target-structure: ELF32 ARM shared object, expected Libretro exports, no glibc symbol names')
    print('ABI ATTRIBUTES:')
    for line in attrs.splitlines():
        if 'Tag_CPU_arch:' in line or 'Tag_ABI_VFP_args:' in line or 'Tag_FP_arch:' in line:
            print('  '+line.strip())
    print('DYNAMIC DEPENDENCIES:')
    for line in dyn.splitlines():
        if 'NEEDED' in line:print('  '+line.strip())
    print('WARNING: No real RG35XX ABI/runtime/library compatibility proof until hardware test.')

if __name__=='__main__':
    try:
        if len(sys.argv)!=2:raise RuntimeError('Usage: verify_uclibc_core.py <ARM libretro .so>')
        verify(Path(sys.argv[1]))
    except (RuntimeError,OSError) as e:
        print('FAIL:',e,file=sys.stderr);sys.exit(1)
