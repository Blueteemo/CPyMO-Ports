#!/usr/bin/env python3
"""Parse CPYMO_PERF logs from v0.21, without confusing RetroArch monitor Hz for FPS."""
import re,sys
from pathlib import Path
if len(sys.argv)!=2:
    sys.exit('Usage: python3 tools/parse_device_perf.py CPyMO_RG35XX_uClibc_Test.log')
text=Path(sys.argv[1]).read_text(encoding='utf-8',errors='replace')
rows=[]
for line in text.splitlines():
    if 'CPYMO_PERF' not in line: continue
    pairs={k:int(v) for k,v in re.findall(r'(frames|draws|elapsed_ms|fps_x10|update_us_avg|draw_us_avg|convert_us_avg)=(\d+)',line)}
    if len(pairs)==7: rows.append(pairs)
if not rows:
    print('NO CPYMO_PERF records; this may be an older v0.20 log or missing verbosity.')
    sys.exit(1)
for i,row in enumerate(rows,1):
    print(f"sample={i} frames={row['frames']} redraw={row['draws']} wall_fps={row['fps_x10']/10:.1f} "
          f"update_ms={row['update_us_avg']/1000:.2f} draw_ms={row['draw_us_avg']/1000:.2f} "
          f"convert_ms={row['convert_us_avg']/1000:.2f}")
