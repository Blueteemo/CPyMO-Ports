#!/usr/bin/python3
"""RA invocation recorder. Real 1.14.0 config parser; no audio device/core run."""
import json,os,sys,subprocess,signal,time
from pathlib import Path
args=sys.argv[1:];assert args.count('--appendconfig')==1,args
base=args[args.index('--config')+1]
files=args[args.index('--appendconfig')+1].split('|')
effective=json.loads(subprocess.check_output([os.environ['RA_PROBE'],base,*files],text=True))
record={'argv':args,'effective':effective,'gain':os.environ.get('CPYMO_OUTPUT_PERCENT'),'session_path':files[-1],'session':Path(files[-1]).read_text(),'file_mode':oct(Path(files[-1]).stat().st_mode&0o777),'dir_mode':oct(Path(files[-1]).parent.stat().st_mode&0o777)}
source=Path(files[-1]).parent/'volume.input'
record['snapshot_mode']=oct(source.stat().st_mode&0o777) if source.exists() else None
Path(os.environ['RA_RECORD']).write_text(json.dumps(record,ensure_ascii=False,indent=2))
if os.environ.get('RA_SIGNAL_WAIT'):
 def term(signum,frame):
  Path(os.environ['RA_RECORD']+'.signal').write_text(str(signum));sys.exit(0)
 signal.signal(signal.SIGTERM,term)
 while True:time.sleep(.05)
sys.exit(int(os.environ.get('RA_EXIT','0')))
