from pathlib import Path
import shutil,subprocess,struct,concurrent.futures,json
from run243 import run,b
base=b/'fixtures/540x360'
# Loop returns to the exact script position: the shared read hash must change.
p=b/'fixtures/readloop';shutil.copytree(base,p,dirs_exist_ok=True)
(p/'script/start.txt').write_text('#bg test\n#label again\n#say ,已读状态：中文对白保持清晰，重复阅读应有适度差异。\n#goto again\n')
# Explicit red text catches unwanted forced-white/alpha changes.
p=b/'fixtures/color';shutil.copytree(base,p,dirs_exist_ok=True);s=(p/'gameconfig.txt').read_text().replace('#ffffff','#ff8040');(p/'gameconfig.txt').write_text(s)
for kind in ['mp3','ogg']:
 p=b/'fixtures'/kind;shutil.copytree(base,p,dirs_exist_ok=True)
 subprocess.run(['ffmpeg','-v','error','-y','-i',str(base/'bgm/song.wav'),str(p/f'bgm/song.{kind}')],check=True)
 (p/'gameconfig.txt').write_text((p/'gameconfig.txt').read_text().replace('bgmformat,wav','bgmformat,'+kind))
p=b/'fixtures/android';shutil.copytree(base,p,dirs_exist_ok=True);(p/'gameconfig.txt').write_text((p/'gameconfig.txt').read_text().replace('platform,s60v5','platform,android'))
for nm,sk in [('read_all',0),('read_only',1)]:
 p=b/'fixtures'/nm;shutil.copytree(b/'fixtures/readloop',p,dirs_exist_ok=True);(p/'save').mkdir(exist_ok=True);(p/'save/config.csav').write_bytes(struct.pack('<6H',32767,32767,32767,26,5,sk))
font=[(120,4),(160,32),(190,32),(220,32),(250,32),(280,128),(320,1)]
skip=[(120,4),(160,32),(190,32),(220,32),(250,32),(280,32),(320,128),(360,1)]
held=lambda a,z:[(i,2048) for i in range(a,z)]
cases=[('small540','540x360',[],300),('font_toggle','540x360',font,600),('font_repeat','540x360',font[:-1]+[(310,64),(340,128),(370,64),(400,128),(430,1),(470,256),(520,256)],600),('skip_setting','readloop',skip+held(400,450),600),('only_unread','read_only',held(150,210),300),('only_read','read_only',[(120,256)]+held(180,230),300),('all_unread','read_all',held(150,210),300),('read_alpha','readloop',[(170,256)],300),('color','color',[],300),('small320','320x240',[],300),('small800','800x600',[],300),('mp3','mp3',[],300),('ogg','ogg',[],300),('android','android',[],300)]
for nm in ['actions','menu','chords','choice','loadmissing','saveonly']:
 t=b/'repo/tests/v0243/timelines'/f'{nm}.txt'
 if t.exists():cases.append((nm,'540x360',[tuple(map(int,l.split())) for l in t.read_text().splitlines() if l.strip()],600))
def do(c):nm,fixture,events,frames=c;return {'case':nm,'exit':run(nm,fixture,events,frames)}
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as ex:r=list(ex.map(do,cases))
(b/'evidence/regression.json').write_text(json.dumps(r,indent=2));assert all(x['exit']==0 for x in r)
# Same files, normal startup and reopening settings must preserve font/policy.
for name in ['font_toggle','skip_setting']:
 run(name+'_restart',fixture=str(b/'cases'/name/'game'),events=[(120,4),(220,1)],frames=300)
 shutil.copyfile(b/'cases'/(name+'_restart')/'run.log',b/'evidence'/f'{name}_restart.log')
