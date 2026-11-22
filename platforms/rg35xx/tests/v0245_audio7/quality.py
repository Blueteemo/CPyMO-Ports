"""Production signal path versus the installed v0.24.5, neutral 50% output."""
from pathlib import Path
import os,subprocess,json,wave,re
import numpy as np
b=Path.cwd();src=b/'repo/source/CPyMO-main';d=b/'audio7-quality';d.mkdir(exist_ok=True)
old=Path(os.environ['CPYMO_QA_PREVIOUS_SOURCE'])
flags=['-O2','-std=gnu11','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-ffunction-sections','-fdata-sections']
for name,source in [('before',old),('after',src/'cpymo-backends/libretro/cpymo_audio_mp3.c')]:
 subprocess.run(['gcc',*flags,'-DAUDIO_SOURCE="'+str(source)+'"','repo/tests/v0245/signal_probe.c',str(src/'cpymo/cpymo_str.c'),str(src/'cpymo/cpymo_package.c'),str(src/'cpymo-backends/libretro/stb_vorbis.c'),'-Wl,--gc-sections','-lm','-o',str(d/name)],check=True)
rows=[]
for rate,freq,amp,mode,channels in [(44100,1000,.6,0,1),(44100,12000,.6,0,1),(22050,1000,.6,0,1),(48000,1000,.95,1,1),(48000,1000,.95,1,3)]:
 wav=d/f'{rate}_{freq}_{amp}.wav';t=np.arange(rate*2)/rate
 with wave.open(str(wav),'wb') as f:
  f.setnchannels(2);f.setsampwidth(2);f.setframerate(rate);a=np.rint(32767*amp*np.sin(2*np.pi*freq*t)).astype('<i2');f.writeframes(np.column_stack([a,a]).tobytes())
 for name in ['before','after']:
  out=d/'out.raw';p=subprocess.run([str(d/name),str(wav),str(out),'96000',str(mode),*([str(wav)]*channels)],capture_output=True,text=True,check=True)
  data=np.fromfile(out,'<i4' if mode==0 else '<i2').reshape(-1,2).astype(float);ix=np.arange(4800,90000);step=int(re.search(r'step=(\d+)',p.stdout)[1]);phase=2*np.pi*freq*ix*step/(65536*rate)
  design=np.column_stack([np.sin(phase),np.cos(phase),np.ones(len(ix))]);fit=design@np.linalg.lstsq(design,data[ix,0],rcond=None)[0];err=data[ix,0]-fit
  snr=float(20*np.log10(np.sqrt(np.mean(fit*fit))/max(1e-12,np.sqrt(np.mean(err*err)))))
  row={'case':f'{rate}/{freq}/amp{amp}/mode{mode}/{channels}ch','variant':name,'snr_db':snr,'peak':float(abs(data).max()),'cpu_seconds':float(re.search(r'CPU_SECONDS ([\d.]+)',p.stdout)[1])};rows.append(row);print(row,flush=True)
  if name=='after':
   assert snr> (65 if freq<10000 else 45),row
   if mode:assert abs(data).max()<=16384,row
 for raw in [out]:raw.unlink()
for case in sorted(set(r['case'] for r in rows)):
 a,c=[next(r for r in rows if r['case']==case and r['variant']==v) for v in ['before','after']]
 if case.endswith('/1ch'):assert c['snr_db']-a['snr_db']>15,(a,c)
(d/'summary.json').write_text(json.dumps({'scope':'analytic production PCM; no claim about speaker/DAC or hardware frame rate','results':rows},indent=2))
print('QUALITY PASS',flush=True)
