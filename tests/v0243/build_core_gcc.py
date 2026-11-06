from pathlib import Path
import subprocess,sys,hashlib,json
root=Path(sys.argv[1]).resolve(); out=Path(sys.argv[2]).resolve(); out.mkdir(exist_ok=True)
sdk=Path('handoff/04_构建环境与脚本/local-sdk/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot').resolve()
cc=str(sdk/'bin/arm-miyoo-linux-uclibcgnueabi-gcc')
host='--host' in sys.argv
if host: cc='gcc'
src=root/'source/CPyMO-main'; backend=src/'cpymo-backends'
files=[backend/p for p in ['libretro/libretro.c','libretro/cpymo_audio_mp3.c','libretro/stb_vorbis.c','garlicos/garlic_adapter.c','garlicos/garlic_paths.c','software/cpymo_backend_image.c','software/cpymo_backend_software.c','software/cpymo_backend_text.c','software/cpymo_backend_masktrans.c']]+sorted((src/'cpymo').glob('*.c'))
flags=['-mcpu=cortex-a9','-mfloat-abi=softfp','-mfpu=vfpv3','-O2','-DNDEBUG','-fPIC','-std=gnu11','-DTEXT_CHARACTER_W_SCALE=1','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-DGARLIC_MP3_AUDIO','-Wall','-Werror=implicit-function-declaration']
if host: flags=flags[3:]
if '--sanitize' in sys.argv: flags += ['-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-fno-omit-frame-pointer']
objects=[]
for i,f in enumerate(files):
 o=out/f'{i:03d}_{f.stem}.o'; subprocess.run([cc,*flags,'-c',str(f),'-o',str(o)],check=True); objects.append(str(o))
core=out/'cpymo_libretro.so'
subprocess.run([cc,*flags,'-shared','-Wl,--hash-style=both','-Wl,--no-undefined','-Wl,--version-script='+str(backend/'libretro/link.T'),*objects,'-lm',*(['-fsanitize=address,undefined'] if '--sanitize' in sys.argv else []),'-o',str(core)],check=True)
if not host: subprocess.run(['python3',str(root/'tools/verify_uclibc_core.py'),str(core)],check=True)
print('SOURCE_UNITS',len(files),'SHA256',hashlib.sha256(core.read_bytes()).hexdigest())
