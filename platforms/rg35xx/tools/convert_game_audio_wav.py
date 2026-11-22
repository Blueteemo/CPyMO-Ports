#!/usr/bin/env python3
"""Create a SEPARATE RG35XX PCM-WAV test game copy; never change the input.

WAV fallback has bounded 32 MiB per file and is NOT a compact/audio codec solution.
Packed SE/voice packages are unsupported by the prototype backend.
"""
from __future__ import annotations
import argparse
import pathlib
import shutil
import subprocess
import sys
import wave

MAX_AUDIO=32*1024*1024
TYPES={"bgm":"bgmformat","se":"seformat","voice":"voiceformat"}

def convert(source:pathlib.Path,target:pathlib.Path,ffmpeg:str)->None:
    cmd=[ffmpeg,"-hide_banner","-loglevel","error","-nostdin","-y","-i",str(source),"-vn","-map","0:a:0", "-ar","22050","-ac","2","-c:a","pcm_s16le",str(target)]
    subprocess.run(cmd,check=True)
    with wave.open(str(target),'rb') as wav:
        if (wav.getnchannels(),wav.getframerate(),wav.getsampwidth(),wav.getcomptype()) != (2,22050,2,'NONE'):
            raise RuntimeError("Unexpected PCM WAV format: "+str(target))
    if target.stat().st_size > MAX_AUDIO:
        raise ValueError(f"Converted WAV exceeds 32 MiB maximum: {target}")

def main()->None:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input",type=pathlib.Path)
    ap.add_argument("output",type=pathlib.Path)
    ns=ap.parse_args()
    src=ns.input.resolve(); dst=ns.output.resolve()
    if not src.is_dir() or not (src/'gameconfig.txt').is_file():ap.error("Input must be a PyMO game directory with gameconfig.txt")
    if dst==src or src in dst.parents or dst in src.parents:ap.error("Input/output must be separate, non-nested paths")
    if dst.exists():ap.error("Output already exists; no overwrite is allowed")
    ffmpeg=shutil.which('ffmpeg')
    if not ffmpeg:ap.error("ffmpeg executable not available")
    # Every known binary pack format must be handled before claiming audio coverage.
    for folder in TYPES:
        if any((src/(folder+'.pak')).exists() for _ in (0,)):
            ap.error(f"Packed audio {folder}.pak needs another decoder; refusing incomplete conversion")
    tmp=dst.with_name(dst.name+'.partial')
    if tmp.exists():ap.error("Temporary output already exists")
    try:
        shutil.copytree(src,tmp,symlinks=False)
        conf=(tmp/'gameconfig.txt')
        text=conf.read_text(encoding='utf-8-sig')
        lines=text.splitlines()
        oldext={k:'mp3' for k in TYPES}
        for line in lines:
            p=line.split(',',1)
            if len(p)==2:
                for key,cfgkey in TYPES.items():
                    if p[0].strip().lower()==cfgkey:
                        oldext[key]=p[1].strip().lstrip('.').lower()
        if any(not ext or len(ext)>8 for ext in oldext.values()):
            raise ValueError('Invalid game audio extension')
        changed=[]
        for folder,ext in oldext.items():
            d=tmp/folder
            if not d.exists():continue
            for asset in sorted(d.iterdir()):
                if not asset.is_file() or asset.suffix.lower()!='.'+ext:continue
                dest=asset.with_suffix('.wav')
                if dest!=asset and dest.exists():raise ValueError(f'Name collision: {dest}')
                scratch=asset.with_name(asset.stem+'.rg35xx-tmp.wav')
                if scratch.exists():raise ValueError(f'Temporary collision: {scratch}')
                convert(asset,scratch,ffmpeg)
                asset.unlink()
                scratch.replace(dest)
                changed.append(str(dest.relative_to(tmp)))
        out=[];existing=set()
        for line in lines:
            parts=line.split(',',1)
            key=parts[0].strip().lower()
            if key in TYPES.values():
                out.append(parts[0]+',wav');existing.add(key)
            else:out.append(line)
        for cfgkey in TYPES.values():
            if cfgkey not in existing:out.append(cfgkey+',wav')
        conf.write_text('\n'.join(out)+'\n',encoding='utf-8')
        tmp.rename(dst)
        print(f"PASS: converted {len(changed)} assets; original unchanged; output={dst}")
    except Exception:
        if tmp.exists():shutil.rmtree(tmp)
        raise

if __name__=='__main__':
    try:main()
    except (subprocess.CalledProcessError,OSError,ValueError,RuntimeError) as e:
        print(f"Conversion failed safely: {e}",file=sys.stderr);sys.exit(1)
