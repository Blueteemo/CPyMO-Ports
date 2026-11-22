from pathlib import Path
import json,re,struct
b=Path.cwd();checks=0
def ck(x,msg):
 global checks
 checks+=1
 assert x,msg
log=lambda n:(b/'cases'/n/'run.log').read_text()
ck('tbfont=31.20000' in log('font_toggle'),'immediate reflow')
ck('history=1' in log('font_toggle') and 'history=2' not in log('font_toggle'),'history not duplicated')
ck('large=1' in log('font_toggle_restart'),'font restart')
ck('onlyread=0' in log('skip_setting_restart'),'skip restart')
ck('history=1 onlyread=1' in log('only_unread') and 'history=2' not in log('only_unread'),'R1 stops unread')
ck('history=27 onlyread=1' in log('only_read'),'R1 advances read')
ck('history=31 onlyread=0' in log('all_unread'),'R1 advances unread all')
ck('read=0 alpha=1.0000' in log('read_alpha') and 'read=1 alpha=0.7500' in log('read_alpha'),'read state shared')
ck('history=2' in log('font_repeat') and 'line=4' in log('font_repeat'),'reflow pages advance intact')
ck('BGM event=loop name=e01 out48k=5821757 source=5348736' in log('bgm_full'),'QEMU full BGM duration')
for n in ['small320','small800','android','ogg','mp3','real_narcissu']:
 ck('AUDIO frames=' in log(n),'regression '+n)
ck(all(x['exit']==0 for x in json.loads((b/'evidence/regression.json').read_text())),'QEMU exits')
ck(all(x['exit']==0 for x in json.loads((b/'evidence/asan_regression.json').read_text())),'ASan UBSan exits')
ck('PASS 66286 checks' in (b/'evidence/render_check.log').read_text(),'exact renderer checks')
# No unapproved volume or input changes.
import subprocess
changed=subprocess.check_output(['git','-C',str(b/'repo'),'diff','--name-only','c82881c'],text=True)
ck('CPyMO.sh' not in changed and 'garlic_adapter.c' not in changed,'launch/input unchanged')
print('PASS',checks,'scenario assertions')
(b/'evidence/scenario_assertions.txt').write_text(f'PASS {checks} scenario assertions\n')
