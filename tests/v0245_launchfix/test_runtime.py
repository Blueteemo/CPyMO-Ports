"""Fault-injection integration against the real launcher and RA1.14 parser.
Only private QA files and volatile Linux runtime directories are touched.
"""
from pathlib import Path
import os,subprocess,json,hashlib,shutil,concurrent.futures,time
QA=Path.cwd();ROOT=QA/'repo';OUT=QA/('runtime-runs-'+str(time.time_ns()));OUT.mkdir()
BUSY=QA/'busybox-bin';BUSY.mkdir(exist_ok=True)
subprocess.run(['/usr/bin/busybox','--install','-s',str(BUSY)],check=True)
results=[]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def fixture(name,shell,baseline=False):
 t=OUT/(shell+'_'+name);t.mkdir()
 app=t/'APPS with spaces/CPyMO';game=app/'games/Demo';game.mkdir(parents=True)
 (game/'gameconfig.txt').write_text('gametitle,Demo\n')
 (app/'cpymo_libretro.so').write_bytes(b'not executed')
 (app/'output-volume.txt').write_text('50\n')
 (app/'retroarch-cpymo.cfg').write_text('audio_volume = "9"\nconfig_save_on_exit = "true"\nauto_overrides_enable = "true"\n')
 launcher=app.parent/'CPyMO.sh'
 original=(QA/'evidence/CPyMO_device.sh' if baseline else ROOT/'deploy/Roms/APPS/CPyMO.sh').read_text()
 # ash can prefer its built-in applets over PATH. Functions inject the same
 # failing operations in both shells without changing the launcher body.
 injection='mktemp() { "$FAULT_ROOT/fault-bin/mktemp" "$@"; }\nmkdir() { "$FAULT_ROOT/fault-bin/mkdir" "$@"; }\n'
 launcher.write_text(original.replace('#!/bin/sh\n','#!/bin/sh\n'+injection,1))
 ra=t/'RetroArch';(ra/'.retroarch').mkdir(parents=True)
 (ra/'.retroarch/retroarch.cfg').write_text('audio_volume = "1.5"\nconfig_save_on_exit = "true"\n')
 (ra/'.retroarch/volume.cfg').write_text('audio_volume = "-2.000000"\n')
 save=app.parent/'saves/Demo/save-01.csav';save.parent.mkdir(parents=True);save.write_bytes(b'user save remains intact')
 protected=[*list((ra/'.retroarch').iterdir()),app/'retroarch-cpymo.cfg',app/'output-volume.txt',app/'cpymo_libretro.so',save]
 before={str(p):(digest(p),p.stat().st_mtime_ns) for p in protected}
 bin=t/'fault-bin';bin.mkdir()
 (bin/'mktemp').write_text('#!/bin/sh\nprintf "invoked\\n" >> "$FAULT_ROOT/mktemp_called"\necho "mktemp: Input/output error" >&2\nexit 1\n');(bin/'mktemp').chmod(0o755)
 wrapper=bin/'mkdir'
 wrapper.write_text('''#!/usr/bin/python3
import os,sys,subprocess
from pathlib import Path
path=Path(sys.argv[-1]);root=Path(os.environ['FAULT_ROOT']);policy=os.environ.get('FAULT_POLICY','none')
if path.name.startswith('cpymo-ra.'):
 with (root/'mkdir_calls').open('a') as f:f.write(str(path)+'\\n')
 first=str(path.parent)==os.environ.get('CPYMO_RUNTIME_ROOT')
 if policy=='all_fail' or policy=='first_fail' and first:
  print('mkdir: Input/output error: '+str(path),file=sys.stderr);sys.exit(1)
 if first and policy in ['collision','exhaust'] and (policy=='exhaust' or not (root/'collision_once').exists()):
  target=root/'foreign';target.mkdir(exist_ok=True);(target/'keep').write_text('never delete this')
  path.symlink_to(target,target_is_directory=True)
  (root/'collision_once').write_text('1')
  with (root/'foreign_links').open('a') as f:f.write(str(path)+'\\n')
  print('mkdir: File exists',file=sys.stderr);sys.exit(1)
 if first and policy=='write_fail':
  p=subprocess.run(['/usr/bin/mkdir',*sys.argv[1:]])
  if p.returncode==0:path.chmod(0)
  sys.exit(p.returncode)
os.execv('/usr/bin/mkdir',['mkdir',*sys.argv[1:]])
''');wrapper.chmod(0o755)
 env={k:v for k,v in os.environ.items() if not k.startswith('CPYMO_')}
 env.update(PATH=str(bin)+':'+(str(BUSY)+':' if shell=='busybox' else '')+'/usr/bin:/bin',FAULT_ROOT=str(t),CPYMO_LOADER='none',CPYMO_RETROARCH_HOME=str(ra),CPYMO_RETROARCH_BIN=str(ROOT/'tests/v0243a/fake_retroarch.py'),RA_PROBE=str(QA/'ra_config_probe'),RA_RECORD=str(t/'record.json'))
 command=(['/usr/bin/busybox','sh'] if shell=='busybox' else ['/bin/sh'])+[str(launcher),str(game)]
 return t,app,env,command,before

