"""Real card-version resources and copied user saves, never card writes."""
from pathlib import Path
import os,shutil,sys,json,re
from run import b,batch,run
mode=sys.argv[1] if len(sys.argv)>1 else 'host'
sanitize='--sanitize' in sys.argv
core=b/('asanbuild' if sanitize else ('build-arm' if mode=='arm' else 'hostbuild'))/'cpymo_libretro.so'
water=Path(os.environ['CPYMO_QA_WATER']);yume=Path(os.environ['CPYMO_QA_YUME'])
card=Path(os.environ['CPYMO_QA_CARD_SAVES']);results=[]
# One immutable guest-side asset copy allows fixtures to hardlink within the
# same filesystem. A Mac shared mount cannot be hardlinked into the VM.
assets=b/'game-assets'/water.name
if not assets.exists():
    assets.parent.mkdir(exist_ok=True)
    shutil.copytree(water,assets)
water=assets
advance=[(f,256) for f in range(100,700,8)]
def check(out,*conditions):
    s=(out/'run.log').read_text();r=json.loads((out/'command.json').read_text())
    good=r['exit']==0 and all(c(s) for c in conditions)
    results.append({'case':out.name,'passed':good,'exit':r['exit']})
    if not good:print(s[-4000:])
    return s
def real(name,game,**kw):
    return run(name,game,mode=mode,core=core,sanitize=sanitize,**kw)

# Genuine fresh R2-save / L2-load, with the 540x360 config and real script/PAKs.
check(real('train_540_new_save',water,frames=3600,scenario='train',copy=False),
      lambda s:'TRAIN save' in s and 'TRAIN load' in s,
      lambda s:'SE frame=2400 playing=0 looping=0 saved_name=' in s)

water_saves=next(p for p in card.iterdir() if p.name.startswith('______1-'))
for slot,meaning in [(1,'legacy_train'),(0,'legacy_rain_one_shot')]:
    fixture=batch/('fixture_'+meaning)
    shutil.copytree(water,fixture,copy_function=os.link)
    shutil.copytree(water_saves,fixture/'save')
    # Copy the selected old slot into quick-load's slot in this private fixture.
    shutil.copy2(fixture/f'save/save-{slot:02d}.csav',fixture/'save/save-01.csav') if slot!=1 else None
    out=real('card_'+meaning,fixture,frames=1800,events=advance+[(1000,4096)],copy=False,local_saves=True,capture_every=1800)
    if slot==1:
        check(out,lambda s:'legacy_one_shot_discard name=faan1_b' in s,
              lambda s:'SE frame=1200 playing=0 looping=0 saved_name=' in s)
    else:
        check(out,lambda s:'legacy_one_shot_discard name=rain_1' in s,
              lambda s:'SE frame=1200 playing=0 looping=0 saved_name=' in s)

# The actual script distinguishes rain_1 (one-shot) from rain_12,1 (loop).
fixture=batch/'fixture_real_rain12'
shutil.copytree(b/'fixtures/540x360',fixture)
(fixture/'se').mkdir()
os.link(water/'se/se.pak',fixture/'se/se.pak')
cfg=fixture/'gameconfig.txt';cfg.write_text(cfg.read_text().replace('seformat,wav','seformat,mp3'))
(fixture/'script/start.txt').write_text('#bg test\n#se rain_12,1\n#say ,真实循环雨声音源保存恢复\n#se_stop\n#say ,脚本停止雨声\n#wait 999999\n')
out=real('real_rain12_loop_save_load_stop',fixture,frames=540,
         events=[(180,8192),(240,4096),(300,256)],copy=False,local_saves=True)
check(out,lambda s:'playing=1 looping=1 saved_name=rain_12' in s,
      lambda s:'SE frame=480 playing=0 looping=0 saved_name=' in s)

# Start from the card's real UI_1 large preference; operate the true settings UI.
fixture=batch/'fixture_card_font';shutil.copytree(water,fixture,copy_function=os.link)
shutil.copytree(water_saves,fixture/'save')
events=advance+[(1000,4096),(1500,4)]+[(1530+i*15,32) for i in range(4)]+[(1620,64),(1680,1)]
out=real('card_legacy_font_to_small',fixture,frames=1860,events=events,copy=False,local_saves=True,capture_every=180)
check(out,lambda s:'large=1' in s and 'large=0' in s,
      lambda s:'tbfont=26.83200' in s and 'shutdown=0' in s)
check(real('card_small_restart',fixture,frames=1320,events=advance+[(1000,4096)],copy=False,local_saves=True),
      lambda s:'large=0' in s and 'tbfont=26.83200' in s)

# No AVI exists in this original-only copy. First opening, title and real voice.
out=real('yume_original_no_avi',yume,frames=1200,copy=False)
check(out,lambda s:'line=49710' in s and 'choice=1' in s,
      lambda s:'MOVIE start' not in s and 'shutdown=0' in s)
out=real('yume_original_new_game_voice',yume,frames=1800,copy=False,
         events=[(400,256)]+[(f,256) for f in range(600,1600,20)],capture_every=1800)
check(out,lambda s:bool(re.search(r'CHANNELS .*voice=1',s)),lambda s:'shutdown=0' in s)
(batch/'summary.json').write_text(json.dumps({'scope':mode+' integration on real 540x360 resources and copies of card saves; no hardware acceptance',
    'sanitize':sanitize,'results':results},indent=2)+'\n')
print('BATCH',batch)
assert all(r['passed'] for r in results),results
