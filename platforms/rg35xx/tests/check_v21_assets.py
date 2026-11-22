#!/usr/bin/env python3
"""No third-party tool dependency except fontTools used to validate menu glyph cmap."""
from pathlib import Path
from fontTools.ttLib import TTFont
from PIL import Image
root=Path(__file__).resolve().parents[1]
game=root/'deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test'
font=TTFont(str(game/'system/default.ttf'))
cmap=font.getBestCmap()
loc=(root/'source/CPyMO-main/cpymo/cpymo_localization.c').read_text()
menu=[c for c in loc if ord(c)>=128]
missing=sorted({c for c in menu if ord(c) not in cmap})
assert not missing, f'Missing localization glyphs: {missing}'
for text in ('中文字体测试：你好，世界！','确定取消存档读档快进隐藏对话框重启游戏'):
  assert all(ord(ch) in cmap for ch in text)
image=Image.open(game/'system/sel_highlight.png')
assert image.mode=='RGBA' and image.size==(240,36)
assert image.getpixel((6,6))[3] >0
print(f'PASS v0.21 menu asset+font: {len(cmap)} glyphs, all {len(set(menu))} localized non-ASCII glyphs covered')
