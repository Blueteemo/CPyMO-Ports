import sys
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/"repo/tests/v0245"))
"""Video lifecycle, invalid-media rejection and named-dialogue regression."""
from pathlib import Path
import json
import os
import shutil
import struct
import sys
from run import b, batch, run

mode = sys.argv[1] if len(sys.argv) > 1 else 'host'
sanitize = '--sanitize' in sys.argv
core = b / ('asanbuild' if sanitize else ('build-arm' if mode == 'arm' else 'hostbuild')) / 'cpymo_libretro.so'
base = b / 'fixtures/800x600'
yume = Path(os.environ['CPYMO_QA_YUME'])
results = []


def check(name, game, events=(), frames=600, menu=False, conditions=()):
    out = run(name, game, events, frames=frames, menu=menu, mode=mode,
              core=core, sanitize=sanitize)
    log = (out / 'run.log').read_text()
    receipt = json.loads((out / 'command.json').read_text())
    ok = receipt['exit'] == 0 and all(c(log) for c in conditions)
    results.append({'case': name, 'passed': ok, 'exit': receipt['exit']})
    if not ok:
        print(log[-2500:], flush=True)
    return out


def fixture(name, script):
    out = batch / ('fixture_' + name)
    shutil.copytree(base, out)
    (out / 'script/start.txt').write_text(script)
    cfg = out / 'gameconfig.txt'
    cfg.write_text(cfg.read_text() + 'playvideo,1\n')
    (out / 'video').mkdir()
    return out


named = fixture('named', '#bg test\n#say 小岚,名字与正文同时切换字号。\n#wait 999999\n')
font_events = [(180, 4)] + [(210 + i * 15, 32) for i in range(4)]
font_events += [(300, 64), (360, 1)]
check('named_font_toggle', named, font_events, frames=480,
      conditions=[lambda s: 'large=2' in s and 'shutdown=0' in s])
repeat_font = font_events[:-2] + [(300 + i * 12, 64 if i%2==0 else 128) for i in range(20)] + [(600, 1)]
check('named_font_repeat_20', named, repeat_font, frames=720,
      conditions=[lambda s: 'large=2' in s and 'large=0' in s and 'shutdown=0' in s,
                  lambda s: 'history=2' not in s])

# Muting voice must not suppress an independent, non-looping sound effect.
se = fixture('se_voice_muted', '#bg test\n#se sound\n#say ,音效独立于语音。\n#wait 999999\n')
(se / 'se').mkdir()
shutil.copyfile(se / 'bgm/song.wav', se / 'se/sound.wav')
cfg = se / 'gameconfig.txt'
cfg.write_text(cfg.read_text() + 'vovolume,0\n')
check('se_with_voice_muted', se, frames=240,
      conditions=[lambda s: 'CHANNELS frame=120 bgm=0 se=1 voice=0' in s])

vo = fixture('voice_only', '#bg test\n#vo sound\n#say 小岚,语音通道验证。\n#wait 999999\n')
(vo / 'voice').mkdir()
shutil.copyfile(vo / 'bgm/song.wav', vo / 'voice/sound.wav')
cfg = vo / 'gameconfig.txt'
cfg.write_text(cfg.read_text() + 'vovolume,5\n')
check('voice_only', vo, frames=240,
      conditions=[lambda s: 'CHANNELS frame=120 bgm=0 se=0 voice=1' in s])

stopped = fixture('bgm_stopped', '#bg test\n#bgm song,1\n#say ,播放音乐\n#bgm_stop\n#say ,保持安静\n#wait 999999\n')
check('stopped_bgm_save_load', stopped, [(180, 256), (240, 8192), (300, 4096)], frames=480,
      conditions=[lambda s: 'CHANNELS frame=120 bgm=1' in s,
                  lambda s: 'CHANNELS frame=360 bgm=0' in s and 'quick load slot 1: 0' in s])

# All movie commands are no-ops, even with stale corrupt experimental sidecars.
movie = fixture('movies_disabled', '#movie opening\n#bg test\n#movie a_ed\n#movie m_ed\n#movie n_ed\n#movie absent\n#say ,安全跳过所有影片后继续。\n#wait 999999\n')
(movie/'video/opening.rg35xx.avi').write_bytes(b'corrupt old experiment')
check('movies_disabled_with_stale_sidecar',movie,frames=360,
      conditions=[lambda s:'ui=0 say=1' in s and 'MOVIE start' not in s and 'shutdown=0' in s])
loop = fixture('cache', '#bg test\n#wait 20\n#label again\n#bg test\n#wait 20\n#goto again\n')
check('cache_repeat_image',loop,frames=600,
      conditions=[lambda s:'shutdown=0' in s,
                  lambda s:any(int(n)>10 for n in __import__('re').findall(r'hits=(\d+)',s))])
(batch/'summary.json').write_text(json.dumps({'mode':mode,'sanitize':sanitize,'results':results},indent=2)+'\n')
print('BATCH',batch)
assert all(r['passed'] for r in results),results
