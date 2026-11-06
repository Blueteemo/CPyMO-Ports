"""Synthetic games; the user-provided font is copied unchanged for local QA."""
from pathlib import Path
from PIL import Image, ImageDraw
import sys, shutil, wave, math, struct, subprocess
dest=Path(sys.argv[1]); font=Path(sys.argv[2])
for w,h in [(320,240),(540,360),(800,600)]:
 r=dest/f'{w}x{h}'
 for d in ['script','bg','system','bgm']: (r/d).mkdir(parents=True,exist_ok=True)
 for n in ['test','logo1','logo2']:
  im=Image.new('RGB',(w,h),(26,40,54)); ImageDraw.Draw(im).rectangle((0,0,w,h*2//3),fill=(80,104,120));im.save(r/'bg'/f'{n}.png')
 for n,ww,hh in [('message',w,h//3),('name',w//3,40),('message_cursor',8,8)]:
  Image.new('RGBA',(ww,hh),(0,0,0,240)).save(r/'system'/f'{n}.png')
 shutil.copyfile(font,r/'system/default.ttf')
 (r/'gameconfig.txt').write_text(f'''gametitle,CPyMO RG35XX regression {w}x{h}
engineversion,1.2
platform,s60v5
scripttype,pymo
imagesize,{w},{h}
startscript,start
bgformat,png
bgmformat,wav
seformat,wav
voiceformat,wav
fontsize,26
textspeed,5
bgmvolume,3
textcolor,#ffffff
msgtb,6,0
msglr,10,10
''')
 text='中文正文颜色与换行测试，水仙的故事仍在继续。ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789'
 (r/'script/start.txt').write_text('#bg test\n#bgm song,1\n'+''.join(f'#say ,第{i}句：{text}\n' for i in range(1,15))+'#wait 999999\n')
 with wave.open(str(r/'bgm/song.wav'),'wb') as f:
  f.setnchannels(2);f.setsampwidth(2);f.setframerate(44100)
  f.writeframes(b''.join(struct.pack('<hh',v,v) for i in range(44100) for v in [round(12000*math.sin(2*math.pi*440*i/44100))]))
print('fixtures',dest)
