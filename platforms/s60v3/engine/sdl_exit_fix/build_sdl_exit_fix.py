#!/usr/bin/env python3
"""Rebuild the exit, cold-start and landscape redraw paths in a private Symbian SDL archive."""
from pathlib import Path
import difflib,hashlib,json,os,shlex,shutil,subprocess
D=Path(__file__).resolve().parent
R=Path(os.environ.get('CPYMO_R1_ROOT','/workspace/scratch/3dcdfea37dce/research/s60_trial')).resolve()
SDK=R/'toolchain/sdk-s60-fp1/epoc32';GCCE=R/'toolchain/gcce-2005q1c/bin'
SDL=R/'symbian-sdl-libs/SDL-1.2.14_ag';src=SDL/'Symbian/EBasicApp.cpp'
view=SDL/'Symbian/SDLSXXView.cpp'
video=SDL/'src/video/symbian/SDL_epocvideo.cpp'
landscape=video.with_name('sdl_epocvideo_s60v3.inl')
events=video.with_name('SDL_epocevents.cpp')
archive=SDK/'release/armv5/urel/ESDL.lib';patched_archive=D/'ESDL_cpymo.lib'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
commands=[]
def run(label,args):
 p=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=120)
 (D/(label+'.log')).write_text(p.stdout+p.stderr)
 commands.append({'label':label,'command':[str(x) for x in args],'returncode':p.returncode})
 (D/'commands.json').write_text(json.dumps(commands,indent=2))
 if p.returncode:raise RuntimeError(label+' failed')
 return p

