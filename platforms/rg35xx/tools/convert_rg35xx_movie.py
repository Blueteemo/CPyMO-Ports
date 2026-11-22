#!/usr/bin/env python3
"""Create a separate original-RG35XX MJPEG/PCM AVI movie sidecar."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path, help='Separate <movie-name>.rg35xx.avi file')
    p.add_argument('--ffmpeg', default='ffmpeg')
    args = p.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if not source.is_file():
        p.error('Source file does not exist')
    if output == source or output.exists():
        p.error('Output must be a new separate file; existing files are never replaced')
    output.parent.mkdir(parents=True, exist_ok=True)
    source_hash = sha256(source)
    cmd = [args.ffmpeg, '-hide_banner', '-nostdin', '-n', '-i', str(source),
           '-map', '0:v:0', '-map', '0:a:0',
           '-vf', 'scale=480:480:force_original_aspect_ratio=decrease:force_divisible_by=2:flags=area,fps=24',
           '-c:v', 'mjpeg', '-q:v', '5', '-pix_fmt', 'yuvj420p',
           '-c:a', 'pcm_s16le', '-ar', '48000', '-ac', '2',
           '-fflags', '+bitexact', '-flags:v', '+bitexact', '-f', 'avi', str(output)]
    version = subprocess.check_output([args.ffmpeg, '-version'], text=True).splitlines()[0]
    with output.with_suffix('.avi.log').open('w') as log:
        result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
    unchanged = sha256(source) == source_hash
    receipt = {'profile': 'original RG35XX; MJPEG, <=480x480 contain, 24fps, stereo s16le 48kHz',
               'source': str(source), 'source_sha256': source_hash, 'source_unchanged': unchanged,
               'output': str(output), 'output_sha256': sha256(output) if output.exists() else None,
               'output_bytes': output.stat().st_size if output.exists() else 0,
               'ffmpeg': version, 'command': cmd, 'exit': result.returncode,
               'status': 'conversion only; runtime/hardware validation is separate'}
    output.with_suffix('.avi.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(receipt, ensure_ascii=False, indent=2))
    return result.returncode or (0 if unchanged else 2)


if __name__ == '__main__':
    raise SystemExit(main())
