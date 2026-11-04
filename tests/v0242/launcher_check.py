from pathlib import Path
import tempfile,subprocess,os,hashlib,json
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="cpymo-launch-") as t:
 t=Path(t);apps=t/'APPS';app=apps/'CPyMO';game=app/'games'/'Demo';game.mkdir(parents=True)
 (game/'gameconfig.txt').write_text('gametitle,Demo\nimagesize,540,360\n')
 (app/'cpymo_libretro.so').write_bytes(b'test core')
 for f in ['CPyMO.sh']: (apps/f).write_bytes((root/'deploy/Roms/APPS'/f).read_bytes())
 (app/'retroarch-cpymo.cfg').write_bytes((root/'deploy/Roms/APPS/CPyMO/retroarch-cpymo.cfg').read_bytes())
 ra=t/'ra';ra.mkdir();globalcfg=ra/'retroarch.cfg';globalcfg.write_text('audio_volume = "-3"\n')
 before=hashlib.sha256(globalcfg.read_bytes()).hexdigest()
 recorder=t/'record.py';record=t/'record.json'
 recorder.write_text('#!/usr/bin/python3\nimport os,sys,json\nopen(os.environ["RECORD"],"w").write(json.dumps(dict(argv=sys.argv[1:],gain=os.environ.get("CPYMO_OUTPUT_PERCENT"))))\n')
 recorder.chmod(0o755)
 env=os.environ.copy();env.update(CPYMO_LOADER='none',CPYMO_RETROARCH_HOME=str(ra),CPYMO_RETROARCH_BIN=str(recorder),RECORD=str(record))
 for gain,want in [('50','50'),('25','25'),('100','100'),('150','50'),('bad;touch file','50')]:
  (app/'output-volume.txt').write_text(gain+'\n')
  subprocess.run(['sh',str(apps/'CPyMO.sh')],env=env,check=True)
  got=json.loads(record.read_text());assert got['gain']==want,got
  assert '--appendconfig' in got['argv'] and '-v' not in got['argv']
  assert got['argv'][-1]==str(app/'games')
  assert hashlib.sha256(globalcfg.read_bytes()).hexdigest()==before
  assert not (apps/'CPyMO.log').exists()
  assert (app/'logs/launcher.log').stat().st_size<8192
 sentinel=apps/'OtherApp.dat';sentinel.write_bytes(b'untouched')
 subprocess.run(['sh',str(apps/'CPyMO.sh')],env=env,check=True)
 assert sentinel.read_bytes()==b'untouched'
 print('PASS scoped launcher config, volume persistence/validation, log placement, global config and other APPS preservation')
