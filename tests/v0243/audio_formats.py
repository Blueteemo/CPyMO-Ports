from pathlib import Path
import wave,struct,subprocess,json
b=Path.cwd();p=b/'audiofixtures';p.mkdir(exist_ok=True)
for rate in [22050,44100,48000]:
 f=p/f'{rate}.wav'
 with wave.open(str(f),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(rate);w.writeframes(b'\x00\x00\x00\x00'*rate)
 # Add legal trailing RIFF chunk; it must never be decoded as PCM.
 data=bytearray(f.read_bytes());tail=b'JUNK'+struct.pack('<I',8000)+b'\xff'*8000;data.extend(tail);struct.pack_into('<I',data,4,len(data)-8);f.write_bytes(data)
for ext in ['mp3','ogg']:
 subprocess.run(['ffmpeg','-v','error','-y','-i',str(b/'fixtures/540x360/bgm/song.wav'),str(p/f'song.{ext}')],check=True)
