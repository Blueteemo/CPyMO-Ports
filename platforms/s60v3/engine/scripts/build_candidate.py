#!/usr/bin/env python3
"""Build the shared-resource S60v3 release from the recovered R1 toolchain.
Source toolchain/activate.sh. No original CPyMO objects or archives overwritten.
"""
from pathlib import Path
import concurrent.futures,hashlib,json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
R1=Path(os.environ['CPYMO_R1_ROOT']).resolve()
SDK=R1/'toolchain/sdk-s60-fp1/epoc32';GCCE=R1/'toolchain/gcce-2005q1c'
OUT=ROOT/'build';OUT.mkdir(exist_ok=True);(OUT/'logs').mkdir(exist_ok=True)
PACKAGE_NAME='CPyMO_S60v3_1.1.0'
FLAGS=['-std=gnu99','-O1','-mthumb','-march=armv5t','-mthumb-interwork','-msoft-float','-fno-unit-at-a-time','-nostdinc','-D__SYMBIAN32__','-D__GCCE__','-D__EPOC32__','-D__MARM__','-D__MARM_ARMV5__','-D__ARMV5__','-D__SUPPORT_CPP_EXCEPTIONS__','-include',str(SDK/'include/gcce/gcce.h'),'-DDISABLE_FFMPEG_AUDIO','-DCPYMO_S60_AUDIO','-DDISABLE_MOVIE','-DS60V3','-DCPYMO_S60_NATIVE_FONT','-DCPYMO_S60_DATA_DIR="C:/Data/CPyMO"','-DOPENC','-DSCREEN_BPP=16','-DEXIT_TO_GAME_SELECTOR','-Dalloca=__builtin_alloca','-DSTBI_NO_THREAD_LOCALS','-I'+str(R1/'cpymo_backend_arm_probe/include'),'-I'+str(SDK/'include/stdapis'),'-I'+str(SDK/'include'),'-I'+str(GCCE/'lib/gcc/arm-none-symbianelf/3.4.3/include')]
LOG=[]
def run(label,cmd,cwd=ROOT):
 p=subprocess.run([str(x) for x in cmd],cwd=cwd,capture_output=True,text=True,timeout=180)
 (OUT/'logs'/(label+'.log')).write_text(p.stdout+p.stderr)
 row={'label':label,'command':[str(x) for x in cmd],'cwd':str(cwd),'returncode':p.returncode};LOG.append(row)
 (OUT/'commands.json').write_text(json.dumps(LOG,indent=2))
 if p.returncode:raise RuntimeError(label+' failed, '+str(OUT/'logs'/(label+'.log')))
 return p

