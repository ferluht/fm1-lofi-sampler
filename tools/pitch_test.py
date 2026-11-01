"""Render decoded 2-bit 16k raw files at -4..+1 octaves (Hermite resampler), concatenated, to wav.
usage: pitch_test.py in.raw out.wav"""
import sys, wave, numpy as np
x = np.fromfile(sys.argv[1], dtype='<i2').astype(np.float64)/32768
def hermite(x, r):
    n = int((len(x)-3)/r); t = np.arange(n)*r; i = t.astype(int); f = t-i
    y0, y1, y2, y3 = x[i], x[i+1], x[i+2], x[i+3]
    c0 = y1; c1 = .5*(y2-y0); c2 = y0-2.5*y1+2*y2-.5*y3; c3 = .5*(y3-y0)+1.5*(y1-y2)
    return ((c3*f+c2)*f+c1)*f+c0
x = np.concatenate([np.zeros(1), x, np.zeros(4)])
gap = np.zeros(int(0.35*16000)); out = []
for o in (0, 1, -1, -2, -3, -4):
    seg = x if len(x) < 16000*6 else x[:16000*6]
    out += [hermite(seg, 2.0**o), gap]
y = np.clip(np.concatenate(out), -1, 1)
with wave.open(sys.argv[2], 'wb') as f:
    f.setnchannels(1); f.setsampwidth(2); f.setframerate(16000); f.writeframes((y*32767).astype('<i2').tobytes())
