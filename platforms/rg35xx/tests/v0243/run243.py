from pathlib import Path
import os,subprocess,shutil,json,sys
b=Path(os.environ.get('CPYMO_QA_ROOT',Path.cwd())).resolve()
sdk=b/'handoff/04_构建环境与脚本/local-sdk/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot'
def run(name,fixture='540x360',events=None,frames=600,mode='arm',core=None,reuse=False):
 out=b/'cases'/name;out.mkdir(parents=True,exist_ok=True);game=out/'game'
 if not reuse:
  if game.exists():shutil.rmtree(game)
  shutil.copytree(b/'fixtures'/fixture,game)
 tl=out/'timeline.txt';tl.write_text(''.join(f'{f} {m}\n' for f,m in (events or [])))
 core=Path(core or b/('build243' if mode=='arm' else 'hostbuild')/'cpymo_libretro.so').resolve()
 off=next(l.split()[0] for l in subprocess.check_output(['nm',str(core)],text=True).splitlines() if l.endswith(' b engine'))
 env=os.environ.copy();env.update(CPYMO_MENU='0',CPYMO_DEBUG='1',CPYMO_TEST_ENGINE_OFFSET=off,CPYMO_TEST_CAPTURE_EVERY='60',CPYMO_TEST_FRAMES=str(frames));env.pop('CPYMO_SAVE_ROOT',None)
 cmd=([shutil.which('qemu-arm'),'-L',str(sdk/'arm-miyoo-linux-uclibcgnueabi/sysroot')] if mode=='arm' else [])+[str(b/f'capture.{mode}'),str(core),str(game/'gameconfig.txt'),str(out),str(tl)]
 with (out/'run.log').open('w') as log:p=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 (out/'command.json').write_text(json.dumps({'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith('CPYMO_')},'exit':p.returncode},ensure_ascii=False,indent=2))
 print(name,p.returncode,flush=True);return p.returncode
if __name__=='__main__':
 events=[(120,4),(160,32),(190,32),(220,32),(250,32),(280,128),(320,1)]
 sys.exit(run('font_toggle',events=events,mode=sys.argv[1] if len(sys.argv)>1 else 'arm'))
