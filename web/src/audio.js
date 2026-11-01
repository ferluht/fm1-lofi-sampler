/* Audio import for the browser: any file the browser can decode -> 16 kHz mono int16 (low-passed at 7.2 kHz), slicing helpers, preview playback.
 * The onset detector is the one of the Android app (Java AudioImport.autoSlice). */
(function (LF) {
'use strict';
const RATE = 16000;
let actx = null;
const ctx = () => actx || (actx = new (window.AudioContext || window.webkitAudioContext)());
async function importFile(file) {
  const data = await file.arrayBuffer();
  const buf = await new Promise((res, rej) => ctx().decodeAudioData(data, res, () => rej(new Error('the browser could not decode ' + file.name))));
  const frames = Math.max(1, Math.round(buf.duration * RATE));
  const off = new OfflineAudioContext(1, frames, RATE);
  const src = off.createBufferSource(); src.buffer = buf;
  const mono = off.createGain(); mono.channelCount = 1; mono.channelCountMode = 'explicit'; mono.channelInterpretation = 'speakers';   // stereo/surround mixed down to mono
  const lp = off.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 7200; lp.Q.value = 0.5;
  src.connect(mono); mono.connect(lp); lp.connect(off.destination); src.start();
  const out = (await off.startRendering()).getChannelData(0), pcm = new Int16Array(out.length);
  for (let i = 0; i < out.length; i++) { const v = Math.round(out[i] * 32767); pcm[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
  return pcm;
}
/** onset detection: 4 ms RMS envelope, a hit starts when the level jumps over the quiet floor. sensitivity 0..1, minGapMs. Returns sample indexes. */
function autoSlice(x, sensitivity, minGapMs) {
  const win = RATE * 4 / 1000 | 0, n = Math.floor(x.length / win); if (n < 4) return [];
  const env = new Float64Array(n); for (let i = 0; i < n; i++) { let s = 0; for (let k = 0; k < win; k++) { const v = x[i * win + k]; s += v * v; } env[i] = Math.sqrt(s / win); }
  let max = 1; for (const e of env) max = Math.max(max, e);
  const ratio = 6 - 4.5 * sensitivity, floorLevel = max * (0.004 + 0.03 * (1 - sensitivity)), gap = minGapMs / 4 | 0, cuts = []; let last = -gap - 1, slow = env[0];
  for (let i = 1; i < n; i++) { if (env[i] > floorLevel && env[i] > ratio * Math.max(slow, 1) && i - last > gap) { cuts.push(Math.max(0, i - 1) * win); last = i; } slow = slow * 0.9 + env[i] * 0.1; }
  return cuts;
}
/** pitch of a sustained sample (YIN on several windows, median). Returns {midi (float), conf} or null. 16 kHz: 55..1000 Hz */
function detectPitch(x) {
  const W = 1024, TMIN = 16, TMAX = 290, step = 512; if (x.length < W + TMAX + 64) return null;
  let max = 0; for (let i = 0; i < x.length; i++) max = Math.max(max, Math.abs(x[i])); if (max < 200) return null;
  const res = [];
  // windows from 8% of the sample on, only where the level is within 50% of the loudest window
  const rmsAt = o => { let s = 0; for (let i = 0; i < W; i++) s += x[o + i] * x[o + i]; return Math.sqrt(s / W); };
  const starts = []; for (let o = Math.floor(x.length * 0.05); o + W + TMAX < x.length; o += step) starts.push(o);
  let top = 0; const lv = starts.map(o => { const r = rmsAt(o); top = Math.max(top, r); return r; });
  for (let k = 0; k < starts.length; k++) {
    if (lv[k] < top * 0.5) continue; const o = starts[k], d = new Float64Array(TMAX + 1);
    for (let t = 1; t <= TMAX; t++) { let s = 0; for (let i = 0; i < W; i++) { const df = x[o + i] - x[o + i + t]; s += df * df; } d[t] = s; }
    let run = 0; const cm = new Float64Array(TMAX + 1); for (let t = 1; t <= TMAX; t++) { run += d[t]; cm[t] = d[t] * t / (run || 1); }
    let tau = -1; for (let t = TMIN; t < TMAX; t++) { if (cm[t] < 0.15) { while (t + 1 < TMAX && cm[t + 1] < cm[t]) t++; tau = t; break; } }
    if (tau < 0) continue;
    const a = cm[tau - 1], b = cm[tau], c = cm[tau + 1], den = a - 2 * b + c, off = den ? 0.5 * (a - c) / den : 0;     // parabolic interpolation
    res.push({ f: RATE / (tau + off), c: 1 - cm[tau] });
  }
  if (res.length < 2) return null;
  res.sort((p, q) => p.f - q.f); const m = res[res.length >> 1], good = res.filter(r => Math.abs(12 * Math.log2(r.f / m.f)) < 0.5).length;
  return { midi: 69 + 12 * Math.log2(m.f / 440), hz: m.f, conf: good / res.length };
}
/** root pitch class + fine tune for the instrument: tune = the correction in cents that brings the sample onto the semitone */
function rootAndTune(x) {
  const p = detectPitch(x); if (!p || p.conf < 0.5) return null; const n = Math.round(p.midi);
  return { root: ((n % 12) + 12) % 12, tune: Math.max(-50, Math.min(50, Math.round((n - p.midi) * 100))), hz: p.hz, midi: p.midi };
}
/** drop the silence after the last audible sample (keeps 12 ms) */
function trimTail(x) { let last = 0; for (let i = x.length - 1; i >= 0; i--) if (Math.abs(x[i]) > 60) { last = i; break; } const end = Math.min(x.length, last + 192); return end < x.length ? x.slice(0, end) : x; }
let playing = null;
function stop() { if (playing) { try { playing.stop(); } catch (e) { /* done */ } playing = null; } }
/** play 16 kHz int16 samples; `octaves` changes the speed (the pitch) like the live speed of the FM-1 */
function play(pcm, octaves = 0) {
  stop(); if (!pcm.length) return; const c = ctx(); if (c.state === 'suspended') c.resume();
  const b = c.createBuffer(1, pcm.length, RATE), d = b.getChannelData(0); for (let i = 0; i < pcm.length; i++) d[i] = pcm[i] / 32768;
  const s = c.createBufferSource(); s.buffer = b; s.playbackRate.value = Math.pow(2, octaves); s.connect(c.destination); s.start(); playing = s; s.onended = () => { if (playing === s) playing = null; };
}
LF.audio = { RATE, importFile, autoSlice, trimTail, detectPitch, rootAndTune, play, stop };
})(typeof window !== 'undefined' ? (window.LF = window.LF || {}) : (module.exports = (global.LF = global.LF || {})));
