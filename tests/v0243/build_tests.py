from pathlib import Path
import subprocess
b=Path.cwd();r=b/'repo';src=r/'source/CPyMO-main'
flags=['-O1','-g','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-DTEXT_CHARACTER_W_SCALE=1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
# Build all production translation units with sanitizers for actual UI paths.
subprocess.run(['python3',str(r/'tests/v0243/build_core_gcc.py'),str(r),str(b/'asanbuild'),'--host','--sanitize'],check=True,stdout=open(b/'asanbuild.log','w'),stderr=subprocess.STDOUT)
objects=[str(p) for p in (b/'asanbuild').glob('*.o') if not any(p.name.endswith('_'+s+'.o') for s in ['libretro','cpymo_msgbox_ui','cpymo_backlog','cpymo_config_ui'])]
subprocess.run(['gcc',*flags,str(r/'tests/v0243/render_check.c'),*objects,'-lm','-o',str(b/'render_check')],check=True)
subprocess.run(['gcc',*flags,'-I'+str(src/'cpymo-backends/libretro'),'-I'+str(src/'cpymo'),str(r/'tests/v0243/capture.c'),'-lm','-ldl','-o',str(b/'capture.asan')],check=True)
