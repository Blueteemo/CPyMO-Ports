from pathlib import Path
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from run import b, batch, run

mode = sys.argv[1] if len(sys.argv) > 1 else 'host'
base = b / 'fixtures/540x360'
font = Path(os.environ['CPYMO_QA_FONT'])


def fixture(name, script, source=base):
    out = batch / ('fixture_' + name)
    shutil.copytree(source, out)
    (out / 'script/start.txt').write_text(script)
    return out


results = []


def check(out, *conditions):
    log = (out / 'run.log').read_text()
    receipt = json.loads((out / 'command.json').read_text())
    ok = receipt['exit'] == 0 and all(cond(log) for cond in conditions)
    results.append({'case': out.name, 'mode': mode, 'passed': ok, 'exit': receipt['exit']})
    if not ok:
        print('ASSERTION FAILED', out.name, log[-1800:], flush=True)
    return log


for dimensions in ['320x240', '540x360', '800x600']:
    check(run('canvas_' + dimensions, b / 'fixtures' / dimensions, mode=mode),
          lambda s: 'shutdown=0' in s and 'ui=0 say=1' in s)

check(run('single_stays_in_library', base, menu=True, mode=mode, frames=180),
      lambda s: 'games=1' in s and 'dir=LIBRARY' in s and 'shutdown=0' in s,
      lambda s: '(direct)' not in s and 'CPYMO_SELECT game=' not in s)

check(run('single_choose', base, [(20, 256)], menu=True, mode=mode),
      lambda s: 'games=1' in s and 'CPYMO_SELECT game=' in s and 'shutdown=0' in s)

check(run('standalone_own_font', base, [(20, 256)], menu=True, mode=mode, font_override=False),
      lambda s: 'games=1' in s and 'CPYMO_SELECT game=' in s and 'shutdown=0' in s)

check(run('exit_library', base, [(30, 1)], menu=True, mode=mode),
      lambda s: 'shutdown=1' in s and 'CPYMO_SELECT game=' not in s)

finite = fixture('finite', '#bg test\n#wait 100\n')
check(run('natural_end', finite, [(20, 256)], menu=True, mode=mode),
      lambda s: 'CPYMO_RETURN_LIBRARY' in s and 'shutdown=0' in s and 'dir=LIBRARY' in s)

broken = fixture('broken', '#change missing_script_for_error_test\n')
check(run('failure_returns', broken, [(20, 256)], menu=True, mode=mode),
      lambda s: 'engine update failed:' in s and 'CPYMO_RETURN_LIBRARY' in s and 'shutdown=0' in s)

# X opens the game menu. The tenth action requires explicit confirmation:
# DOWN selects Cancel from no selection, UP then selects Confirm, A accepts.
back = [(20, 256), (80, 512)] + [(110 + i * 20, 32) for i in range(9)]
back += [(310, 256), (340, 32), (360, 16), (380, 256)]
back = [back[0]] + [(f + 120, mask) for f, mask in back[1:]]
check(run('return_then_restart', base, back + [(560, 256)], menu=True, mode=mode, frames=840),
      lambda s: s.count('CPYMO_SELECT game=') == 2 and 'CPYMO_RETURN_LIBRARY reason=0' in s,
      lambda s: 'shutdown=0' in s)

multi = batch / 'fixture_library'
multi.mkdir()
for name, src in [('Alpha', base), ('Beta', b / 'fixtures/800x600')]:
    shutil.copytree(src, multi / name)
    cfg = multi / name / 'gameconfig.txt'
    cfg.write_text(re.sub(r'^gametitle,.*$', 'gametitle,' + name, cfg.read_text(), flags=re.M))
check(run('switch_two_games', multi, back + [(540, 32), (570, 256)], menu=True,
          mode=mode, frames=840),
      lambda s: 'games=2' in s and '/Alpha canvas=' in s and '/Beta canvas=' in s,
      lambda s: 'shutdown=0' in s)

for row in range(3):
    events = [(60, 4)] + [(90 + i * 12, 32) for i in range(row)]
    events += [(150 + i * 5, 64) for i in range(15)]
    events += [(300 + i * 5, 128) for i in range(15)] + [(450, 1)]
    events = [(f + 120, mask) for f, mask in events]
    out = run('volume_' + str(row), base, events, frames=660, mode=mode)
    def boundary(s, row=row):
        vals = [(int(f), list(map(float, v.split(',')))) for f, v in
                re.findall(r'GAME frame=(\d+).*vols=([0-9.,]+)', s)]
        low = [v[row] for f, v in vals if 350 <= f <= 410]
        high = [v[row] for f, v in vals if 510 <= f <= 660]
        return bool(low and high) and all(v == 0 for v in low) and all(v == 1 for v in high)
    check(out, boundary)
    data = (out / 'game/save/config.csav').read_bytes()
    assert len(data) == 12 and struct.unpack('<6H', data)[row] == 65535

font_events = [(60, 4)] + [(90 + i * 15, 32) for i in range(4)]
font_events += [(180, 128), (240, 1)]
font_events = [(f + 120, mask) for f, mask in font_events]
out = run('font_large', base, font_events, mode=mode, frames=480, capture_every=120)
check(out, lambda s: 'tbfont=31.20000' in s and 'large=1' in s and 'shutdown=0' in s)
check(run('font_large_restart', out / 'game', mode=mode, frames=180),
      lambda s: 'tbfont=31.20000' in s and 'large=1' in s)

read = fixture('read', '#bg test\n#label again\n#say ,已读状态：中文对白保留脚本颜色。\n#goto again\n')
events = [(40, 256), (80, 256), (120, 4)] + [(150 + i * 15, 32) for i in range(6)]
events += [(270, 128), (300, 1)]
events = [(f + 120, mask) for f, mask in events]
out = run('read_gray_on', read, events, mode=mode, frames=540, capture_every=120)
check(out, lambda s: 'read=0 alpha=1.0000' in s and 'read=1 alpha=1.0000' in s,
      lambda s: 'gray=1' in s and 'read=1 alpha=0.7500' in s)
check(run('read_gray_restart', out / 'game', mode=mode, frames=180),
      lambda s: 'gray=1' in s and 'read=1 alpha=0.7500' in s)

sound = fixture('loop', '#bg test\n#se sound,1\n#say ,环境音恢复\n#se_stop\n#say ,环境音停止\n#wait 999999\n')
(sound / 'se').mkdir(exist_ok=True)
shutil.copyfile(sound / 'bgm/song.wav', sound / 'se/sound.wav')
check(run('loop_save_load_stop', sound, [(180, 8192), (240, 4096), (300, 256)],
          mode=mode, frames=540),
      lambda s: 'playing=1 looping=1 saved_name=sound' in s,
      lambda s: 'SE frame=480 playing=0 looping=0 saved_name=' in s)

summary = {'scope': mode + ' integration; no hardware result', 'results': results}
(batch / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
print('BATCH', batch)
assert all(r['passed'] for r in results), summary
