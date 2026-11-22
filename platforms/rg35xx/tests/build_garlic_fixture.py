#!/usr/bin/env python3
"""Generate a synthetic PNG/text game in a temp directory; copy a local host font.
No binary proprietary game data or third-party fonts are distributed.
"""
import argparse,os,struct,shutil,subprocess,zlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('dest',type=Path);p.add_argument('--bad-font',action='store_true');p.add_argument('--timing',action='store_true');a=p.parse_args()
r=a.dest
for d in ('script','system','bg'):(r/d).mkdir(parents=True,exist_ok=True)
(r/'gameconfig.txt').write_text('gametitle,RG35XX synthetic test\nengineversion,1.2\nplatform,pygame\nscripttype,pymo\nimagesize,800,600\nstartscript,start\nbgformat,.png\nfontsize,24\n',encoding='utf-8')
(r/'script/start.txt').write_text(('#bg test\n#wait 200\n#bg test\n#wait 200\n' if a.timing else '#bg test\n#say Test,Hello from CPyMO\n#wait 100\n'),encoding='utf-8')
def chunk(k,d):return struct.pack('>I',len(d))+k+d+struct.pack('>I',zlib.crc32(k+d)&0xffffffff)
w,h=32,24; pixels=b''.join(b'\x00'+bytes((40,88,200))*w for _ in range(h))
image=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b'')
for n in ('logo1','logo2','test'):(r/'bg'/f'{n}.png').write_bytes(image)
for n in ('message','name','message_cursor'):(r/'system'/f'{n}.png').write_bytes(image)
if not a.bad_font:
  font=Path(os.environ.get('CPYMO_TEST_FONT','/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'))
  if not font.is_file():
    t=subprocess.run(['fc-match','-f','%{file}\n','sans'],text=True,capture_output=True)
    font=Path(t.stdout.strip().splitlines()[0]) if t.returncode==0 and t.stdout.strip() else font
  if not font.is_file():raise RuntimeError('No local testing TTF; set CPYMO_TEST_FONT=/path/to/local.ttf')
  shutil.copy2(font,r/'system/default.ttf')
print('PASS synthetic fixture:',r)
