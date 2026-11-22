from pathlib import Path
import os,subprocess,wave,json
import numpy as np
b=Path.cwd();src=b/'repo/source/CPyMO-main';d=b/'audio7-formats';d.mkdir(exist_ok=True)
flags=['-O1','-g','-std=gnu11','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all']
subprocess.run(['gcc',*flags,'repo/tests/v0245/signal_probe.c',str(src/'cpymo/cpymo_str.c'),str(src/'cpymo/cpymo_package.c'),str(src/'cpymo-backends/libretro/stb_vorbis.c'),'-Wl,--gc-sections','-lm','-o',str(d/'probe')],check=True)
rows=[]
for rate in [8000,11025,16000,22050,24000,32000,44100,48000,88200,96000,192000]:
 for channels in [1,2]:
  p=d/f'{rate}_{channels}.wav';x=np.rint(28000*np.sin(2*np.pi*1000*np.arange(rate//20)/rate)).astype('<i2')
  with wave.open(str(p),'wb') as f:
   f.setframerate(rate);f.setnchannels(channels);f.setsampwidth(2);f.writeframes(np.tile(x[:,None],(1,channels)).tobytes())
  result=subprocess.run([str(d/'probe'),str(p),'/dev/null','48000','1'],capture_output=True)
  assert result.returncode==(3 if rate==192000 else 0),(rate,channels,result.returncode,result.stderr)
  rows.append({'rate':rate,'channels':channels,'short_eof':rate!=192000,'unsupported_clean_rejection':rate==192000,'passed':True})
wav=b/'audio7-quality/48000_1000_0.95.wav'
for percent in [0,1,25,50,100]:
 out=d/'gain.raw';subprocess.run([str(d/'probe'),str(wav),str(out),'48000','1'],env={**os.environ,'PROBE_OUTPUT_PERCENT':str(percent)},check=True,capture_output=True);x=np.fromfile(out,'<i2').reshape(-1,2)[4800:40000]
 assert abs(x.astype(np.int32)).max()<=32767*percent/100+1
 if percent==0:assert not np.any(x)
 else:assert abs(np.sqrt(np.mean(x.astype(float)**2))-(32767*.95/np.sqrt(2)*percent/100))<2
 rows.append({'output_percent':percent,'peak':int(abs(x.astype(np.int32)).max()),'passed':True})
subprocess.run([str(d/'probe'),str(wav),'/dev/null',str(48000*600),'1'],env={**os.environ,'PROBE_LOOP':'1'},capture_output=True,check=True)
rows.append({'generated_seconds':600,'loop':True,'passed':True})
(d/'summary.json').write_text(json.dumps({'scope':'ASAN/UBSAN, short mono/stereo EOF, output controls and accelerated generated loop','results':rows},indent=2));print('FORMATS/GAIN PASS',len(rows),flush=True)
