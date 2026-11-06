"""Select measured BGM bands and extract representative private test audio.
Run on the Linux QA root. Sources are never edited; outputs stay in audio-real.
"""
from pathlib import Path
import hashlib,json,os,re,shutil,struct,subprocess
import numpy as np
b=Path.cwd();src=b/'repo/source/CPyMO-main';d=b/'audio-real';d.mkdir(exist_ok=True)
probe=d/'candidate';old=d/'baseline'
flags=['-O2','-std=gnu11','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-ffunction-sections','-fdata-sections']
for exe,source in [(probe,src/'cpymo-backends/libretro/cpymo_audio_mp3.c'),
                   (old,Path(os.environ['CPYMO_QA_BASELINE'])/'source/CPyMO-main/cpymo-backends/libretro/cpymo_audio_mp3.c')]:
    subprocess.run(['gcc',*flags,'-DAUDIO_SOURCE="'+str(source)+'"',str(b/'repo/tests/v0245/signal_probe.c'),
        str(src/'cpymo/cpymo_str.c'),str(src/'cpymo/cpymo_package.c'),str(src/'cpymo-backends/libretro/stb_vorbis.c'),
        '-Wl,--gc-sections','-lm','-o',str(exe)],check=True)
def decode(path,dest):
    p=subprocess.run([str(probe),str(path),str(dest),'0','2'],capture_output=True,text=True,check=True)
    rate=int(re.search(r'source_rate=(\d+)',p.stdout)[1])
    channels=int(re.search(r'source_channels=(\d+)',p.stdout)[1])
    x=np.fromfile(dest,'<i2').reshape(-1,2).astype(float)
    spec=np.sum(abs(np.fft.rfft(x*np.hanning(len(x))[:,None],axis=0))**2,axis=1)
    freq=np.fft.rfftfreq(len(x),1/rate);total=float(spec.sum()) or 1
    return {'rate':rate,'channels':channels,'source':str(path),'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'decode_sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),
            'peak':float(np.max(abs(x)))/32768,'rms':float(np.sqrt(np.mean(x*x)))/32768,
            'high_band_power_fraction':float(spec[freq>=8000].sum()/total),
            'bass_band_power_fraction':float(spec[(freq>=20)&(freq<=200)].sum()/total)}
def member(game,folder,name):
    p=game/folder/(folder+'.pak')
    with p.open('rb') as f:
        n=struct.unpack('<I',f.read(4))[0];assert n<20000
        entries=[struct.unpack('<32sII',f.read(40)) for _ in range(n)]
        for raw,off,length in entries:
            if raw.rstrip(b'\0').decode('ascii').lower()==name.lower():
                assert off+length<=p.stat().st_size
                f.seek(off);output=d/(game.name+'_'+name+'.mp3');output.write_bytes(f.read(length));return output
    raise ValueError(name)
selected=[];catalog=[]
for label,key in [('Narcissu','CPYMO_QA_WATER'),('Yume','CPYMO_QA_YUME')]:
    game=Path(os.environ[key]);candidates=[]
    for p in sorted((game/'bgm').glob('*.mp3')):
        raw=d/'temporary_decode.s16le';info=decode(p,raw);info['game']=label;catalog.append(info)
        candidates.append(info)
    high=max(candidates,key=lambda x:x['high_band_power_fraction'])
    bass=max((r for r in candidates if r is not high),key=lambda x:x['bass_band_power_fraction'])
    for kind,info in [('high',high),('bass',bass)]:
        name=label+'_bgm_'+kind;raw=d/(name+'.decoded.s16le');decode(Path(info['source']),raw)
        selected.append(dict(info,name=name,decoded_file=raw.name,volume=.32))
    extras=[('voice', 'n200' if label=='Narcissu' else 'aeka0001'),
            ('se','faan1_b' if label=='Narcissu' else 'se_001')]
    if label=='Narcissu':extras.append(('se','rain_12'))
    for folder,name in extras:
        p=member(game,folder,name);raw=d/(label+'_'+name+'.decoded.s16le');info=decode(p,raw)
        selected.append(dict(info,game=label,name=label+'_'+name,decoded_file=raw.name,volume=.7 if folder=='voice' else .4))
if (d/'temporary_decode.s16le').exists():(d/'temporary_decode.s16le').unlink()
(d/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
(d/'selected.json').write_text(json.dumps(selected,indent=2)+'\n')
print(json.dumps(selected,indent=2))
