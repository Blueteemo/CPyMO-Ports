"""Linux: python3 tests/v0245_layout8/run_render.py ASAN_OBJECT_DIR FONT FIXTURE."""
from pathlib import Path
import os, subprocess, sys
repo = Path(__file__).resolve().parents[2]
objects, font, game = map(lambda p: Path(p).resolve(), sys.argv[1:])
flags = ['-O2', '-DNDEBUG', '-std=gnu11', '-DTEXT_CHARACTER_W_SCALE=1',
         '-DDISABLE_MOVIE', '-DDISABLE_FFMPEG_AUDIO', '-DGARLIC_NO_FFMPEG',
         '-DGARLIC_MP3_AUDIO', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all', '-g', '-fno-omit-frame-pointer']
excluded = ['_libretro.o', '_cpymo_backend_text.o', '_cpymo_msgbox_ui.o',
            '_cpymo_backlog.o', '_cpymo_config_ui.o', '_cpymo_textbox.o',
            '_cpymo_select_img.o', '_cpymo_rmenu.o']
linked = [str(p) for p in objects.glob('*.o')
          if not any(p.name.endswith(s) for s in excluded)]
assert linked
binary = objects / 'layout8_render'
subprocess.run(['gcc', *flags, str(Path(__file__).with_name('render_check.c')),
                *linked, '-lm', '-o', str(binary)], check=True, cwd=repo)
subprocess.run([str(binary), str(font)], check=True,
               env={**os.environ, 'CPYMO_QA_GAME_PATH': str(game)}, cwd=repo)
