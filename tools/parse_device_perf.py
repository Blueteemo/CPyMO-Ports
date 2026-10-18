#!/usr/bin/env python3
"""Parse CPYMO_PERF logs from v0.21 onwards, without confusing RetroArch monitor
Hz for FPS.

The 7 v0.21 fields are REQUIRED and are what this prints. Later cores add
fields (`outside_us_avg`, `video_us_avg`, `audio_us_avg` in v0.23, for the
inter-frame budget the 10.19 log left unexplained); those lines must not be
skipped, because a log from such a core is exactly the one worth reading. Use
`tools/parse_device_perf2.py` for a weighted summary of all of them.
"""
import re,sys
from pathlib import Path
REQUIRED=('frames','draws','elapsed_ms','fps_x10','update_us_avg','draw_us_avg','convert_us_avg')
if len(sys.argv)!=2:
    sys.exit('Usage: python3 tools/parse_device_perf.py CPyMO_RG35XX_uClibc_Test.log')
text=Path(sys.argv[1]).read_text(encoding='utf-8',errors='replace')
rows=[]
for line in text.splitlines():
    if 'CPYMO_PERF' not in line: continue
    pairs={k:int(v) for k,v in re.findall(r'([a-z0-9_]+)=(\d+)',line)}
    if all(k in pairs for k in REQUIRED): rows.append(pairs)
if not rows:
    print('NO CPYMO_PERF records; this may be an older v0.20 log or missing verbosity.')
    sys.exit(1)
for i,row in enumerate(rows,1):
    extra=(' outside_ms=%.2f'%(row['outside_us_avg']/1000)) if 'outside_us_avg' in row else ''
    print(f"sample={i} frames={row['frames']} redraw={row['draws']} wall_fps={row['fps_x10']/10:.1f} "
          f"update_ms={row['update_us_avg']/1000:.2f} draw_ms={row['draw_us_avg']/1000:.2f} "
          f"convert_ms={row['convert_us_avg']/1000:.2f}{extra}")