def main():
 guards={str(path):sha(path) for path in (src, view, video, landscape, events, archive)}
 (D/'r1_guards.before.json').write_text(json.dumps(guards,indent=2))
 old=src.read_text();assert old.count('#include "EBasicApp.h"')==1
 old_tail='\tSDL_Quit();\n#ifndef OPENC\n\tCloseSTDLIB();\n\tExit();\n#endif\n}\n\nvoid CEBasicAppUi::symbian_at_exit()'
 assert old.count(old_tail)==1
 new=old.replace('#include "EBasicApp.h"','#include "EBasicApp.h"\n#include <basched.h>\n\n#ifdef S60V3\n#include <avkon.hrh>\nextern "C" int cpymo_s60_handle_selector_end_key();\nextern "C" int cpymo_s60_native_query_open();\nextern "C" int cpymo_s60_selector_mode();\n#endif')
 new_tail='\t// AppUi::Exit leaves with KLeaveExit; return it to the outer UI framework.\n\t// Swallowing this leave under OPENC leaves a cleaned-up but live process.\n\tif (exiterr == KLeaveExit)\n\t\tUser::Leave(exiterr);\n'+old_tail
 new=new.replace(old_tail,new_tail)
 key_handler='\tTInt type= aEvent.Type();\n#ifdef UIQ3\n\tCQikAppUi::HandleWsEventL(aEvent,aDestination);'
 assert new.count(key_handler)==1
 new=new.replace(key_handler, '''\tTInt type= aEvent.Type();
#ifdef S60V3
\t// Deliver native query input to Avkon. A softkey can close the dialog
\t// before its release reaches the app UI.
\tstatic TInt native_softkeys_down = 0;
\tif (type == EEventKeyDown || type == EEventKeyUp)
\t{
\t\tconst TInt scan = aEvent.Key()->iScanCode;
\t\tconst TInt bit = scan == EStdKeyDevice0 ? 1 : scan == EStdKeyDevice1 ? 2 : 0;
\t\tif (type == EEventKeyDown)
\t\t{
\t\t\tnative_softkeys_down &= ~bit;
\t\t\tif (cpymo_s60_native_query_open()) native_softkeys_down |= bit;
\t\t}
\t\telse if (native_softkeys_down & bit)
\t\t{
\t\t\tnative_softkeys_down &= ~bit;
\t\t\tif (!cpymo_s60_native_query_open()) return;
\t\t}
\t}
\tif (cpymo_s60_native_query_open())
\t{
\t\tCEikAppUi::HandleWsEventL(aEvent, aDestination);
\t\treturn;
\t}
\t// Handle both Avkon's close event and the raw red key before focus can move.
\tif (current_video &&
\t    (type == KAknUidValueEndKeyCloseEvent ||
\t     (type == EEventKeyDown && aEvent.Key()->iScanCode == EStdKeyNo)) &&
\t    cpymo_s60_handle_selector_end_key())
\t\treturn;
\t// The E63 prints 0 on M. Accept that scancode only in the selector.
\tif (current_video && (type == EEventKeyDown || type == EEventKeyUp))
\t{
\t\tconst TInt scan = aEvent.Key()->iScanCode;
\t\tconst TBool letter_zero = (scan == 'M' || scan == 'm') && cpymo_s60_selector_mode();
\t\tif (scan == '0' || scan == EStdKeyNkp0 || scan == EStdKeyDevice1 || letter_zero)
\t\t{
\t\t\tSDL_keysym key;
\t\t\tkey.scancode = scan;
\t\t\tkey.sym = scan == EStdKeyDevice1 ? SDLK_ESCAPE : letter_zero ? SDLK_m : SDLK_0;
\t\t\tkey.mod = KMOD_NONE;
\t\t\tkey.unicode = 0;
\t\t\tSDL_PrivateKeyboard(type == EEventKeyDown ? SDL_PRESSED : SDL_RELEASED, &key);
\t\t\treturn;
\t\t}
\t}
\t// On full keyboards, a Fn-modified key may report 0 only as a character.
\tif (current_video && type == EEventKey && aEvent.Key()->iCode == '0' &&
\t\t!aEvent.Key()->iRepeats && aEvent.Key()->iScanCode != '0' &&
\t\taEvent.Key()->iScanCode != EStdKeyNkp0)
\t{
\t\tSDL_Event key;
\t\tSDL_memset(&key, 0, sizeof(key));
\t\tkey.key.type = SDL_KEYDOWN;
\t\tkey.key.state = SDL_PRESSED;
\t\tkey.key.keysym.sym = SDLK_0;
\t\tSDL_PushEvent(&key);
\t\treturn;
\t}
#endif
#ifdef UIQ3
\tCQikAppUi::HandleWsEventL(aEvent,aDestination);''')
 (D/'EBasicApp.cpp').write_text(new)
 (D/'sdl-propagate-normal-exit-leave.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/Symbian/EBasicApp.cpp',tofile='b/Symbian/EBasicApp.cpp')))
 (D/'SDLSXXView.cpp').write_text(view.read_text())
 run('patch-startup-view',['patch','--batch',D/'SDLSXXView.cpp',D/'sdl-startup-full-window.patch'])
 view_source=(D/'SDLSXXView.cpp').read_text()
 for call in ('DrawScreenStatus(gc);','DrawScreenStatus(*iDsa->Gc());'):
  assert view_source.count(call)==1
  line=next(line for line in view_source.splitlines() if line.strip()==call)
  view_source=view_source.replace(line,'#ifndef S60V3\n'+line+'\n#endif')
 (D/'SDLSXXView.cpp').write_text(view_source)
 (D/video.name).write_text(video.read_text())
 (D/landscape.name).write_text(landscape.read_text())
 run('patch-landscape-redraw',['patch','--batch',D/landscape.name,D/'sdl-full-landscape-redraw.patch'])
 old_events=events.read_text(encoding='latin-1')
 new_events=old_events.replace('CWaitForActivate* gWaitForActive = NULL;', '''CWaitForActivate* gWaitForActive = NULL;

#ifdef S60V3
extern "C" int cpymo_s60_exit_is_requested();
#endif

static bool KeepWaitingForForeground()
{
#ifdef S60V3
    if (cpymo_s60_exit_is_requested())
        return false;
#endif
    return !gViewVisible;
}''')
 wait_body='\tif(gViewVisible)\n\t{\n        RedrawWindowL(current_video);'
 assert new_events.count(wait_body)==1
 new_events=new_events.replace(wait_body,
     '\tif(!KeepWaitingForForeground())\n\t{\n        if (gViewVisible) RedrawWindowL(current_video);')
 idle_body='\tif(!gViewVisible)\n\t{\n\t\tdelete gWaitForActive;'
 assert new_events.count(idle_body)==1
 new_events=new_events.replace(idle_body,
     '\tif(KeepWaitingForForeground())\n\t{\n\t\tdelete gWaitForActive;')
 (D/events.name).write_text(new_events,encoding='latin-1')
 (D/'sdl-background-exit.patch').write_text(''.join(difflib.unified_diff(
     old_events.splitlines(True),new_events.splitlines(True),
     fromfile='a/src/video/symbian/'+events.name,tofile='b/src/video/symbian/'+events.name)))
 build=SDK/('build'+str(SDL/'Symbian/s60v3/ESDL_S60V3/gcce'))
 makefile=build/'ESDL_S60V3.gcce'
 targets=['EBasicApp.o','SDLSXXView.o','SDL_epocvideo.o','SDL_epocevents.o']
 for unit, member in zip((src, view, video, events), targets):
  recovered=run('recover-compile-'+unit.stem,
      ['make','-n','-B','-f',makefile,build/'urel'/member]).stdout
  lines=[s for s in recovered.splitlines() if 'g++' in s and str(unit) in s]
  assert len(lines)==1
  cmd=shlex.split(lines[0]);cmd[0]=str(GCCE/'arm-none-symbianelf-g++')
  cmd[cmd.index('-o')+1]=str(D/member);cmd[-1]=str(D/unit.name)
  if unit in (video, events):cmd.insert(-1,'-I'+str(unit.parent))
  assert '-DOPENC' in cmd and '-DS60V3' in cmd
  run('compile-'+unit.stem,cmd)
 ar=GCCE/'arm-none-symbianelf-ar'
 members=run('archive-members',[ar,'t',archive]).stdout.splitlines()
 assert all(members.count(member)==1 for member in targets)
 def member_hashes(p):return {n:hashlib.sha256(subprocess.run([str(ar),'p',str(p),n],capture_output=True,check=True).stdout).hexdigest() for n in members}
 before=member_hashes(archive);shutil.copy2(archive,patched_archive)
 run('replace-members',[ar,'rs',patched_archive,*[D/member for member in targets]])
 after=member_hashes(patched_archive)
 assert all(before[member]!=after[member] for member in targets)
 assert all(before[n]==after[n] for n in members if n not in targets)
 run('EBasicApp-disassembly',[GCCE/'arm-none-symbianelf-objdump','-drC',D/'EBasicApp.o'])
 unchanged={str(path):sha(path) for path in (src, view, video, landscape, events, archive)};assert guards==unchanged
 (D/'r1_guards.after.json').write_text(json.dumps(unchanged,indent=2))
 report={'scope':'ARM compilation and private archive audit; guest lifecycle is a separate check.',
   'r1_inputs_unchanged':guards==unchanged,'member_count':len(members),'changed_members':targets,
   'members_before':before,'members_after':after,
   'artifacts':[{'path':str(p),'bytes':p.stat().st_size,'sha256':sha(p)}
                for p in [*[D/member for member in targets],patched_archive]]}
 (D/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k not in ['members_before','members_after']},indent=2))
if __name__=='__main__':main()
