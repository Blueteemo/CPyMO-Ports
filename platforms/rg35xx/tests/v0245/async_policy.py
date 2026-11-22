"""The frontend offers async support, but RG35XX must stay on safe batch audio."""
from pathlib import Path
import os,json,re
from run import b,batch,run
rows=[]
for mode in ['host','arm']:
 out=run('async_offer_'+mode,b/'fixtures/540x360',mode=mode,frames=240,copy=False,accept_async=True)
 s=(out/'run.log').read_text();receipt=json.loads((out/'command.json').read_text())
 assert receipt['exit']==0 and 'ASYNC_OFFERS registrations=0' in s
 assert 'AUDIO frames=192000' in s
 rows.append({'case':out.name,'passed':True,'mode':mode,'policy':'mandatory frame batch, 240x800 stereo frames; no async state access'})
# Counterexample: unmodified old card ELF really registers the optional route.
old=run('v244_async_registration_counterexample',b/'fixtures/540x360',mode='arm',core=Path(os.environ['CPYMO_QA_244_CORE']),frames=240,copy=False,accept_async=True)
s=(old/'run.log').read_text();receipt=json.loads((old/'command.json').read_text())
assert receipt['exit']==91 and 'ASYNC_OFFERS registrations=1' in s
rows.append({'case':old.name,'expected_exit':91,'registrations':1,'scope':'API registration counterexample, not a claim of a physical race/crash'})
(batch/'summary.json').write_text(json.dumps({'results':rows},indent=2)+'\n')
print('BATCH',batch)
