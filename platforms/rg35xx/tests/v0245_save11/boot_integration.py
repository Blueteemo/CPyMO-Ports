"""Actual core input: boot A skip, held-key drain, and save-toast capture."""
from pathlib import Path
import sys,json,re,os
sys.path.insert(0,str(Path.cwd()/'repo/tests/v0245'))
from run import b,batch,run
mode=sys.argv[1]if len(sys.argv)>1 else 'arm';fixture=b/'fixtures/540x360';rows=[]
cases=[('automatic',[(20,256)],None),('skip_logo1',[(20,256),(35,256)],35),('skip_logo2',[(20,256),(65,256)],65),('held_skip',[(20,256)]+[(f,256)for f in range(35,81)],35),('held_library_selection',[(f,256)for f in range(20,51)],None)]
for name,events,pressed in cases:
 out=run(name,fixture,events,mode=mode,menu=True,frames=150,capture_every=150)
 log=(out/'run.log').read_text();assert json.loads((out/'command.json').read_text())['exit']==0
 first=int(re.search(r'STATE frame=(\d+).*say=1',log)[1])
 if name=='automatic': automatic_first=first
 if pressed:
  assert 'CPYMO_BOOT skip_logos' not in log
  assert first>pressed+25 and first<=automatic_first,(name,first,automatic_first)
 else:
  assert 'CPYMO_BOOT skip_logos' not in log and first>=90,(name,first)
 if name=='held_skip':
  assert not re.search(r'STATE frame=(?:[4-9]\d|1[0-4]\d).*line=4 ',log),'skip key advanced the first say'
 rows.append({'case':name,'passed':True,'first_say_frame':first,'skip_press':pressed})
out=run('quick_save_toast',fixture,[(180,8192),(183,8192)],mode=mode,frames=210,capture_every=210)
assert json.loads((out/'command.json').read_text())['exit']==0
assert 'quick save slot 1: 0' in (out/'run.log').read_text()
rows.append({'case':'quick_save_toast','passed':True})
(batch/'summary.json').write_text(json.dumps({'mode':mode,'results':rows},indent=2)+'\n');print('BATCH',batch)
