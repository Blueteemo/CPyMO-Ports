from pathlib import Path
import os,subprocess,json,hashlib,shutil,signal,time,stat,tempfile
ROOT=Path(__file__).resolve().parents[2]
QA=Path(os.environ.get('CPYMO_QA_ROOT',Path.cwd())).resolve()
RESULTS=[]
RUNS=Path(tempfile.mkdtemp(prefix='launcher-run-',dir=QA))
(QA/'evidence/launcher_run_root.txt').write_text(str(RUNS))
BINARY=QA/'ra_config_probe';assert BINARY.is_file()
BUSY=QA/'busybox-bin';BUSY.mkdir(exist_ok=True)
subprocess.run(['/usr/bin/busybox','--install','-s',str(BUSY)],check=True)

def stamp(p):
 return (hashlib.sha256(p.read_bytes()).hexdigest(),p.stat().st_mtime_ns)

def case(name,content,want,mode='dash',exit_code=0,custom=False,missing_static=False,gain='50',signal_test=False,signal_type=signal.SIGTERM):
 t=RUNS/mode/name
 if t.exists():shutil.rmtree(t)
 app=t/'APPS 空格'/ 'CPyMO';game=app/'games/Demo';game.mkdir(parents=True)
 apps=app.parent
 (game/'gameconfig.txt').write_text('gametitle,Test\n')
 (app/'cpymo_libretro.so').write_bytes(b'non-executed core placeholder')
 shutil.copyfile(ROOT/'deploy/Roms/APPS/CPyMO.sh',apps/'CPyMO.sh')
 cfg=app/'retroarch-cpymo.cfg'
 if not missing_static:
  # Deliberately stale/conflicting private values. Final guard must win.
  cfg.write_text('audio_volume = "9.0"\nconfig_save_on_exit = "true"\nauto_overrides_enable = "true"\ninput_hotkey_block_delay = "0"\n')
 (app/'output-volume.txt').write_text(gain+'\n')
 ra=t/'RetroArch 空格';globaldir=ra/'.retroarch';globaldir.mkdir(parents=True)
 globalcfg=globaldir/'retroarch.cfg';globalcfg.write_text('audio_volume = "1.500000"\nconfig_save_on_exit = "true"\nauto_overrides_enable = "true"\n')
 volumecfg=globaldir/'volume.cfg'
 if content is not None:volumecfg.write_bytes(content)
 other=apps/'OtherAPP.dat';other.write_bytes(b'other app unchanged')
 saved=apps/'saves/Demo/save-01.csav';saved.parent.mkdir(parents=True);saved.write_bytes(b'preserve user save')
 override=globaldir/'config/CPyMO/CPyMO.cfg';override.parent.mkdir(parents=True);override.write_text('audio_volume = "12"\nconfig_save_on_exit = "true"\n')
 protected=[globalcfg,other,saved,override]+([volumecfg] if content is not None else [])
 if cfg.exists():protected.append(cfg)
 before={str(p):stamp(p)for p in protected}
 record=t/'record.json';env=os.environ.copy()
 for key in list(env):
  if key.startswith('CPYMO_'):env.pop(key)
 env.update(CPYMO_LOADER='none',CPYMO_RETROARCH_HOME=str(ra),CPYMO_RETROARCH_BIN=str(ROOT/'tests/v0243a/fake_retroarch.py'),RA_PROBE=str(BINARY),RA_RECORD=str(record),RA_EXIT=str(exit_code))
 if mode=='busybox':env['PATH']=str(BUSY)+':/usr/bin:/bin'
 if custom:
  alt=ra/'custom.cfg';alt.write_text('audio_volume = "6"\nconfig_save_on_exit = "true"\n');env['CPYMO_RETROARCH_CONFIG']=str(alt);before[str(alt)]=stamp(alt)
 cmd=(['/usr/bin/busybox','sh']if mode=='busybox'else['/bin/sh'])+[str(apps/'CPyMO.sh')]
 if signal_test:
  env['RA_SIGNAL_WAIT']='1';p=subprocess.Popen(cmd,env=env)
  for _ in range(200):
   if record.exists():break
   if p.poll()is not None:raise AssertionError('launcher exited before recorder')
   time.sleep(.01)
  assert record.exists()
  p.send_signal(signal_type);rc=p.wait(timeout=8);assert rc==128+signal_type,rc
  assert Path(str(record)+'.signal').read_text()==str(signal.SIGTERM)
 else:
  p=subprocess.run(cmd,env=env,timeout=8);rc=p.returncode;assert rc==exit_code,(name,rc)
 got=json.loads(record.read_text());e=got['effective']
 assert e=={'audio_volume':want,'config_save_on_exit':False,'auto_overrides_enable':False},(name,mode,e)
 assert got['gain']==gain,(name,got['gain'])
 assert got['file_mode']=='0o600'and got['dir_mode']=='0o700',got
 assert got['snapshot_mode'] in [None,'0o600'],got
 assert not Path(got['session_path']).exists(),got
 assert not Path(got['session_path']).parent.exists(),got
 assert all(stamp(Path(p))==s for p,s in before.items()),name
 log=(app/'logs/launcher.log').read_text()
 assert 'launcher_revision=0.24.3a' in log and f'ra_audio_volume_db={want:.6f}'in log,log
 assert (app/'logs/launcher.log').stat().st_size<8192
 assert not (apps/'CPyMO.log').exists()
 assert not (t/'PWNED').exists()
 RESULTS.append({'case':name,'shell':mode,'exit':rc,'db':e['audio_volume'],'gain':got['gain'],'volume_status':next(l for l in log.splitlines()if l.startswith('volume_status=')),'global_volume_other_apps_and_saves_unchanged':True,'temporary_files_cleaned':True})
 return t,env,cmd

