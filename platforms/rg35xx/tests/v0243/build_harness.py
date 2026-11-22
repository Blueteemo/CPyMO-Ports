from pathlib import Path
import subprocess
b=Path.cwd();src=b/'repo/source/CPyMO-main'; sdk=b/'handoff/04_构建环境与脚本/local-sdk/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot'
p=b/'repo/tests/v0243/capture.c'
for mode,cc in [('host','gcc'),('arm',str(sdk/'bin/arm-miyoo-linux-uclibcgnueabi-gcc'))]:
 cmd=[cc,'-O2','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-I'+str(src/'cpymo-backends/libretro'),'-I'+str(src/'cpymo'),str(p),'-ldl','-lm','-o',str(b/f'capture.{mode}')];subprocess.run(cmd,check=True)
