"""Byte-for-byte rollback check against the actual v0.24.4 DSP source."""
from pathlib import Path
import subprocess,os,json,hashlib
b=Path.cwd();src=b/'repo/source/CPyMO-main';out=b/'audio-rollback';out.mkdir(exist_ok=True)
flags=['-O2','-std=gnu11','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-ffunction-sections','-fdata-sections']
for label,source in [('v244',Path(os.environ['CPYMO_QA_BASELINE'])/'source/CPyMO-main/cpymo-backends/libretro/cpymo_audio_mp3.c'),('rollback',src/'cpymo-backends/libretro/cpymo_audio_mp3.c')]:
 subprocess.run(['gcc',*flags,'-DAUDIO_SOURCE="'+str(source)+'"','repo/tests/v0245/signal_probe.c',str(src/'cpymo/cpymo_str.c'),str(src/'cpymo/cpymo_package.c'),str(src/'cpymo-backends/libretro/stb_vorbis.c'),'-Wl,--gc-sections','-lm','-o',str(out/label)],check=True)
selected=json.loads(Path(os.environ['CPYMO_QA_AUDIO_INDEX']).read_text());rows=[]
for row in selected:
 for loop in [False,True]:
  hashes=[]
  for label in ['v244','rollback']:
   env={k:v for k,v in os.environ.items() if not k.startswith('PROBE_')};env['PROBE_VOLUME']=str(row['volume'])
   if loop:env['PROBE_LOOP']='1'
   target=out/'pcm.raw';p=subprocess.run([str(out/label),row['source'],str(target),'384000','1'],env=env,capture_output=True,text=True,check=True)
   hashes.append(hashlib.sha256(target.read_bytes()).hexdigest())
  assert hashes[0]==hashes[1],(row['name'],loop,hashes)
  rows.append({'source':row['name'],'loop':loop,'output_sha256':hashes[0],'v244_pcm_identical':True})
for game in ['Narcissu','Yume']:
 group=[next(r for r in selected if r['name']==game+'_bgm_high'),next(r for r in selected if r['name']==game+('_n200' if game=='Narcissu' else '_aeka0001')),next(r for r in selected if r['name']==game+('_faan1_b' if game=='Narcissu' else '_se_001'))]
 hashes=[]
 for label in ['v244','rollback']:
  target=out/'pcm.raw';subprocess.run([str(out/label),group[0]['source'],str(target),'384000','1',*[r['source'] for r in group]],env={**os.environ,'PROBE_VOLUMES':'.32,.7,.4'},capture_output=True,check=True)
  hashes.append(hashlib.sha256(target.read_bytes()).hexdigest())
 assert hashes[0]==hashes[1],(game,hashes)
 rows.append({'source':game+'_bgm_voice_se','output_sha256':hashes[0],'v244_pcm_identical':True})
(out/'pcm.raw').unlink()
(out/'summary.json').write_text(json.dumps({'scope':'Actual game source excerpts, original v244 DSP versus rollback, same volume and50%gain; no DAC or audibility claim','results':rows},indent=2)+'\n')
print('PASS',len(rows),'real-source single/loop/mix cases are byte-identical to v0.24.4')
