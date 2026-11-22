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
font_events += [(300, 128), (360, 1)]
check('named_font_toggle', named, font_events, frames=480,
      conditions=[lambda s: 'large=1' in s and 'shutdown=0' in s])
repeat_font = font_events[:-2] + [(300 + i * 12, 128) for i in range(20)] + [(600, 1)]
check('named_font_repeat_20', named, repeat_font, frames=720,
      conditions=[lambda s: 'large=1' in s and 'large=0' in s and 'shutdown=0' in s,
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

short = Path(os.environ['CPYMO_QA_SHORT_MOVIE'])
loop = fixture('movie_loop', '#label again\n#movie opening\n#goto again\n')
shutil.copyfile(short, loop / 'video/opening.rg35xx.avi')
check('movie_repeat_30', loop, frames=5600,
      conditions=[lambda s: s.count('MOVIE start') >= 30,
                  lambda s: s.count('skipped=0 failed=0') >= 29,
                  lambda s: 'shutdown=0' in s])

skip = fixture('movie_skip', '#movie opening\n#bg test\n#say ,影片结束后继续。\n#wait 999999\n')
shutil.copyfile(short, skip / 'video/opening.rg35xx.avi')
check('movie_skip_a', skip, [(120, 256)], frames=360,
      conditions=[lambda s: 'skipped=1 failed=0' in s and 'ui=0 say=1' in s])
check('movie_skip_b', skip, [(120, 1)], frames=360,
      conditions=[lambda s: 'skipped=1 failed=0' in s and 'ui=0 say=1' in s])

truncated = fixture('truncated', '#movie opening\n#wait 999999\n')
data = short.read_bytes()
(truncated / 'video/opening.rg35xx.avi').write_bytes(data[:100])
check('reject_truncated_avi', truncated, [(20, 256)], menu=True, frames=360,
      conditions=[lambda s: 'CPYMO_RETURN_LIBRARY' in s and 'shutdown=0' in s])

oversize = fixture('oversize', '#movie opening\n#wait 999999\n')
bad = bytearray(data)
avih = bad.index(b'avih')
struct.pack_into('<I', bad, avih + 8 + 32, 0xFFFFFFFF)
(oversize / 'video/opening.rg35xx.avi').write_bytes(bad)
check('reject_oversize_avi', oversize, [(20, 256)], menu=True, frames=360,
      conditions=[lambda s: 'CPYMO_RETURN_LIBRARY' in s and 'shutdown=0' in s])

corrupt = fixture('corrupt', '#movie opening\n#wait 999999\n')
bad = bytearray(data)
movi = bad.index(b'movi')
frame = bad.index(b'00dc', movi + 4)
bad[frame + 8:frame + 10] = b'xx'
(corrupt / 'video/opening.rg35xx.avi').write_bytes(bad)
check('reject_corrupt_jpeg', corrupt, [(20, 256)], menu=True, frames=360,
      conditions=[lambda s: 'CPYMO_RETURN_LIBRARY' in s and 'failed=1' in s and 'shutdown=0' in s])

# These test the actual converted ending media and decoder path. They do not
# claim to have played through the game's complete ending branches.
if not sanitize and mode == 'host':
    for movie in ['a_ed', 'm_ed', 'n_ed']:
        f = fixture(movie, f'#movie {movie}\n#bg test\n#say ,片尾结束后继续。\n#wait 999999\n')
        shutil.copyfile(yume / f'video/{movie}.rg35xx.avi', f / f'video/{movie}.rg35xx.avi')
        check('ending_media_' + movie, f, frames=4800,
              conditions=[lambda s: 'skipped=0 failed=0' in s and 'ui=0 say=1' in s,
                          lambda s: 'read_errors=0' in s and 'dropped=0' in s])

(batch / 'summary.json').write_text(json.dumps({'mode': mode, 'sanitize': sanitize,
    'results': results}, indent=2) + '\n')
print('BATCH', batch)
assert all(r['passed'] for r in results), results