valid=[('reported',b'audio_volume = "-2.000000"\n',-2),('negative',b'audio_volume = "-12.5"\n',-12.5),('positive',b'audio_volume = "+1.500000"\n',1.5),('minimum',b'audio_volume = "-80.000000"\n',-80),('maximum',b'audio_volume = "12.000000"\n',12),('integer',b'audio_volume = "0"',0),('crlf_tabs',b'# Device volume\r\n\t audio_volume\t= "-2.000000"  # saved\r\n',-2),('leading_zero',b'audio_volume = "001.50"\n',1.5)]
invalid=[('missing',None),('empty',b''),('only_comment',b'# nothing\n'),('unquoted',b'audio_volume = -2\n'),('single_quote',b"audio_volume = '-2'\n"),('unknown_key',b'other = "-2"\n'),('extra_key',b'audio_volume = "-2"\nother = "x"\n'),('duplicate',b'audio_volume = "-2"\naudio_volume = "-3"\n'),('same_duplicate',b'audio_volume = "-2"\naudio_volume = "-2"\n'),('include',b'#include "other.cfg"\naudio_volume = "-2"\n'),('reference',b'#reference "other.cfg"\naudio_volume = "-2"\n'),('nul',b'audio_volume = "-2"\x00\n'),('nul_comment',b'audio_volume = "-2"\n#\x00\n'),('bom',b'\xef\xbb\xbfaudio_volume = "-2"\n'),('long',b'#'+b'x'*1024),('lines',b'\n'*17+b'audio_volume = "-2"\n'),('semicolon',b'audio_volume = "-2"; touch PWNED\n'),('shell_substitution',b'audio_volume = "$(touch PWNED)"\n'),('backtick',b'audio_volume = "`touch PWNED`"\n')]
for value in ['NaN','nan','inf','-inf','1e0','12.000001','-80.000001','13','-81','0000','1.0000000','999999999999999999999999999999999999999999','1.','.5','--2','2,5',' 2','2x']:
 invalid.append(('value_'+value.replace('/','_'),f'audio_volume = "{value}"\n'.encode()))
for mode in ['dash','busybox']:
 for n,data,want in valid:case(n,data,want,mode)
 for n,data in invalid:case(n,data,0,mode)
 case('nonzero_exit',valid[0][1],-2,mode,exit_code=7)
 case('missing_private_cfg',valid[0][1],-2,mode,missing_static=True)
 case('custom_base_cfg',valid[0][1],-2,mode,custom=True)
 case('gain_stays_50',valid[0][1],-2,mode)
 case('signal_cleanup',valid[0][1],-2,mode,signal_test=True)
 case('hup_cleanup',valid[0][1],-2,mode,signal_test=True,signal_type=signal.SIGHUP)
 case('int_cleanup',valid[0][1],-2,mode,signal_test=True,signal_type=signal.SIGINT)
 # Simulate the firmware updating only the independent file between runs.
 t,env,cmd=case('restart',valid[0][1],-2,mode)
 source=t/'RetroArch 空格/.retroarch/volume.cfg';source.write_text('audio_volume = "-6.000000"\n')
 globalcfg=t/'RetroArch 空格/.retroarch/retroarch.cfg';g=stamp(globalcfg);v=stamp(source)
 subprocess.run(cmd,env=env,check=True,timeout=8)
 got=json.loads((t/'record.json').read_text());assert got['effective']['audio_volume']==-6 and got['gain']=='50'
 assert stamp(globalcfg)==g and stamp(source)==v and not Path(got['session_path']).exists()
 RESULTS.append({'case':'restart_updated_independent_volume','shell':mode,'db':-6,'gain':'50','global_and_volume_unchanged':True,'temporary_files_cleaned':True})
(QA/'evidence/launcher_matrix.json').write_text(json.dumps(RESULTS,ensure_ascii=False,indent=2))
print(f'PASS {len(RESULTS)} launch scenarios with unmodified RA 1.14.0 parser, dash + BusyBox ash/awk, read-only global/volume, gain unchanged, cleanup and restart')
