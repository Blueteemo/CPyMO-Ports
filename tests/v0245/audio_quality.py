"""Measured production resampling of standard band-limited signals.
Analytic sine reference; no claim about the device DAC or audibility.
"""
from pathlib import Path
import os, sys, json, subprocess, wave, math, re
import numpy as np
import resource,shlex
resource.setrlimit(resource.RLIMIT_CORE,(0,0))

b=Path.cwd(); repo=b/'repo'; src=repo/'source/CPyMO-main'
baseline=Path(os.environ['CPYMO_QA_BASELINE'])
dest=b/('audio-quality-'+sys.argv[1]);dest.mkdir()
flags=['-O2','-std=gnu11','-DDISABLE_MOVIE','-DDISABLE_FFMPEG_AUDIO','-DGARLIC_NO_FFMPEG',
       '-ffunction-sections','-fdata-sections']
for label,source in [('v244',baseline/'source/CPyMO-main/cpymo-backends/libretro/cpymo_audio_mp3.c'),
                     ('v245',src/'cpymo-backends/libretro/cpymo_audio_mp3.c')]:
    extra=shlex.split(os.environ.get('CPYMO_QA_AUDIO_CFLAGS','')) if label=='v245' else []
    cmd=['gcc',*flags,*extra,'-DAUDIO_SOURCE="'+str(source)+'"',str(repo/'tests/v0245/signal_probe.c'),
         str(src/'cpymo/cpymo_str.c'),str(src/'cpymo/cpymo_package.c'),
         str(src/'cpymo-backends/libretro/stb_vorbis.c'),'-Wl,--gc-sections','-lm','-o',str(dest/label)]
    subprocess.run(cmd,check=True)
rows=[]
for rate,tones in [(8000,[1000,3000]),(16000,[1000,6000]),(22050,[1000,9000]),(24000,[1000,9000]),(32000,[1000,12000]),(88200,[1000,30000]),(44100,[1000,6000,12000,18000,20000]),
                   (48000,[1000,12000,20000]),(96000,[10000,30000,40000])]:
    for freq in tones:
        channels=1 if freq==1000 else 2
        n=rate*2
        samples=np.empty((n,channels),np.int16)
        t=np.arange(n)/rate
        samples[:,0]=np.rint(19660*np.sin(2*np.pi*freq*t))
        if channels==2:samples[:,1]=np.rint(13762*np.cos(2*np.pi*freq*t))
        wav=dest/f'{rate}_{freq}_{channels}.wav'
        with wave.open(str(wav),'wb') as f:
            f.setnchannels(channels);f.setsampwidth(2);f.setframerate(rate);f.writeframes(samples.astype('<i2').tobytes())
        for label in ['v244','v245']:
            output=dest/(label+'.raw')
            run=subprocess.run([str(dest/label),str(wav),str(output),'96000','0'],text=True,capture_output=True)
            if run.returncode:
                rows.append({'core':label,'rate':rate,'frequency':freq,'channels':channels,'exit':run.returncode,'failed':True})
                if output.exists():output.unlink()
                continue
            data=np.fromfile(output,dtype='<i4').reshape(-1,2).astype(float)
            cpu=float(re.search(r'CPU_SECONDS ([\d.]+)',run.stdout)[1])
            step=int(re.search(r'step=(\d+)',run.stdout)[1])
            # Fit a sinusoid at the actual Q16 source-clock frequency. This
            # separates passband gain from nonlinear/interpolation residual.
            indices=np.arange(4800,90000)
            w=2*np.pi*freq*indices*step/(65536*rate)
            design=np.column_stack([np.sin(w),np.cos(w),np.ones(len(w))])
            coef=np.linalg.lstsq(design,data[indices,0],rcond=None)[0]
            fitted=design@coef; residual=data[indices,0]-fitted
            boundary=(((indices*step)&65535)>>7)==511
            rms=lambda a:float(np.sqrt(np.mean(a*a))) if len(a) else 0.0
            amp=float(np.hypot(coef[0],coef[1]));noise=rms(residual)
            rows.append({'core':label,'rate':rate,'frequency':freq,'channels':channels,
                         'gain_db':20*math.log10(max(amp,1e-12)/19660),
                         'residual_snr_db':20*math.log10(max(rms(fitted),1e-12)/max(noise,1e-12)),
                         'phase511_samples':int(boundary.sum()),'phase511_error_rms_lsb':rms(residual[boundary]),
                         'other_error_rms_lsb':rms(residual[~boundary]),
                         'cpu_seconds':cpu,'peak_pre_limiter':float(np.max(np.abs(data))),
                         'mono_channels_equal':bool(np.array_equal(data[:,0],data[:,1])) if channels==1 else None,
                         'step_q16':step})
            output.unlink()  # bounded generated intermediate, not a user file
print(json.dumps(rows,indent=2))
(dest/'results.json').write_text(json.dumps({'reference':'analytic sinusoid with actual fixed-point source clock; gain and residual separate',
                                          'results':rows},indent=2)+'\n')

assert not any(r.get('failed') for r in rows if r['core']=='v245')
assert all(r['mono_channels_equal'] for r in rows if r['core']=='v245' and r['channels']==1)
