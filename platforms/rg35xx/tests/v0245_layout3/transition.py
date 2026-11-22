"""Every output frame stays complete from the library to the first game draw."""
from pathlib import Path
import sys,os,json,shutil,re
import numpy as np
from PIL import Image
sys.path.insert(0,str(Path.cwd()/'repo/tests/v0245'))
from run import b,batch,run
mode=sys.argv[1] if len(sys.argv)>1 else 'host';results=[]
for dims in ['320x240','540x360','800x600']:
 out=run('transition_'+dims,b/'fixtures'/dims,[(20,256)],menu=True,mode=mode,frames=90,capture_every=1)
 assert json.loads((out/'command.json').read_text())['exit']==0
 frames=[np.asarray(Image.open(out/f'frame_{i:04d}.ppm')).astype(np.int16) for i in range(19,91)]
 def is_game(im):return np.mean((im[:,:,2]>im[:,:,0]+10)&(im[:,:,1]>im[:,:,0]+10))>.5
 scene=int(re.search(r'FIRST_SCENE frame=(\d+)',(out/'run.log').read_text())[1])
 first=next(i for i,im in enumerate(frames) if not np.array_equal(im,frames[0]));assert scene<=19+first<=scene+2
 for im in frames[:scene-19]:assert np.array_equal(im,frames[0]),'library text/backdrop changed before first game draw'
 assert any(is_game(im) for im in frames[first:]),'game never appears after the authored logo fade'
 assert 'CPYMO_TRANSITION first_game_frame ready=1' in (out/'run.log').read_text()
 Image.fromarray(frames[first-1].astype('uint8')).save(out/'before_game.png');Image.fromarray(frames[first].astype('uint8')).save(out/'first_game.png')
 results.append({'case':out.name,'passed':True,'held_frames_after_A':first-1,'no_incomplete_library_frame':True})
# A fully black first logo is intentional content, so it must release the hold.
game=batch/'black_logo';shutil.copytree(b/'fixtures/540x360',game)
Image.new('RGB',(540,360),'black').save(game/'bg/logo1.png')
out=run('black_first_frame',game,[(20,256)],menu=True,mode=mode,frames=90,capture_every=1)
assert json.loads((out/'command.json').read_text())['exit']==0
before=np.asarray(Image.open(out/'frame_0019.ppm'))
first=next(i for i in range(20,91) if not np.array_equal(np.asarray(Image.open(out/f'frame_{i:04d}.ppm')),before))
assert not np.any(np.asarray(Image.open(out/f'frame_{first:04d}.ppm')))
assert 'CPYMO_TRANSITION first_game_frame ready=1' in (out/'run.log').read_text()
results.append({'case':'black_first_frame','passed':True,'release_frame':first})
# The prior deployed core is an explicit negative control for the reported blink.
if mode=='host' and os.environ.get('CPYMO_QA_PREVIOUS_HOST_CORE'):
 out=run('previous_core_blink',b/'fixtures/540x360',[(20,256)],menu=True,mode=mode,frames=90,capture_every=1,core=Path(os.environ['CPYMO_QA_PREVIOUS_HOST_CORE']))
 frames=[np.asarray(Image.open(out/f'frame_{i:04d}.ppm')).astype(np.int16) for i in range(19,91)]
 scene=int(re.search(r'FIRST_SCENE frame=(\d+)',(out/'run.log').read_text())[1]);bad=sum(not np.array_equal(im,frames[0]) for im in frames[:scene-19]);assert bad>0
 results.append({'case':'previous_core_blink','passed':True,'incomplete_frames_reproduced':bad})
(batch/'summary.json').write_text(json.dumps({'mode':mode,'scope':'software frame continuity, no hardware timing claim','results':results},indent=2)+'\n')
print('BATCH',batch)
