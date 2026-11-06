"""Private real-source comparison against FFmpeg 7.1.5's 64-tap reference.
Prepare decoded excerpts/references first. No source audio is modified or shipped.
The reference is evaluated at each core's measured Q16 clock and FIR delay.
"""
from pathlib import Path
import os,json,subprocess,re,math
import numpy as np
b=Path.cwd();src=b/'repo/source/CPyMO-main';d=b/'audio-real';d.mkdir(exist_ok=True)
refdir=Path(os.environ['CPYMO_QA_AUDIO_REFERENCE'])
selected=json.loads((refdir/'selected.json').read_text())
flags=['-O2','-std=gnu11','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-ffunction-sections','-fdata-sections']
for label,source in [('baseline',Path(os.environ['CPYMO_QA_BASELINE'])/'source/CPyMO-main/cpymo-backends/libretro/cpymo_audio_mp3.c'),('candidate',src/'cpymo-backends/libretro/cpymo_audio_mp3.c')]:
 subprocess.run(['gcc',*flags,'-DAUDIO_SOURCE="'+str(source)+'"',str(b/'repo/tests/v0245/signal_probe.c'),str(src/'cpymo/cpymo_str.c'),str(src/'cpymo/cpymo_package.c'),str(src/'cpymo-backends/libretro/stb_vorbis.c'),'-Wl,--gc-sections','-lm','-o',str(d/label)],check=True)
N=384000;indices=np.arange(4800,370000)
def warp(ref,positions):
 result=np.empty((len(positions),2))
 offsets=np.arange(-31,33)
 for start in range(0,len(positions),12000):
  p=positions[start:start+12000];ix=np.floor(p).astype(int)[:,None]+offsets
  delta=p[:,None]-ix
  weights=np.sinc(delta)*np.sinc(delta/32);weights/=weights.sum(axis=1)[:,None]
  result[start:start+len(p)]=np.sum(ref[np.clip(ix,0,len(ref)-1)]*weights[:,:,None],axis=1)
 return result
rms=lambda x:float(np.sqrt(np.mean(x*x)))
def summarize(x,reference):
 err=x-reference;noise=rms(err);signal=rms(reference)
 spec=np.sum(abs(np.fft.rfft(err*np.hanning(len(err))[:,None],axis=0))**2,axis=1)
 freqs=np.fft.rfftfreq(len(err),1/48000)
 return {'peak':float(np.max(abs(x))),'rms':rms(x),'reference_rms':signal,'error_rms':noise,'reference_snr_db':20*math.log10(max(signal,1e-12)/max(noise,1e-12)),
         'error_spectral_power_above8k_fraction':float(spec[freqs>=8000].sum()/max(spec.sum(),1e-30)),
         'samples_at_fullscale':int(np.sum(abs(x)>=1))}
def limited(x):
 a=np.abs(x);mask=a>.72
 a[mask]=.72+.28*(1-np.exp(-(a[mask]-.72)/.28))
 return np.trunc(np.trunc(np.copysign(a,x)*32767)*.5)/32768
rows=[];reference_cache={}
for label in ['baseline','candidate']:
 centre=7+.5/512 if label=='baseline' else 15
 for r in selected:
  cmd=[str(d/label),r['source'],str(d/'scratch.i32'),str(N),'0'];env={**os.environ,'PROBE_VOLUME':str(r['volume'])}
  p=subprocess.run(cmd,env=env,text=True,capture_output=True,check=True)
  step=int(re.search(r'step=(\d+)',p.stdout)[1]);x=np.fromfile(d/'scratch.i32','<i4').reshape(-1,2)[indices]/32768
  ref=np.fromfile(refdir/(r['name']+'.reference.s16le'),'<i2').reshape(-1,2).astype(float)/32768
  positions=(indices*step/65536+centre)*48000/r['rate']
  ideal=warp(ref,positions)*(int(r['volume']*4096)/4096);reference_cache[(label,r['name'])]=ideal
  row={'core':label,'case':r['name'],'rate':r['rate'],'source_channels':r['channels'],'volume':r['volume'],'step_q16':step,'cpu':p.stdout.strip(),'premix':summarize(x,ideal)}
  p=subprocess.run([str(d/label),r['source'],str(d/'scratch.s16'),str(N),'1'],env=env,text=True,capture_output=True,check=True)
  post=np.fromfile(d/'scratch.s16','<i2').reshape(-1,2)[indices]/32768
  row['postmix']=summarize(post,limited(ideal));row['limiter_fraction']=float(np.mean(abs(x)>.72));row['expected_postmix_peak_bound']=.5
  assert row['postmix']['peak']<=.5
  rows.append(row);print(label,r['name'],round(row['premix']['reference_snr_db'],2),round(row['postmix']['reference_snr_db'],2),flush=True)
 for game in ['Narcissu','Yume']:
  group=[next(r for r in selected if r['name']==game+'_bgm_high'),next(r for r in selected if r['name']==game+('_n200' if game=='Narcissu' else '_aeka0001')),next(r for r in selected if r['name']==game+('_faan1_b' if game=='Narcissu' else '_se_001'))]
  for stress in [False,True]:
   volumes=[1,1,1] if stress else [r['volume'] for r in group]
   ideal=sum(reference_cache[(label,r['name'])]*(v/(int(r['volume']*4096)/4096)) for r,v in zip(group,volumes)) if stress else sum(reference_cache[(label,r['name'])] for r in group)
   env={**os.environ,'PROBE_VOLUMES':','.join(map(str,volumes))}
   args=[str(d/label),group[0]['source'],str(d/'scratch.s16'),str(N),'1',*[r['source'] for r in group]]
   p=subprocess.run(args,env=env,text=True,capture_output=True,check=True)
   x=np.fromfile(d/'scratch.s16','<i2').reshape(-1,2)[indices]/32768
   rows.append({'core':label,'case':game+'_BGM_VOICE_SE'+('_stress' if stress else ''),'volumes':volumes,'postmix':summarize(x,limited(ideal)),'reference_limiter_fraction':float(np.mean(abs(ideal)>.72)),'cpu':p.stdout.strip()})
   assert np.max(abs(x))<=.5
for p in [d/'scratch.i32',d/'scratch.s16']:
 if p.exists():p.unlink()
(d/'comparison.json').write_text(json.dumps({'reference':'FFmpeg 7.1.5 aresample filter_size64 phase_shift10 cutoff.97; production-decoder input, Q16 clock and FIR delay aligned with 64-point windowed sinc; first .1s and final .29s excluded','notes':'Digital error is not an audibility or DAC test. Band-edge reference filter differences are included. Premix uses Q12 volumes; postmix includes unchanged limiter then50%gain. Stereo channels remain separate.', 'results':rows},indent=2)+'\n')
print('PASS actual-source references and mix bounds',len(rows))
