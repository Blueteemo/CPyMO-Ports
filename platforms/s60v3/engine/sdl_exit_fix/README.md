# CPyMO S60v3 — propagate the native application-exit leave

## Actual failure evidence

The parent-controlled final2 run proved RFs stages 1–4 complete, hence SDL_main returns and both stdout fflush calls return. C exit failed to terminate the target process. The following final3 run used the native AppUi HandleCommandL(EEikCmdExit) path; stages 1–5 completed, while no stage6 returned. Kernel:trace showed the native `KLeaveExit` value -1003 started and was trapped, but no CPyMO main-thread termination. This is a swallowed native exit signal, not an unresolved guess about fflush or a forced host stop.

The retained FP1 SDK basched.h defines `const TInt KLeaveExit=(-1003)`. The actual SDL EBasicApp.cpp SDLStartL invokes application LaunchAppL inside `TRAPD(exiterr, ...)`, restores any platform-specific filesystem sharing, calls SDL_Quit and, under OPENC, excludes its final Exit(). Thus AppUi::Exit's intentional leave is caught and silently discarded. The process then remains with its CPyMO resources already cleaned up.

The SDL_Quit second-call hypothesis is no longer required to explain the failure: final3's -1003 leave is precisely the native framework exit signal. Static SDL code also guards subsystem cleanup with SDL_initialized bits; the actual archive's SDL_UninstallParachute is a single bx lr on this NO_SIGNAL_H build.

## Minimal fix

Add basched.h and, immediately after TRAPD/filesystem sharing restoration and before redundant SDL_Quit, propagate only the standard exit leave:

```cpp
if (exiterr == KLeaveExit)
    User::Leave(exiterr);
```

The surrounding normal/other-error paths are retained. The native UI framework outside the SDL callback must receive its usual exit signal. This is `User::Leave`, an exception/control-flow mechanism; it is not `User::Exit`, process kill, or a replacement for the user's actual exit command.

Keep the parent's native AppUi launcher path. CPyMO engine, game/audio, font and SDL cleanup has completed before that path. No direct C exit is needed. The parent thread must install the newly linked SISX and verify the UI-triggered main process ending and restart/real UI readback.

## Actual build

`build_sdl_exit_fix.py` reads the original retained EBasicApp.cpp and ESDL.lib, creates an isolated patched source, expands the original SDK make compile recipe in dry-run mode, and compiles the new EBasicApp.o with the same GCCE 3.4.3/FP1/Open C flags. The new source is found through the original read-only SDL include directories. It copies ESDL.lib to ESDL_cpymo.lib, replacing exactly EBasicApp.o.

- ARM compilation succeeded; `compile-EBasicApp.log` is empty (no warnings/errors).
- 54 archive members; the other 53 member hashes are unchanged.
- Original R1 source and original ESDL.lib hashes match before/after guards.
- EBasicApp.o: 43,668 bytes; SHA-256 489a4bc5726490b2170d0cb6d268ffc1fa7fb2a24fc3c715451580fa967962e7.
- ESDL_cpymo.lib: 576,374 bytes; SHA-256 f403199e843cc2620675391ba5550fc3198af19f5c1b902be11acb365b6247f9.
- `SDLStartL-disassembly-excerpt.txt` shows comparison with -1003 and the actual User::Leave relocation.

## Integration

Copy only the newly named ESDL_cpymo.lib into the SDK release/armv5/urel search directory and change the candidate MMP STATICLIBRARY from ESDL.lib to ESDL_cpymo.lib. Keep original ESDL.lib untouched. Force ELF/E32 relinking so the tested package actually contains this object. The parent owns this final link/install step; this subtask's result.json certifies build/archive integrity only.

From the existing recovered environment:

```sh
source /workspace/scratch/3dcdfea37dce/research/s60_trial/toolchain/activate.sh
python3 /workspace/scratch/4acb6c03f8f3/r3/sdl_exit_fix/build_sdl_exit_fix.py
```

For another workspace, first recover the pinned SDL source, FP1 SDK, GCCE and generated SDK makefile according to the R1/R3 instructions, then set CPYMO_R1_ROOT. This script does not recover a full SDK by itself. No key, SDK or firmware is included here. Preserve the Symbian SDL/SDL license and attribution when distributing the private linked archive and source patch.

## 0.1.13 后续 SDL 修改

当前构建还从原 SDL 树重建冷启动窗口和横屏刷新补丁，并在 Window Server 事件入口将数字 0 和小键盘 0 保留为 SDL 的数字键。已由全键盘产生的字符 0 会单独交给 CPyMO，常规左软键事件沿用原映射。当前版本的更改记录在 `sdl-propagate-normal-exit-leave.patch`，构建哈希见交付包 `evidence/SDL_BUILD_RESULT.json`。
