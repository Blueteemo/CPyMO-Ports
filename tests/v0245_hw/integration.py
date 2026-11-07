"""Hardware-feedback regressions using real libretro inputs and output frames."""
from pathlib import Path
import sys,os,json,re,shutil
import numpy as np
from PIL import Image
sys.path.insert(0,str(Path.cwd()/'repo/tests/v0245'))
from run import b,batch,run
mode=sys.argv[1] if len(sys.argv)>1 else 'arm';results=[]
def check(out,**extra):
 log=(out/'run.log').read_text();assert json.loads((out/'command.json').read_text())['exit']==0
 results.append({'case':out.name,'passed':True,**extra});return log
# The exact same library selection must yield identical pixels after refreshes
# and 15 down/up cycles, with the full 3-line title inside each 104px row.
lib=batch/'library';lib.mkdir()
for i in range(8):
 p=lib/f'Game{i}';p.mkdir();(p/'gameconfig.txt').write_text(f'gametitle,游戏{i+1}标题\\n第二行中文信息\\n第三行副标题\nengineversion,1.2\nplatform,pygame\nscripttype,pymo\nimagesize,800,600\nstartscript,start\nfontsize,24\n')
events=[(100+15*i,32 if i%2==0 else 16) for i in range(30)]
out=run('library_roundtrip_stability',lib,events,mode=mode,menu=True,copy=False,frames=600,capture_every=30)
s=check(out);first=np.asarray(Image.open(out/'frame_0090.ppm'));last=np.asarray(Image.open(out/'frame_0600.ppm'))
assert np.array_equal(first,last),'highlight/background accumulation after returning to the same selection'
# Between row title blocks and below the library area there must be clear gaps.
for k in range(4):
 row=last[k*104:(k+1)*104];white=np.any(row[:,:,0:3]>220,axis=2);yy,xx=np.where(white)
 assert len(yy)>30 and yy.min()>=3 and yy.max()<101,(k,yy.min(),yy.max())
assert not np.any(last[464:]);Image.fromarray(last).save(out/'library_final.png')
# Real settings input: current default is 86%, left once selects the new 72%.
fixture=b/'fixtures/540x360'
events=[(180,4)]+[(210+i*15,32) for i in range(4)]+[(300,64),(360,1)]
out=run('choose_new_small',fixture,events,mode=mode,frames=480,capture_every=120)
s=check(out);assert 'tbfont=22.46400' in s and 'large=2' in s
cfg=out/'game/save/rg35xx-ui.cfg';assert 'CPYMO_UI_3\nfont=small\n' in cfg.read_text()
out2=run('new_small_restart',out/'game',mode=mode,frames=180)
s=check(out2);assert 'tbfont=22.46400' in s and 'large=2' in s
# Menu opens through actual START input and cycles all menu rows.
events=[(180,8)]+[(210+i*15,32) for i in range(18)]+[(540,1)]
out=run('start_menu_repeat',fixture,events,mode=mode,frames=600,capture_every=30)
s=check(out);assert 'shutdown=0' in s
# A single library B sends shutdown; the launcher-only append setting makes
# RetroArch itself quit on content close (verified with the upstream parser).
out=run('library_single_exit',lib,[(100,1)],mode=mode,menu=True,copy=False,frames=180)
s=check(out);assert 'shutdown=1' in s
(batch/'summary.json').write_text(json.dumps({'mode':mode,'results':results},indent=2)+'\n')
print('BATCH',batch)
