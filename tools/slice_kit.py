"""Slice a one-shot kit WAV into hits by onset: python slice_kit.py kit.wav outdir
Onset = envelope rises above -35 dB after >=60 ms below -60 dB (or at file start)."""
import sys, subprocess, numpy as np, wave
src, out = sys.argv[1], sys.argv[2]
sr = 44100
raw = subprocess.run(['ffmpeg','-v','error','-i',src,'-ac','1','-ar',str(sr),'-f','f32le','-'],capture_output=True).stdout
x = np.frombuffer(raw, dtype=np.float32)
w = 128
env = np.sqrt(np.convolve(x*x, np.ones(w)/w, 'same')); db = 20*np.log10(env+1e-9)
quiet = int(0.06*sr); starts = [0]; last_loud = 0
loud = db > -60
idx = np.where(loud)[0]
prev = idx[0]
for i in idx[1:]:
    if i - prev > quiet: starts.append(i)       # first loud sample after a gap
    prev = i
starts = sorted(set(starts))
pre = int(0.01*sr)
starts = [max(0, s-pre) if s else 0 for s in starts]
for n, s in enumerate(starts):
    e = starts[n+1] if n+1 < len(starts) else len(x)
    seg = x[s:e]; l = np.where(db[s:e] > -60)[0]
    seg = seg[:l[-1]+int(0.01*sr)] if len(l) else seg
    fade = min(len(seg), int(0.004*sr)); seg = seg.copy(); seg[-fade:] *= np.linspace(1, 0, fade)
    pcm = (np.clip(seg, -1, 1)*32767).astype('<i2')
    with wave.open(f'{out}/hit{n+1:02d}.wav', 'wb') as f:
        f.setnchannels(1); f.setsampwidth(2); f.setframerate(sr); f.writeframes(pcm.tobytes())
    print(f'hit{n+1:02d} start {s/sr:6.3f}s length {len(seg)/sr:5.3f}s')
