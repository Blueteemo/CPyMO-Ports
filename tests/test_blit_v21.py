#!/usr/bin/env python3
"""Compare old/new ARGB->RGB565 output pixel-for-pixel; benchmark representative scale.
Host CPU timing is NOT an RG35XX FPS prediction.
"""
import ctypes, subprocess, tempfile, pathlib, random, time
P=pathlib.Path(__file__).resolve().parents[1]
NEW=P/'source/CPyMO-main/cpymo-backends/garlicos/garlic_adapter.c'
OLD=P/'tests/reference_garlic_adapter_v020.c'
with tempfile.TemporaryDirectory() as temp:
  libs=[]
  for i, src in enumerate((OLD,NEW)):
    so=pathlib.Path(temp)/f'test{i}.so'
    subprocess.run(['gcc','-O2','-shared','-fPIC','-I',str(NEW.parent),str(src),'-o',str(so)],check=True)
    lib=ctypes.CDLL(str(so));fn=lib.garlic_blit_rgb565
    fn.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int,ctypes.c_int,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int,ctypes.c_int]
    fn.restype=ctypes.c_bool
    libs.append(fn)
  rnd=random.Random(2333)
  shapes=[(800,600,640,480),(320,240,640,480),(540,360,640,480),(64,200,640,480),(200,64,640,480),
    (2,2,4,4),(37,93,188,218),(1,1,3,3),(100,80,40,30),(100,111,640,480)]
  shapes += [(rnd.randrange(1,90),rnd.randrange(1,90),rnd.randrange(1,220),rnd.randrange(1,150)) for _ in range(130)]
  for sw,sh,dw,dh in shapes:
    src=bytes(rnd.getrandbits(8) for _ in range(sw*sh*4)); sb=ctypes.create_string_buffer(src)
    results=[]
    for fn in libs:
      dst=ctypes.create_string_buffer(dw*dh*2+16)
      dst.raw=b'\x5a'*(dw*dh*2+16)
      accepted=fn(dst,dw*2,dw,dh,sb,sw*4,sw,sh)
      results.append((accepted,dst.raw))
    assert results[0]==results[1],f'pixel difference: {sw,sh,dw,dh}'
  print('PASS: 140 scale cases exact v0.20 pixel parity, destination canaries intact')
  sw,sh,dw,dh=800,600,640,480
  src=ctypes.create_string_buffer(bytes([48,90,167,255])*sw*sh)
  durations=[]
  for fn in libs:
    dst=ctypes.create_string_buffer(dw*dh*2)
    t0=time.perf_counter()
    for _ in range(40):
      assert fn(dst,dw*2,dw,dh,src,sw*4,sw,sh)
    durations.append(time.perf_counter()-t0)
  print(f'HOST_ONLY 800x600 -> 640x480; 40 iterations: old={durations[0]:.3f}s new={durations[1]:.3f}s ratio={durations[0]/durations[1]:.2f}x')
