from pathlib import Path
import os,subprocess,shutil,json
b=Path.cwd();core=b/'asanbuild/cpymo_libretro.so';off=next(l.split()[0] for l in subprocess.check_output(['nm',str(core)],text=True).splitlines() if l.endswith(' b engine'))
results=[]
for name in ['font_repeat','only_unread','only_read','all_unread','actions','skip_setting','ogg']:
 out=b/'asan_cases'/name;out.mkdir(parents=True,exist_ok=True);source=b/'cases'/name/'game';game=out/'game'
 shutil.copytree(source,game,dirs_exist_ok=True)
 if (game/'save').exists():shutil.rmtree(game/'save')
 if name in ['only_unread','only_read','all_unread']:shutil.copytree(b/'fixtures'/('read_all' if name=='all_unread' else 'read_only')/'save',game/'save')
 env=os.environ.copy();env.update(CPYMO_DEBUG='1',CPYMO_MENU='0',CPYMO_TEST_ENGINE_OFFSET=off,CPYMO_TEST_FRAMES='600',CPYMO_TEST_CAPTURE_EVERY='300',ASAN_OPTIONS='detect_leaks=0')
 with (out/'run.log').open('w') as log:p=subprocess.run([str(b/'capture.asan'),str(core),str(game/'gameconfig.txt'),str(out),str(b/'cases'/name/'timeline.txt')],env=env,stdout=log,stderr=subprocess.STDOUT)
 results.append(dict(case=name,exit=p.returncode));print(name,p.returncode,flush=True)
(b/'evidence/asan_regression.json').write_text(json.dumps(results,indent=2))
