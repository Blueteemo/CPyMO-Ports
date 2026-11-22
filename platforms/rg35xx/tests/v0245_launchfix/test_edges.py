"""Additional fault/concurrent launcher paths. No device/TF access."""
from pathlib import Path
import json,os,subprocess,shutil,hashlib,concurrent.futures
QA=Path.cwd();ROOT=QA/'repo';out=QA/'edge-runs';out.mkdir(exist_ok=True)
smoke=QA/'smoke';smoke.mkdir(exist_ok=True)
(smoke/'global.cfg').write_text('audio_volume = "1.5"\nconfig_save_on_exit = "true"\nauto_overrides_enable = "true"\n')
(smoke/'session.cfg').write_text('audio_volume = "-2"\nconfig_save_on_exit = "false"\nauto_overrides_enable = "false"\n')
rows=[]
def fixture(name):
 t=out/name
 if t.exists():shutil.rmtree(t)
 app=t/'APPS/CPyMO';game=app/'games/Demo';game.mkdir(parents=True)
 (game/'gameconfig.txt').write_text('gametitle,Demo\n');(app/'cpymo_libretro.so').write_bytes(b'not executed')
 (app.parent/'CPyMO.sh').write_bytes((ROOT/'deploy/Roms/APPS/CPyMO.sh').read_bytes())
 shutil.copyfile(ROOT/'deploy/Roms/APPS/CPyMO/retroarch-cpymo.cfg',app/'retroarch-cpymo.cfg')
 ra=t/'ra';(ra/'.retroarch').mkdir(parents=True);(ra/'.retroarch/retroarch.cfg').write_text('audio_volume = "1.500000"\n')
 env=os.environ.copy();env.update(CPYMO_LOADER='none',CPYMO_RETROARCH_HOME=str(ra),CPYMO_RETROARCH_BIN=str(ROOT/'tests/v0243a/fake_retroarch.py'),RA_PROBE=str(QA/'ra_config_probe'),RA_RECORD=str(t/'record.json'))
 return t,app,ra,env,['/bin/sh',str(app.parent/'CPyMO.sh')]
for name in ['directory_input','fifo_input','mktemp_failure','append_separator']:
 t,app,ra,env,cmd=fixture(name);v=ra/'.retroarch/volume.cfg'
 if name=='directory_input':v.mkdir()
 elif name=='fifo_input':os.mkfifo(v)
 else:v.write_text('audio_volume = "-2"\n')
 if name=='mktemp_failure':
  bin=t/'bin';bin.mkdir();p=bin/'mktemp';p.write_text('#!/bin/sh\nexit 1\n');p.chmod(0o755);env['PATH']=str(bin)+':'+env['PATH']
 if name=='append_separator':
  # A literal | cannot appear in RA append paths; reject instead of misparse.
  moved=app.parent.with_name('APPS|delimiter');app.parent.rename(moved);app=moved/'CPyMO';cmd=['/bin/sh',str(moved/'CPyMO.sh')]
 before=(ra/'.retroarch/retroarch.cfg').read_bytes()
 p=subprocess.run(cmd,env=env,timeout=8)
 if name in ['directory_input','fifo_input']:
  assert p.returncode==0;record=json.loads((t/'record.json').read_text());assert record['effective']['audio_volume']==0;assert not Path(record['session_path']).exists()
 elif name=='mktemp_failure':
  assert p.returncode==0;record=json.loads((t/'record.json').read_text());assert record['effective']['audio_volume']==-2;assert not Path(record['session_path']).exists()
 else:assert p.returncode==47 and not(t/'record.json').exists(),(name,p.returncode)
 assert(ra/'.retroarch/retroarch.cfg').read_bytes()==before
 rows.append({'case':name,'exit':p.returncode,'passed':True})
# Two launchers in one APP tree have separate temporary configs; both survive
# until their own child exits, then disappear independently.
t,app,ra,env,cmd=fixture('concurrent');(ra/'.retroarch/volume.cfg').write_text('audio_volume = "-2"\n')
def run(i):
 e=env.copy();e['RA_RECORD']=str(t/f'record{i}.json');e['CPYMO_LOG']=str(app/f'logs/launcher{i}.log')
 p=subprocess.run(cmd,env=e,timeout=8);assert p.returncode==0;return json.loads(Path(e['RA_RECORD']).read_text())
with concurrent.futures.ThreadPoolExecutor(max_workers=2)as ex:a,b=list(ex.map(run,[1,2]))
assert a['session_path']!=b['session_path'] and all(not Path(x['session_path']).exists()for x in [a,b])
rows.append({'case':'concurrent_launches','passed':True})
# Explicitly exercise RA's actual precedence both forward and reverse.
p=QA/'ra_config_probe';g=QA/'smoke/global.cfg';s=QA/'smoke/session.cfg'
assert json.loads(subprocess.check_output([str(p),str(g),str(s)]))['audio_volume']==-2
assert json.loads(subprocess.check_output([str(p),str(s),str(g)]))['audio_volume']==1.5
same=QA/'smoke/duplicate.cfg';same.write_text('audio_volume = "-2"\naudio_volume = "9"\n')
assert json.loads(subprocess.check_output([str(p),str(same)]))['audio_volume']==-2
rows.append({'case':'actual_parser_append_order_and_duplicate_first_wins','passed':True})
(QA/'evidence/launcher_edges.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2));print('PASS',len(rows),'edge/precedence scenarios')
