"""Matched redraw/transition workloads and real-game input replay.
No wall-clock value from QEMU is a hardware FPS measurement.
"""
from pathlib import Path
import json,os,shutil,re,sys
from PIL import Image,ImageDraw
from run import b,batch,run
mode='arm'
cores={'v243a':Path(os.environ['CPYMO_QA_243_CORE']),
       'v244':Path(os.environ['CPYMO_QA_244_CORE']),
       'v245':Path(os.environ.get('CPYMO_QA_245_CORE',str(b/'build-arm/cpymo_libretro.so')))}
variants=os.environ.get('CPYMO_QA_PERF_VARIANTS','v243a,v244,v245').split(',')
cores={k:v for k,v in cores.items() if k in variants}
mask_only='--mask-only' in sys.argv
rows=[]
def measure(name,game,core,frames=720,events=(),menu=False):
    out=run(name,game,events,frames=frames,mode=mode,core=core,copy=False,quiet=True,menu=menu)
    s=(out/'run.log').read_text();assert 'shutdown=0' in s
    receipt=json.loads((out/'command.json').read_text());assert receipt['exit']==0
    timing=re.search(r'FRAME_TIME n=(\d+) mean_us=([\d.]+) p95_us=(\d+) p99_us=(\d+) max_us=(\d+)',s)
    assert timing
    m=(out/'process_metrics.txt').read_text()
    rows.append({'case':name,'frames':int(timing[1]),'mean_us':float(timing[2]),
                 'p95_us':int(timing[3]),'p99_us':int(timing[4]),'max_us':int(timing[5]),
                 'rss_kib':int(re.search(r'Maximum resident set size \(kbytes\): (\d+)',m)[1]),
                 'first_scene':re.findall(r'FIRST_SCENE .*',s),
                 'core_stages':[l for l in s.splitlines() if l.startswith(('CPYMO_LOAD','CPYMO_SWITCH','CPYMO_STAGE','CPYMO_CACHE','CPYMO_PERF'))]})
for w,h in ([(800,600)] if mask_only else [(320,240),(540,360),(800,600)]):
    for workload in (['mask'] if mask_only else ['static','transition']+(['mask'] if w==800 else [])):
        f=batch/f'fixture_{w}x{h}_{workload}';shutil.copytree(b/f'fixtures/{w}x{h}',f)
        cfg=f/'gameconfig.txt';cfg.write_text(cfg.read_text().replace('bgformat,png','bgformat,jpg'))
        im=Image.open(os.environ['CPYMO_QA_BACKGROUND']).convert('RGB').resize((w,h),Image.Resampling.LANCZOS)
        im.save(f/'bg/first.jpg',quality=95)
        # A second safe background ensures transitions produce distinct frames.
        im.transpose(Image.Transpose.FLIP_LEFT_RIGHT).save(f/'bg/second.jpg',quality=95)
        for n in ['logo1','logo2']:im.save(f/f'bg/{n}.jpg',quality=95)
        (f/'chara').mkdir(exist_ok=True)
        sprite=Image.new('RGBA',(w,h),(0,0,0,0));draw=ImageDraw.Draw(sprite)
        draw.ellipse((w//4,h//5,w*3//4,h*4//5),fill=(80,190,240,150))
        sprite.save(f/'chara/sprite.png')
        script='#bg first,BG_NOFADE\n'
        if workload=='static':script+='#wait 999999\n'
        elif workload=='mask':
            shutil.copy2(Path(os.environ['CPYMO_QA_YUME'])/'system/efmsk_07.png',f/'system/efmsk_07.png')
            script+='#label again\n#bg second,efmsk_07,250\n#bg first,efmsk_07,250\n#goto again\n'
        else:script+='#label again\n#chara 0,sprite,50,0,200\n#bg second,BG_ALPHA,250\n#bg first,BG_FADE,250\n#goto again\n'
        (f/'script/start.txt').write_text(script)
        for label,core in cores.items():measure(f'{label}_{w}x{h}_{workload}',f,core,frames=360 if mask_only else 720)
if not mask_only:
    choice=batch/'choice_library';choice.mkdir()
    chosen=choice/'Yume';chosen.mkdir()
    yume=Path(os.environ['CPYMO_QA_YUME'])
    shutil.copy2(yume/'gameconfig.txt',chosen/'gameconfig.txt')
    for child in yume.iterdir():
        if child.is_dir(): (chosen/child.name).symlink_to(child,target_is_directory=True)
    dummy=choice/'Z_Dummy';dummy.mkdir()
    shutil.copy2(b/'fixtures/540x360/gameconfig.txt',dummy/'gameconfig.txt')
    for label,core in cores.items():
        measure(label+'_real_yume',Path(os.environ['CPYMO_QA_YUME']),core,frames=1800,
                events=[(f,256) for f in range(160,1800,20)])
        measure(label+'_real_water540',Path(os.environ['CPYMO_QA_WATER']),core,frames=1800,
                events=[(f,256) for f in range(100,1600,20)])
        measure(label+'_choose_yume',choice,core,frames=360,
                events=[(20,256)],menu=True)
(batch/'summary.json').write_text(json.dumps({'scope':'QEMU, unpaced callback timings including frontend test callback, filesystem warm; not board FPS',
    'results':rows},indent=2)+'\n')
print('BATCH',batch)
print(json.dumps([{k:v for k,v in r.items() if k!='core_stages'} for r in rows],indent=2))
