from pathlib import Path
import subprocess,json,re
b=Path.cwd();r=[]
for f in sorted((b/'audiofixtures').iterdir()):
 d={'file':f.name}
 for kind in ['baseline','candidate']:
  try:p=subprocess.run([str(b/f'audio_{kind}'),str(f)],capture_output=True,text=True,timeout=10);d[kind]={'exit':p.returncode,'log':p.stdout,'error':p.stderr}
  except subprocess.TimeoutExpired:d[kind]={'timeout':True}
 r.append(d)
(b/'evidence/audio_formats.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
