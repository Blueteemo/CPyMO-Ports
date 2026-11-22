"""Isolated real-core runs. No writes to original games, card or old saves."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import time

b = Path.cwd()
sdk = Path(os.environ['SDK'])
batch = b / 'runs' / (time.strftime('%Y%m%d_%H%M%S') + '_' + str(time.time_ns() % 1000000))
batch.mkdir(parents=True)


def offsets(core):
    result = {}
    wanted = {'engine': 'ENGINE', 'cpymo_audio_get_se_name': 'SE_NAME',
              'cpymo_audio_channel_is_playing': 'SE_PLAYING',
              'cpymo_audio_channel_is_looping': 'SE_LOOPING',
              'cpymo_audio_get_channel_volume': 'VOLUME'}
    for line in subprocess.check_output(['nm', str(core)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] in wanted:
            result['CPYMO_TEST_' + wanted[fields[2]] + '_OFFSET'] = fields[0]
    assert len(result) == len(wanted), result
    return result


def run(name, game, events=(), frames=600, mode='host', menu=False,
        core=None, scenario=None, copy=True, capture_every=None, harness=None, saves_seed=None,
        sanitize=False, font_override=True):
    out = batch / name
    out.mkdir()
    if copy:
        shutil.copytree(game, out / 'game')
        game = out / 'game'
    core = Path(core or b / ('build-arm' if mode == 'arm' else 'hostbuild') / 'cpymo_libretro.so').resolve()
    tl = out / 'timeline.txt'
    tl.write_text(''.join(f'{f} {mask}\n' for f, mask in events))
    env = os.environ.copy()
    for k in list(env):
        if k.startswith('CPYMO_'):
            del env[k]
    env.update(offsets(core))
    env.update(CPYMO_DEBUG='1', CPYMO_TEST_FRAMES=str(frames),
               CPYMO_TEST_CAPTURE_EVERY=str(capture_every or frames))
    if capture_every is None:
        env['CPYMO_TEST_NO_CAPTURE'] = '1'
    if font_override:
        env['CPYMO_MENU_FONT'] = os.environ['CPYMO_QA_FONT']
    if not copy:
        env['CPYMO_SAVE_ROOT'] = str(out / 'saves')
        if saves_seed:
            shutil.copytree(saves_seed, out / 'saves')
    if not menu:
        env['CPYMO_MENU'] = '0'
    if scenario == 'train':
        env['CPYMO_TEST_TRAIN'] = '1'
    if sanitize:
        env['LD_PRELOAD'] = subprocess.check_output(['gcc', '-print-file-name=libasan.so'], text=True).strip()
        env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
        env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    cmd = ([shutil.which('qemu-arm'), '-L', str(sdk / 'arm-miyoo-linux-uclibcgnueabi/sysroot')]
           if mode == 'arm' else []) + [str(harness or b / ('capture.' + mode)), str(core),
                                        str(game), str(out), str(tl)]
    if Path('/usr/bin/time').exists():
        cmd = ['/usr/bin/time', '-v', '-o', str(out / 'process_metrics.txt'), *cmd]
    with (out / 'run.log').open('w') as log:
        p = subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT)
    receipt = {'case': name, 'mode': mode, 'exit': p.returncode, 'command': cmd,
               'environment': {k: v for k, v in env.items() if k.startswith('CPYMO_')}}
    (out / 'command.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n')
    print(name, p.returncode, flush=True)
    if p.returncode:
        print((out / 'run.log').read_text()[-2000:], flush=True)
    return out


if __name__ == '__main__':
    # Set up source fixtures once with the supplied real font.
    font = Path(os.environ['CPYMO_QA_FONT'])
    subprocess.run(['python3', str(b / 'repo/tests/v0243/make_fixtures.py'),
                    str(b / 'fixtures'), str(font)], check=True)
    game = b / 'fixtures/540x360'
    run('direct', game)
    run('single_library', game, [(20, 256)], menu=True)
    print('BATCH', batch)