def verify(t,app,env,command,before,want=0):
 p=subprocess.run(command,env=env,timeout=12,capture_output=True,text=True)
 assert p.returncode==want,(t.name,p.returncode,(app/'logs/launcher.log').read_text())
 for path,stamp in before.items():assert (digest(Path(path)),Path(path).stat().st_mtime_ns)==stamp,path
 log=(app/'logs/launcher.log').read_text()
 if want==0:
  r=json.loads((t/'record.json').read_text())
  assert r['effective']=={'audio_volume':-2,'config_save_on_exit':False,'auto_overrides_enable':False},r
  assert r['gain']=='50' and r['file_mode']=='0o600' and r['dir_mode']=='0o700'
  assert not Path(r['session_path']).parent.exists()
  assert 'runtime_allocator=atomic_mkdir' in log
 else:assert not(t/'record.json').exists()
 results.append({'case':t.name,'exit':p.returncode,'passed':True,'protected_files_unchanged':True})
 return log
for shell in ['dash','busybox']:
 for name,policy,override,want in [
  ('old_mktemp_eio','none',False,47),
  ('mktemp_eio_bypassed','none',False,0),
  ('explicit_root','none',True,0),
  ('mkdir_eio_fallback','first_fail',True,0),
  ('config_write_fallback','write_fail',True,0),
  ('foreign_symlink_collision','collision',True,0),
  ('all_32_foreign_collisions','exhaust',True,0),
  ('all_roots_fail','all_fail',False,47),
  ('invalid_root_no_shell_eval','none',True,0)]:
  t,app,env,command,before=fixture(name,shell,baseline=name=='old_mktemp_eio')
  env['FAULT_POLICY']=policy
  if override:
   root=t/'runtime root';root.mkdir();env['CPYMO_RUNTIME_ROOT']=str(root)
  if name=='invalid_root_no_shell_eval':env['CPYMO_RUNTIME_ROOT']='$(touch PWNED)'
  log=verify(t,app,env,command,before,want)
  assert not(t/'mktemp_called').exists() or name=='old_mktemp_eio'
  if 'fallback' in name or name=='all_32_foreign_collisions':
   assert 'runtime_root=/dev/shm' in log,log
  if name=='all_roots_fail':assert 'runtime_failure=no_private_writable_memory_directory' in log
  if (t/'foreign_links').exists():
   for link in (t/'foreign_links').read_text().splitlines():
    path=Path(link);assert path.is_symlink() and (path/'keep').read_text()=='never delete this'
   # Only generated test symlinks are removed, after preservation was proved.
   for link in (t/'foreign_links').read_text().splitlines():Path(link).unlink()
  assert not(t/'PWNED').exists()
 # Two live children must own distinct private configs until their own exit.
 t,app,env,command,before=fixture('concurrent',shell)
 def launch(i):
  cfg={**env,'RA_RECORD':str(t/f'record-{i}.json'),'CPYMO_LOG':str(app/f'logs/launcher-{i}.log')}
  p=subprocess.run(command,env=cfg,timeout=12);assert p.returncode==0
  return json.loads(Path(cfg['RA_RECORD']).read_text())
 with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:a,b=list(pool.map(launch,[1,2]))
 assert a['session_path']!=b['session_path'] and all(not Path(r['session_path']).parent.exists() for r in [a,b])
 for path,stamp in before.items():assert (digest(Path(path)),Path(path).stat().st_mtime_ns)==stamp
 results.append({'case':t.name,'passed':True,'distinct_sessions':True,'cleanup':True})
# Exercise mount-prefix selection against representative target mount tables.
# The function body is exact; only /proc/mounts is redirected to a QA table.
production=(ROOT/'deploy/Roms/APPS/CPyMO.sh').read_text()
function=production[production.index('runtime_filesystem() {'):production.index('prepare_runtime_dir() {')]
table=OUT/'mounts';table.write_text('rootfs / rootfs rw 0 0\n/dev/mmc /tmp vfat rw 0 0\ntmpfs /dev tmpfs rw 0 0\ntmpfs /tmp/with\\040space tmpfs rw 0 0\n')
script=OUT/'fs_probe.sh';script.write_text(function.replace('/proc/mounts',str(table))+'\nruntime_filesystem "$1"\n')
for shell in ['dash','busybox']:
 for path,want in [('/tmp/cpymo','vfat'),('/tmp-other/cpymo','rootfs'),('/dev/shm/cpymo','tmpfs'),('/tmp/with space/cpymo','tmpfs'),('/tmp/with spaces/cpymo','vfat'),('/','rootfs')]:
  cmd=(['/usr/bin/busybox','sh'] if shell=='busybox' else ['/bin/sh'])+[str(script),path]
  got=subprocess.check_output(cmd,text=True).strip();assert got==want,(shell,path,got,want)
  results.append({'case':shell+'_mount_prefix_'+path,'passed':True,'filesystem':got})
(QA/'evidence/runtime_faults.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS',len(results),'runtime allocation/fault/collision/concurrency scenarios')
