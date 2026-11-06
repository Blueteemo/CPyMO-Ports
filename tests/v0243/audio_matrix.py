from pathlib import Path
import subprocess,json,concurrent.futures,hashlib
b=Path.cwd();tracks=sorted((b/'gamebase/Roms/APPS/CPyMO/games/水仙1/bgm').glob('*.mp3'))
def run(p):
 d={'track':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
 for n in ['baseline','candidate']:
  x=subprocess.run([str(b/f'audio_{n}'),str(p)],capture_output=True,text=True,check=True);d[n]=x.stdout
 d['ffprobe']=subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration','-of','json',str(p)],text=True)
 return d
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:r=list(ex.map(run,tracks))
(b/'evidence/audio_full_tracks.json').write_text(json.dumps(r,ensure_ascii=False,indent=2));print('tested',len(r),'tracks',flush=True)
