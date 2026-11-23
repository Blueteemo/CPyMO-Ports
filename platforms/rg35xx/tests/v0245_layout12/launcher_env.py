"""New audio report export; existing volume/session behavior stays protected."""
from pathlib import Path
import os,subprocess,json,tempfile,hashlib
b=Path.cwd();repo=b/'repo';rows=[]
root=Path(tempfile.mkdtemp(prefix='audio7-launcher-',dir=b))
for shell in [['/bin/sh'],['/usr/bin/busybox','ash']]:
 for value,want in [('50','50'),('0','0'),('100','100'),('bad','50')]:
  d=root/(('dash' if len(shell)==1 else 'ash')+'_'+value);app=d/'APPS with spaces/CPyMO';game=app/'games/Demo';game.mkdir(parents=True)
  (game/'gameconfig.txt').write_text('gametitle,Demo\n');(app/'cpymo_libretro.so').write_bytes(b'not executed');(app/'output-volume.txt').write_text(value+'\n')
  launcher=app.parent/'CPyMO.sh';launcher.write_bytes((repo/'deploy/Roms/APPS/CPyMO.sh').read_bytes());ra=d/'ra';cfg=ra/'.retroarch/retroarch.cfg';cfg.parent.mkdir(parents=True);cfg.write_text('audio_volume = "1.5"\nconfig_save_on_exit = "true"\n');volume=cfg.with_name('volume.cfg');volume.write_text('audio_volume = "-4.000000"\n')
  fake=d/'fake-ra';record=d/'record.json';fake.write_text('#!/usr/bin/python3\nimport os,json\nfrom pathlib import Path\nPath(os.environ["RA_RECORD"]).write_text(json.dumps({k:os.environ.get(k) for k in ["CPYMO_AUDIO_REPORT","CPYMO_OUTPUT_PERCENT","CPYMO_AUDIO_CAPTURE","CPYMO_SAVE_ROOT"]}))\n');fake.chmod(0o755)
  env={k:v for k,v in os.environ.items() if not k.startswith('CPYMO_')};env.update(CPYMO_LOADER='none',CPYMO_RETROARCH_HOME=str(ra),CPYMO_RETROARCH_BIN=str(fake),RA_RECORD=str(record))
  save_root = d/'custom saves' if value == '100' else app.parent/'saves'
  if value == '100':
   save_root.mkdir()
   env['CPYMO_SAVE_ROOT'] = str(save_root)
  before=[p.read_bytes() for p in [cfg,volume]];p=subprocess.run([*shell,str(launcher)],env=env,capture_output=True,timeout=10);assert p.returncode==0,(p.returncode,p.stderr)
  got=json.loads(record.read_text());assert got['CPYMO_OUTPUT_PERCENT']==want and got['CPYMO_AUDIO_REPORT']==str(app/'logs/audio-status.txt'),got
  assert got['CPYMO_AUDIO_CAPTURE']=='' and got['CPYMO_SAVE_ROOT']==str(save_root),got
  log=(app/'logs/launcher.log').read_text();assert 'launcher_revision=1.0.0' in log and 'ra_audio_volume_db=-4.000000' in log
  assert [p.read_bytes() for p in [cfg,volume]]==before
  rows.append({'shell':shell,'value':value,'expected_output_percent':want,'passed':True})
(b/'reading9-launcher.json').write_text(json.dumps(rows,indent=2));print('LAUNCHER PASS',len(rows),flush=True)
