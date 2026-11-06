"""Resize finalized artwork only; reproduce from the v0.24.2 committed PNG.
No AI generation, repainting, colour adjustment or new typography.
Reference Imgs.zip metrics are recorded in docs/v0243/icon_metrics.json.
"""
from pathlib import Path
from PIL import Image
from io import BytesIO
import subprocess,hashlib
root=Path(__file__).resolve().parents[2]
raw=subprocess.check_output(['git','-C',str(root),'show','c82881c661aa5a1fac7f5fef9888c3f2cabb090b:deploy/Roms/APPS/Imgs/CPyMO.png'])
assert hashlib.sha256(raw).hexdigest()=='a300b2c061c2e23421163a21ef38376b4557be55d7db0ff3544228d677443b9c'
im=Image.open(BytesIO(raw)).convert('RGBA');art=im.crop((31,19,270,258))
im.paste((0,0,0,0),(0,0,640,258))
im.alpha_composite(art.resize((190,190),Image.Resampling.LANCZOS),(60,68))
im.save(root/'deploy/Roms/APPS/Imgs/CPyMO.png')
