"""Full-track decode/loop and raw-sample parity against the stable core."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import wave

b = Path.cwd()
src = b / 'repo/source/CPyMO-main'
flags = ['-O2', '-std=gnu11', '-DDISABLE_MOVIE', '-DDISABLE_FFMPEG_AUDIO',
         '-DGARLIC_NO_FFMPEG', '-ffunction-sections', '-fdata-sections']
for variant in ['candidate', 'baseline']:
    defs = [] if variant == 'candidate' else [
        '-DCPYMO_AUDIO_BASELINE', '-DAUDIO_SOURCE="' + str(
            b / 'baseline/source/CPyMO-main/cpymo-backends/libretro/cpymo_audio_mp3.c') + '"']
    subprocess.run(['gcc', *flags, *defs, str(b / 'repo/tests/v0244/audio_probe.c'),
                    str(src / 'cpymo/cpymo_str.c'), str(src / 'cpymo/cpymo_package.c'),
                    str(src / 'cpymo-backends/libretro/stb_vorbis.c'), '-Wl,--gc-sections',
                    '-lm', '-o', str(b / ('audio_' + variant))], check=True)


def probe(path):
    row = {'track': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    values = []
    for variant in ['baseline', 'candidate']:
        result = subprocess.run([str(b / ('audio_' + variant)), str(path)],
                                text=True, capture_output=True, check=True)
        row[variant] = result.stdout
        frames = re.findall(r'PASS loop=\d frames=(\d+)', result.stdout)
        hashes = re.findall(r'PCM loop=\d hash=([a-f0-9]+)', result.stdout)
        assert len(frames) == len(hashes) == 2
        assert frames[0] == frames[1] and hashes[0] == hashes[1], row
        values.append((frames, hashes))
    assert values[0] == values[1], row
    row['passed'] = True
    return row


tracks = sorted((Path(os.environ['CPYMO_QA_WATER']) / 'bgm').glob('*.mp3'))
assert len(tracks) == 17
formats = b / 'audioformats'
formats.mkdir(exist_ok=True)
for rate in [22050, 44100, 48000]:
    path = formats / f'{rate}_trailing_junk.wav'
    with wave.open(str(path), 'wb') as stream:
        stream.setnchannels(2)
        stream.setsampwidth(2)
        stream.setframerate(rate)
        stream.writeframes(b'\0\0\0\0' * rate)
    data = bytearray(path.read_bytes())
    data.extend(b'JUNK' + struct.pack('<I', 8000) + b'\xff' * 8000)
    struct.pack_into('<I', data, 4, len(data) - 8)
    path.write_bytes(data)
    tracks.append(path)
if os.environ.get('CPYMO_QA_OGG'):
    tracks.append(Path(os.environ['CPYMO_QA_OGG']))
with ThreadPoolExecutor(max_workers=2) as executor:
    results = list(executor.map(probe, tracks))
(b / 'audio-regression.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
print('PASS', len(results), 'full MP3/Ogg/WAV streams, single/loop raw samples match stable v0.24.3')
