import os, pathlib, subprocess, sys, tempfile, wave, struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rg35xxwav-') as d:
    root=Path(d);src=root/'input';dst=root/'output';src.mkdir();(src/'bgm').mkdir();(src/'se').mkdir();(src/'voice').mkdir()
    (src/'gameconfig.txt').write_text('gametitle,Audio test\nbgmformat,wav\nseformat,wav\nvoiceformat,wav\n',encoding='utf8')
    for name in ('bgm/b1.wav','se/s1.wav','voice/v1.wav'):
        with wave.open(str(src/name),'wb') as w:
            w.setnchannels(1);w.setframerate(8000);w.setsampwidth(2)
            w.writeframes(b''.join(struct.pack('<h',i%1100-550) for i in range(3200)))
    cmd=[sys.executable,str(ROOT/'tools/convert_game_audio_wav.py'),str(src),str(dst)]
    r=subprocess.run(cmd,capture_output=True,text=True)
    assert r.returncode==0,r.stdout+r.stderr
    assert (src/'bgm/b1.wav').is_file() and (src/'gameconfig.txt').read_text().count('format,wav')==3
    assert 'bgmformat,wav' in (dst/'gameconfig.txt').read_text()
    for name in ('bgm/b1.wav','se/s1.wav','voice/v1.wav'):
        with wave.open(str(dst/name),'rb') as w:
            assert (w.getnchannels(),w.getframerate(),w.getsampwidth())==(2,22050,2)
    r=subprocess.run(cmd,capture_output=True,text=True)
    assert r.returncode!=0 and 'Output already exists' in r.stderr
    assert not (root/'output.partial').exists()
    print('PASS audio conversion: 3 assets, format/header checks, input unchanged, no overwrite')
