from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import numpy as np,json
b=Path.cwd();font=ImageFont.truetype(str(b/'gamebase/Roms/APPS/CPyMO/system/default.ttf'),22)
items=[('默认小字体 · 92% · 同一句对白',b/'cases/small540/frame_0180.ppm'),('设置切换后的大字体 · 同一句对白',b/'cases/font_toggle/frame_0360.ppm'),('未读 · Alpha 1.00',b/'cases/read_alpha/frame_0120.ppm'),('已读 · Alpha 0.75 · 同一句对白',b/'cases/read_alpha/frame_0300.ppm'),('快进模式：仅已读',b/'cases/font_toggle/frame_0300.ppm'),('快进模式：全部',b/'cases/skip_setting/frame_0360.ppm')]
out=Image.new('RGB',(1280,3*520),(25,29,34));draw=ImageDraw.Draw(out)
for i,(label,p) in enumerate(items):
 x=i%2*640;y=i//2*520;draw.text((x+10,y+3),label,font=font,fill='white');out.paste(Image.open(p),(x,y+40))
out.save(b/'UI_实际RGB565对照.png')
a=np.array(Image.open(items[2][1]));c=np.array(Image.open(items[3][1]));mask=(a[330:460,:,0]>200)&(a[330:460,:,1]>200)&(a[330:460,:,2]>200)
r={'unread_bright_pixel_count':int(mask.sum()),'unread_mean_rgb':a[330:460][mask].mean(axis=0).tolist(),'same_read_mean_rgb':c[330:460][mask].mean(axis=0).tolist(),'changed_pixels_body':int(np.any(a[330:460]!=c[330:460],axis=2).sum())}
(b/'evidence/color_metrics.json').write_text(json.dumps(r,indent=2));print(r)
