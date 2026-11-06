"""Matched ARM/uClibc microbench of measured resampler alternatives."""
from pathlib import Path
import json,os,subprocess
b=Path.cwd();sdk=Path(os.environ['SDK']);sysroot=sdk/'arm-miyoo-linux-uclibcgnueabi/sysroot'
gcclib=sdk/'lib/gcc/arm-miyoo-linux-uclibcgnueabi/9.4.0'
src=b/'repo/source/CPyMO-main'
sources=[b/'repo/tests/v0245/signal_probe.c',src/'cpymo/cpymo_str.c',src/'cpymo/cpymo_package.c',src/'cpymo-backends/libretro/stb_vorbis.c']
common=['--target=armv7-unknown-linux-gnueabi','--sysroot='+str(sysroot),'-mcpu=cortex-a9','-mfloat-abi=softfp','-mfpu=vfpv3','-O2','-std=gnu11','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-ffunction-sections','-fdata-sections']
rows=[]
for taps in [16,32]:
    d=b/f'audio-budget-{taps}';d.mkdir(exist_ok=True);objs=[]
    for i,s in enumerate(sources):
        o=d/f'{i}.o';subprocess.run(['clang',*common,f'-DGARLIC_AUDIO_RES_TAPS={taps}','-c',str(s),'-o',str(o)],check=True);objs.append(str(o))
    exe=d/'probe'
    subprocess.run(['clang',*common,'-fuse-ld=lld','-nostdlib','-no-pie','-Wl,-dynamic-linker=/lib/ld-uClibc.so.1','-Wl,--gc-sections',str(sysroot/'usr/lib/crt1.o'),str(sysroot/'usr/lib/crti.o'),*objs,str(sysroot/'usr/lib/crtn.o'),'-L'+str(sysroot/'usr/lib'),'-L'+str(sysroot/'lib'),'-L'+str(gcclib),'-lm','-lc','-lgcc','-o',str(exe)],check=True)
for repeat in range(3):
    for taps in ([16,32] if repeat%2==0 else [32,16]):
        d=b/f'audio-budget-{taps}'
        for channels,volume in [(1,1),(3,.2),(3,1)]:
            wav=b/'audio-quality-fixed16/44100_12000_2.wav'
            cmd=['qemu-arm','-L',str(sysroot),str(d/'probe'),str(wav),str(d/'out.raw'),'960000','1',*([str(wav)]*channels)]
            env={**os.environ,'PROBE_LOOP':'1','PROBE_VOLUME':str(volume)}
            p=subprocess.run(cmd,capture_output=True,text=True,env=env);assert p.returncode==0,p.stderr
            rows.append({'taps':taps,'channels':channels,'volume':volume,'repeat':repeat,'result':p.stdout.strip(),'command':cmd})
        (d/'out.raw').unlink()
(b/'audio-arm-budget-repeated.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(rows,indent=2))
