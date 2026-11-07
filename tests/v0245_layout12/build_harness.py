"""Build the same read-only integration observer for Linux host and ARM."""
from pathlib import Path
import os
import subprocess

b = Path.cwd()
repo = b / 'repo'
sdk = Path(os.environ['SDK'])
sysroot = sdk / 'arm-miyoo-linux-uclibcgnueabi/sysroot'
gcclib = sdk / 'lib/gcc/arm-miyoo-linux-uclibcgnueabi/9.4.0'
src = repo / 'source/CPyMO-main'
inc = ['-I' + str(src / 'cpymo'), '-I' + str(src / 'cpymo-backends/libretro')]
flags = ['-O2', '-DDISABLE_MOVIE', '-DDISABLE_FFMPEG_AUDIO', '-DGARLIC_NO_FFMPEG',
         '-DTEXT_CHARACTER_W_SCALE=1']
c = repo / 'tests/v0245_layout12/capture.c'
subprocess.run(['gcc', *flags, *inc, str(c), '-ldl', '-lm', '-o', str(b / 'capture.host')], check=True)
arm = ['clang', '--target=armv7-unknown-linux-gnueabi', '--sysroot=' + str(sysroot),
       '-mcpu=cortex-a9', '-mfloat-abi=softfp', '-mfpu=vfpv3']
subprocess.run([*arm, *flags, *inc, '-c', str(c), '-o', str(b / 'capture.o')], check=True)
subprocess.run([*arm, '-fuse-ld=lld', '-nostdlib', '-no-pie',
                '-Wl,-dynamic-linker=/lib/ld-uClibc.so.1',
                '-Wl,--hash-style=both', str(sysroot / 'usr/lib/crt1.o'),
                str(sysroot / 'usr/lib/crti.o'), str(b / 'capture.o'),
                str(sysroot / 'usr/lib/crtn.o'), '-L' + str(sysroot / 'usr/lib'),
                '-L' + str(sysroot / 'lib'), '-L' + str(gcclib),
                '-ldl', '-lm', '-lc', '-lgcc', '-o', str(b / 'capture.arm')], check=True)
print('Built host and ARM integration harnesses')
