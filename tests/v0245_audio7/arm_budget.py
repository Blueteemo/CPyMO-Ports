from pathlib import Path
import os,subprocess,json,re,statistics
b=Path.cwd();src=b/'repo/source/CPyMO-main';sdk=Path(os.environ['SDK']);sysroot=sdk/'arm-miyoo-linux-uclibcgnueabi/sysroot';gcclib=sdk/'lib/gcc/arm-miyoo-linux-uclibcgnueabi/9.4.0';d=b/'audio7-arm-budget';d.mkdir(exist_ok=True)
flags=['--target=armv7-unknown-linux-gnueabi','--sysroot='+str(sysroot),'-mcpu=cortex-a9','-mfloat-abi=softfp','-mfpu=vfpv3','-O2','-std=gnu11','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG','-ffunction-sections','-fdata-sections']
for label,source in [('before',Path(os.environ['CPYMO_QA_PREVIOUS_SOURCE'])),('after',src/'cpymo-backends/libretro/cpymo_audio_mp3.c')]:
 objs=[]
 for i,p in enumerate([b/'repo/tests/v0245/signal_probe.c',src/'cpymo/cpymo_str.c',src/'cpymo/cpymo_package.c',src/'cpymo-backends/libretro/stb_vorbis.c']):
  o=d/f'{label}-{i}.o';subprocess.run(['clang',*flags,'-DAUDIO_SOURCE="'+str(source)+'"','-c',str(p),'-o',str(o)],check=True);objs.append(str(o))
 subprocess.run(['clang',*flags,'-fuse-ld=lld','-nostdlib','-no-pie','-Wl,-dynamic-linker=/lib/ld-uClibc.so.1','-Wl,--gc-sections',str(sysroot/'usr/lib/crt1.o'),str(sysroot/'usr/lib/crti.o'),*objs,str(sysroot/'usr/lib/crtn.o'),'-L'+str(sysroot/'usr/lib'),'-L'+str(sysroot/'lib'),'-L'+str(gcclib),'-lm','-lc','-lgcc','-o',str(d/label)],check=True)
rows=[];wav=b/'audio7-quality/44100_12000_0.6.wav'
for repeat in range(3):
 for label in (['before','after'] if repeat%2==0 else ['after','before']):
  for channels,vol in [(1,1),(3,.2),(3,1)]:
   p=subprocess.run(['qemu-arm','-L',str(sysroot),str(d/label),str(wav),'/dev/null','480000','1',*([str(wav)]*channels)],env={**os.environ,'PROBE_VOLUME':str(vol),'PROBE_LOOP':'1'},capture_output=True,text=True,check=True)
   row={'variant':label,'repeat':repeat,'channels':channels,'volume':vol,'cpu_seconds':float(re.search(r'CPU_SECONDS ([\d.]+)',p.stdout)[1])};rows.append(row);print(row,flush=True)
ratios=[]
for channels,vol in [(1,1),(3,.2),(3,1)]:
 med={v:statistics.median(r['cpu_seconds'] for r in rows if r['variant']==v and r['channels']==channels and r['volume']==vol)for v in ['before','after']};ratios.append({'channels':channels,'volume':vol,'candidate_over_baseline':med['after']/med['before'],'median':med})
 assert ratios[-1]['candidate_over_baseline']<1.15,ratios[-1]
(d/'summary.json').write_text(json.dumps({'scope':'matched ARM/uClibc/QEMU relative CPU; not real hardware timing','results':rows,'ratios':ratios},indent=2));print('ARM BUDGET PASS',ratios,flush=True)
