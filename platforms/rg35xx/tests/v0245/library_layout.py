from pathlib import Path
import os,shutil,json,sys
from run import b,batch,run
mode=sys.argv[1] if len(sys.argv)>1 else 'arm'
rows=[]
for count in [0,1,4,8]:
    root=batch/f'library_{count}';root.mkdir()
    for i in range(count):
        d=root/f'Game{i}';d.mkdir()
        title=f'游戏 {i+1} 标题\\n第二行中文信息\\n第三行副标题'
        (d/'gameconfig.txt').write_text(f'gametitle,{title}\nengineversion,1.2\nplatform,pygame\nscripttype,pymo\nimagesize,800,600\nstartscript,start\nfontsize,24\n')
    events=[(50+30*i,32) for i in range(max(0,count-1))]
    out=run(f'library_{count}_render',root,events,frames=420,mode=mode,menu=True,copy=False,capture_every=420)
    s=(out/'run.log').read_text();assert 'shutdown=0' in s and f'games={count}' in s
    # Inspect the exact 640x480 RGB565 frame expanded losslessly by the frontend.
    from PIL import Image
    import numpy as np
    im=Image.open(out/'frame_0420.ppm');px=np.asarray(im)
    ink=np.any(px[432:]!=0,axis=2);ys,xs=np.where(ink)
    assert len(xs)>100 and ys.max()+432<464
    assert not np.any(px[464:])
    if count: assert not np.any(px[:4]>200), 'offscreen text leaked above the first full row (selection fill is allowed)'
    rows.append({'games':count,'footer_ink_bbox':[int(xs.min()),int(ys.min()+432),int(xs.max()+1),int(ys.max()+433)],'passed':True})
(batch/'summary.json').write_text(json.dumps({'scope':mode+' actual output pixels; no hardware acceptance','results':rows},indent=2)+'\n')
print('BATCH',batch);print(json.dumps(rows))