def main():
 required = [GCCE/'bin/arm-none-symbianelf-gcc', SDK/'include/e32base.h',
             SDK/'include/gcce/gcce.h', SDK/'release/armv5/urel/ESDL.lib']
 missing = [str(path) for path in required if not path.is_file()]
 if missing:
  sys.exit('Restore the S60v3 FP1 build inputs first:\n' + '\n'.join(missing))
 sdl_override=os.environ.get('CPYMO_SDL_LIBRARY')
 if sdl_override:
  sdl_lib=Path(sdl_override).resolve()
  if not sdl_lib.is_file():sys.exit('CPYMO_SDL_LIBRARY does not name a file')
 else:
  run('sdl-exit-and-startup',['python3',ROOT/'sdl_exit_fix/build_sdl_exit_fix.py'])
  sdl_lib=ROOT/'sdl_exit_fix/ESDL_cpymo.lib'
 startup=(ROOT/'assets/startup.png').read_bytes()
 # Compile the complete frame into the executable so a stale external splash cannot take precedence.
 rows_of_bytes=('    '+', '.join(f'0x{b:02x}' for b in startup[i:i+16])+','
                for i in range(0,len(startup),16))
 (OUT/'cpymo_s60_startup_image.h').write_text(
     'static const unsigned char cpymo_s60_startup_png[] = {\n'+'\n'.join(rows_of_bytes)+'\n};\n')
 header_digest=hashlib.sha256(b''.join(p.read_bytes() for p in sorted((ROOT/'source').rglob('*.h')) if p.is_file())).hexdigest()
 def comp(item):
  kind,src=item;obj=OUT/(kind+'_'+src.stem+'.o')
  splash_source=kind=='backend' and src.name=='main.c'
  includes=['-I'+str(OUT)] if splash_source else []
  cmd=[str(GCCE/'bin/arm-none-symbianelf-gcc'),*FLAGS,*includes,'-c',str(src),'-o',str(obj)]
  sig=hashlib.sha256((header_digest+json.dumps(cmd)).encode()+src.read_bytes()+
      (startup if splash_source else b'')).hexdigest();marker=obj.with_suffix('.sig')
  if obj.exists() and marker.exists() and marker.read_text()==sig:return {'source':str(src),'object':str(obj),'reused':True}
  p=subprocess.run(cmd,capture_output=True,text=True,timeout=180);(OUT/'logs'/(kind+'_'+src.stem+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:return {'source':str(src),'returncode':p.returncode,'command':cmd}
  marker.write_text(sig);return {'source':str(src),'object':str(obj),'command':cmd,'returncode':0,'sha256':hashlib.sha256(obj.read_bytes()).hexdigest()}
 sources=[(k,p) for k,d in [('core',ROOT/'source/cpymo'),('backend',ROOT/'source/cpymo-backends/sdl1')] for p in sorted(d.glob('*.c'))]
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=list(pool.map(comp,sources))
 (OUT/'compile_result.json').write_text(json.dumps({'flags':FLAGS,'files':rows},indent=2))
 bad=[r for r in rows if r.get('returncode',0)]
 if bad: print(json.dumps(bad));sys.exit(1)
 for kind in ['core','backend']:
  lib=SDK/f'release/armv5/urel/cpymo_font_trial_{kind}.lib'
  # Recreate only our own archive to avoid carrying removed members forward.
  if lib.exists():lib.unlink()
  run('archive-'+kind,[GCCE/'bin/arm-none-symbianelf-ar','rs',lib,*[r['object'] for r in rows if Path(r['object']).name.startswith(kind+'_')]])
  shutil.copy2(lib,OUT/lib.name)
 mad_dir=ROOT/'source/thirdparty/libmad'
 mad_flags=[flag for flag in FLAGS if flag not in ('-mthumb','-O1')]
 mad_objects=[]
 for name in ('bit','fixed','frame','huffman','layer12','layer3','stream','synth','timer'):
  obj=OUT/f'libmad_{name}.o'
  run(f'libmad-{name}', [GCCE/'bin/arm-none-symbianelf-gcc',*mad_flags,
      '-marm','-O2','-DFPM_ARM','-DOPT_SPEED','-DHAVE_LIMITS_H','-DNDEBUG',
      '-c',mad_dir/f'{name}.c','-o',obj])
  mad_objects.append(obj)
 mad_archive=SDK/'release/armv5/urel/cpymo_font_trial_mp3.lib'
 run('archive-libmad',[GCCE/'bin/arm-none-symbianelf-ar','rs',mad_archive,*mad_objects])
 shutil.copy2(mad_archive,OUT/mad_archive.name)
 target_sdl=SDK/'release/armv5/urel/ESDL_cpymo.lib'
 if sdl_lib != target_sdl.resolve():shutil.copy2(sdl_lib,target_sdl)
 run('bldmake',['bldmake','bldfiles'],ROOT/'app')
 run('makefile',['abld','makefile','gcce'],ROOT/'app')
 # Generated SDK makefiles omit static-archive prerequisites; force our ELF/E32 relink.
 for stale in [SDK/('build'+str(ROOT/'app/cpymo_font_trial/gcce/urel/cpymo_font_trial.exe')),SDK/'release/gcce/urel/cpymo_font_trial.exe']:
  if stale.exists():stale.unlink()
 run('symbian-build',['abld','build','gcce','urel'],ROOT/'app')
 exe=SDK/'release/gcce/urel/cpymo_font_trial.exe';shutil.copy2(exe,OUT/exe.name)
 run('e32-headers',['elftran','-dump','hsi',exe])
 # Define expected installation payload before checking final package files.
 package=ROOT/'package';package.mkdir(exist_ok=True)
 icon=OUT/'cpymo_font_trial_icons.mbm'
 run('launcher-icon',['bash',ROOT/'scripts/build_launcher_icon.sh',ROOT/'assets/app-icon.png',icon])
 expected=[(OUT/'cpymo_font_trial.exe','!:\\sys\\bin\\cpymo_font_trial.exe'),(SDK/'data/z/resource/apps/cpymo_font_trial.rsc','!:\\resource\\apps\\cpymo_font_trial.rsc'),(SDK/'data/z/resource/apps/cpymo_font_trial_loc.rsc','!:\\resource\\apps\\cpymo_font_trial_loc.rsc'),(icon,'!:\\resource\\apps\\cpymo_font_trial_icons.mbm'),(SDK/'data/z/private/10003a3f/apps/cpymo_font_trial_reg.rsc','!:\\private\\10003a3f\\import\\apps\\cpymo_font_trial_reg.rsc'),(ROOT/'assets/fonts/default.otf','C:\\Data\\CPyMO\\fonts\\default.otf'),(ROOT/'assets/fonts/OFL.txt','C:\\Data\\CPyMO\\fonts\\OFL.txt'),(ROOT/'LICENSE_CPyMO','C:\\Data\\CPyMO\\licenses\\CPyMO-AGPL-3.0.txt'),(ROOT/'NOTICE.txt','C:\\Data\\CPyMO\\licenses\\NOTICE.txt')]
 for font_file in ('story-pixel.ttf', 'WenQuanYi-GPL-2.0.txt', 'WenQuanYi-README.txt',
     'WenQuanYi-AUTHORS.txt', 'story-fusion-12.ttf', 'Fusion-OFL.txt',
     'Fusion-ark-pixel-OFL.txt', 'Fusion-cubic-11-OFL.txt', 'Fusion-galmuri-LICENSE.txt'):
  folder = 'fonts' if font_file.endswith('.ttf') else 'licenses'
  expected.append((ROOT/'assets/fonts'/font_file, f'C:\\Data\\CPyMO\\{folder}\\{font_file}'))
 payload=[]
 for src,dest in expected:
  assert src.is_file(),src
  staged=package/'payload'/src.name;staged.parent.mkdir(exist_ok=True);shutil.copy2(src,staged)
  payload.append({'source':str(staged),'destination':dest,'bytes':staged.stat().st_size,'sha256':hashlib.sha256(staged.read_bytes()).hexdigest()})
 (package/'EXPECTED_PAYLOAD.json').write_text(json.dumps({'version':'1.1.0','uid':'0xe93c60a3','files':payload},indent=2))
 pkg=['; Independent CPyMO S60v3 shared-resource release','&EN','#{"CPyMO"},(0xe93c60a3),1,1,0','%{"CPyMO contributors"}',':"CPyMO contributors"','[0x101F7961],0,0,0,{"S60ProductID"}','(0x20013851),1,7,0,{"P.I.P.S."}']+['"'+i['source']+'"-"'+i['destination']+'"' for i in payload]
 (package/'cpymo_font_trial.pkg').write_text('\n'.join(pkg)+'\n')
 unsigned=package/(PACKAGE_NAME+'.sis')
 signed=package/(PACKAGE_NAME+'.sisx')
 run('makesis',['makesis','cpymo_font_trial.pkg',unsigned],package)
 if os.environ.get('CPYMO_UNSIGNED_ONLY') == '1':
  print(unsigned)
  return
 cert=Path(os.environ['CPYMO_SIGN_CERT']).resolve()
 key=Path(os.environ['CPYMO_SIGN_KEY']).resolve()
 assert cert.is_file() and key.is_file(),'Supply the signing certificate and key; no private key is distributed.'
 run('signsis',['signsis',unsigned,signed,cert,key])
 run('signature',['signsis','-o',signed])
 report={'scope':'build/link/package only, final runtime validation separate','cpymo_commit':'5a746bc72a4a8c127e0aee7deb20bf415c385ae2','compiled_sources':len(rows),'audio':True,'video':'Symbian system player','image_scaling':'S60 nearest-neighbor required for legacy 540px UI on 320x240 games','expected_payload_files':len(payload),'artifacts':[{'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [exe,signed]]}
 (OUT/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if __name__=='__main__':main()
