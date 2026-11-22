"""Production WAV loop clock, transfer curve, EOF safety and bounded long run."""
from pathlib import Path
import os,subprocess,json,wave,re,resource
import numpy as np
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
b=Path.cwd();src=b/'repo/source/CPyMO-main';d=b/'audio-loop-final';d.mkdir(exist_ok=True)
probe=b/'audio-real';rows=[]
rate=44100;freq=1000;n=rate*2
with wave.open(str(d/'loop.wav'),'wb') as f:
 f.setnchannels(1);f.setsampwidth(2);f.setframerate(rate)
 f.writeframes(np.rint(12000*np.sin(2*np.pi*freq*np.arange(n)/rate)).astype('<i2').tobytes())
for label in ['baseline','candidate']:
 out=d/'loop.raw';p=subprocess.run([str(probe/label),str(d/'loop.wav'),str(out),'480000','0'],env={**os.environ,'PROBE_LOOP':'1'},capture_output=True,text=True,check=True)
 x=np.fromfile(out,'<i4').reshape(-1,2)[:,0].astype(float);ix=np.arange(4800,len(x));step=int(re.search(r'step=(\d+)',p.stdout)[1])
 phase=2*np.pi*freq*ix*step/(65536*rate);design=np.column_stack([np.sin(phase),np.cos(phase),np.ones(len(ix))]);fit=design@np.linalg.lstsq(design,x[ix],rcond=None)[0];err=x[ix]-fit
 rows.append({'case':'loop_10_seconds','core':label,'rms_error_lsb':float(np.sqrt(np.mean(err*err))),'snr_db':float(20*np.log10(np.sqrt(np.mean(fit*fit))/np.sqrt(np.mean(err*err)))),'max_error_lsb':float(abs(err).max()),'output':p.stdout.strip()})
 assert label!='candidate' or rows[-1]['snr_db']>75
 out.unlink()
# Constant levels verify the actual knee and 50% final gain, including 3ch overload.
for level in [0,.5,.72,.8,.99]:
 with wave.open(str(d/'dc.wav'),'wb') as f:
  f.setnchannels(2);f.setsampwidth(2);f.setframerate(48000);f.writeframes(np.full((48000,2),round(level*32767),'<i2').tobytes())
 for channels in [1,3]:
  out=d/'dc.raw';subprocess.run([str(probe/'candidate'),str(d/'dc.wav'),str(out),'48000','1',*([str(d/'dc.wav')]*channels)],check=True,capture_output=True)
  x=np.fromfile(out,'<i2').reshape(-1,2)[4800:40000]/32768
  rows.append({'case':'limiter_dc','level':level,'channels':channels,'output_min':float(x.min()),'output_max':float(x.max())});assert abs(x).max()<=.5
  out.unlink()
# Demonstrate the old high-rate EOF invalid phase with ASan, not just an exit code.
for label,source in [('baseline',Path(os.environ['CPYMO_QA_BASELINE'])/'source/CPyMO-main/cpymo-backends/libretro/cpymo_audio_mp3.c'),('candidate',src/'cpymo-backends/libretro/cpymo_audio_mp3.c')]:
 exe=d/(label+'-asan')
 cmd=['gcc','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-ffunction-sections','-fdata-sections','-DAUDIO_SOURCE="'+str(source)+'"',str(b/'repo/tests/v0245/signal_probe.c'),str(src/'cpymo/cpymo_str.c'),str(src/'cpymo/cpymo_package.c'),str(src/'cpymo-backends/libretro/stb_vorbis.c'),'-Wl,--gc-sections','-lm','-o',str(exe)]
 subprocess.run(cmd,check=True)
 wav=b/'audio-quality-fixed32/96000_10000_2.wav'
 p=subprocess.run([str(exe),str(wav),'/dev/null','192000','0'],env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1'},text=True,capture_output=True)
 (d/(label+'-96k-asan.log')).write_text(p.stdout+p.stderr)
 rows.append({'case':'96k_eof_asan','core':label,'exit':p.returncode,'address_sanitizer_overflow':'heap-buffer-overflow' in p.stderr})
 assert label!='candidate' or p.returncode==0
# One hour of generated stereo audio, discarded immediately. Peak RSS recorded.
cmd=['/usr/bin/time','-v','-o',str(d/'hour_metrics.txt'),str(probe/'candidate'),str(d/'loop.wav'),'/dev/null',str(48000*3600),'1']
p=subprocess.run(cmd,env={**os.environ,'PROBE_LOOP':'1'},text=True,capture_output=True,check=True)
rows.append({'case':'one_hour_loop','seconds_generated':3600,'output':p.stdout.strip(),'process_metrics':(d/'hour_metrics.txt').read_text()})
(d/'summary.json').write_text(json.dumps({'scope':'Digital production DSP; long run is unpaced host generation, not ALSA wall-clock or device test','results':rows},indent=2)+'\n')
print(json.dumps(rows,indent=2))
