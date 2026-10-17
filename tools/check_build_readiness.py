#!/usr/bin/env python3
"""Report the current *real* SDK status; no fake fallback and no silent build claims."""
from pathlib import Path
import os,shutil
exe=os.environ.get('CC','arm-linux-gnueabihf-gcc')
fields={
 'ARM compiler': bool(shutil.which(exe)),
 'ARM sysroot stdio.h': (Path(os.getenv('SDK_SYSROOT','/NO_SDK'))/'usr/include/stdio.h').is_file(),
 'real SDL1 header': (Path(os.getenv('SDL_INCLUDE_DIR','/NO_SDL'))/'SDL/SDL.h').is_file(),
 'real SDL_ttf header': (Path(os.getenv('SDL_INCLUDE_DIR','/NO_SDL'))/'SDL/SDL_ttf.h').is_file(),
 'glibc 2.15 dynamic lib': (Path(os.getenv('GLIBC_LIB_DIR','/NO_GLIBC'))/'libc.so.6').is_file(),
 'ARM SDL1 lib': (Path(os.getenv('SDL_LIB_DIR','/NO_ARM_LIBS'))/'libSDL.so').exists(),
}
for k,v in fields.items():print(('PASS' if v else 'BLOCKED'),k)
print('READY_FOR_ARM_BUILD',all(fields.values()))
