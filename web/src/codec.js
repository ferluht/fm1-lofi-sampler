/* lofi codec in JavaScript: the 2 bit LMS-ADPCM of core/lofi_codec.h, bit exact with the C encoder and decoder (verified by web/test/test_codec.js).
 * 16 kHz mono int16 in, 192 bytes per 640 samples out (see docs/CODEC.md). Plain numbers are exact for every intermediate value (< 2^53). */
(function (LF) {
'use strict';
const GROUP = 20, SUPER = 640, SUPER_BYTES = 192;
const DQT = new Int16Array([1, -1, 3, -3, 2, -2, 5, -5, 3, -3, 9, -9, 5, -5, 16, -16, 9, -9, 28, -28, 16, -16, 49, -49, 29, -29, 86, -86, 50, -50, 151, -151, 88, -88, 264, -264, 154, -154, 462, -462, 269, -269, 808, -808, 471, -471, 1414, -1414, 825, -825, 2475, -2475, 1444, -1444, 4331, -4331, 2527, -2527, 7580, -7580, 4422, -4422, 13265, -13265]);
const sat16 = x => (x > 32767 ? 32767 : x < -32768 ? -32768 : x);
function predict(s) { return Math.floor((s[0] * s[4] + s[1] * s[5] + s[2] * s[6] + s[3] * s[7]) / 8192); }       // >> 13 of the int64 sum
function update(s, pcm, res) {
  const d = res >> 4;
  for (let i = 0; i < 4; i++) s[4 + i] = sat16(s[4 + i] + (s[i] < 0 ? -d : d));
  s[0] = s[1]; s[1] = s[2]; s[2] = s[3]; s[3] = pcm;
}
function penalty(s) { const v = Math.floor((s[4] * s[4] + s[5] * s[5] + s[6] * s[6] + s[7] * s[7]) / 262144) - 0x8ff; return v > 0 ? v * v : 0; }
const encodedBytes = frames => Math.ceil(frames / SUPER) * SUPER_BYTES;

/** Encode `frames` samples of an Int16Array. Returns { bytes: Uint8Array, recon: Int16Array } (recon = exactly what the FM-1 will play).
 *  `progress(fraction)` is called per superblock when given; `yieldFn` (async) lets a UI breathe: use encodeAsync. */
function* encodeSteps(input, frames) {
  const nsb = Math.ceil(frames / SUPER), out = new Uint8Array(nsb * SUPER_BYTES), recon = new Int16Array(frames);
  const s = new Int32Array([0, 0, 0, 0, 0, 0, -8192, 16384]), t = new Int32Array(8), bs = new Int32Array(8);
  const cc = new Uint8Array(GROUP), oo = new Int16Array(GROUP), bc = new Uint8Array(GROUP), bo = new Int16Array(GROUP);
  let prev = 0;
  for (let b = 0; b < nsb; b++) {
    const sb = b * SUPER_BYTES;
    for (let i = 0; i < 8; i++) { out[sb + 2 * i] = s[i] & 255; out[sb + 2 * i + 1] = (s[i] >> 8) & 255; }
    for (let g = 0; g < 32; g++) {
      const chunk = sb + 16 + (g >> 4) * 88, gi = g & 15;
      let best = Infinity, bsf = prev;
      for (let k = 0; k < 16; k++) {
        const sf = (k + prev) & 15; t.set(s); let rank = 0, ok = true;
        for (let i = 0; i < GROUP; i++) {
          const p = b * SUPER + g * GROUP + i, pcm = p < frames ? input[p] : 0, pr = predict(t);
          let bq = 0, be = Infinity;
          for (let q = 0; q < 4; q++) { const e = Math.abs(pcm - sat16(pr + DQT[sf * 4 + q])); if (e < be) { be = e; bq = q; } }
          const res = DQT[sf * 4 + bq], rec = sat16(pr + res), er = pcm - rec;
          rank += er * er + penalty(t);
          if (rank > best) { ok = false; break; }
          cc[i] = bq; oo[i] = rec; update(t, rec, res);
        }
        if (ok && rank < best) { best = rank; bs.set(t); bc.set(cc); bo.set(oo); bsf = sf; }
      }
      prev = bsf; s.set(bs);
      out[chunk + 80 + (gi >> 1)] |= bsf << ((gi & 1) ? 0 : 4);
      for (let i = 0; i < GROUP; i++) {
        out[chunk + gi * 5 + (i >> 2)] |= bc[i] << (6 - 2 * (i & 3));
        const p = b * SUPER + g * GROUP + i; if (p < frames) recon[p] = bo[i];
      }
    }
    yield (b + 1) / nsb;
  }
  return { bytes: out, recon };
}
function encode(input, frames) { const it = encodeSteps(input, frames == null ? input.length : frames); let r; while (!(r = it.next()).done); return r.value; }
/** async version for the browser: yields to the event loop about every 40 ms */
async function encodeAsync(input, progress) {
  const it = encodeSteps(input, input.length); let r, last = Date.now();
  while (!(r = it.next()).done) { if (Date.now() - last > 40) { if (progress) progress(r.value); await new Promise(res => setTimeout(res, 0)); last = Date.now(); } }
  return r.value;
}
/** decode one superblock (192 bytes at `off`) to 640 samples */
function decodeSuper(data, off, out, outOff) {
  const s = new Int32Array(8);
  for (let i = 0; i < 8; i++) { const v = data[off + 2 * i] | (data[off + 2 * i + 1] << 8); s[i] = v >= 32768 ? v - 65536 : v; }
  for (let g = 0; g < 32; g++) {
    const chunk = off + 16 + (g >> 4) * 88, gi = g & 15, scale = (data[chunk + 80 + (gi >> 1)] >> ((gi & 1) ? 0 : 4)) & 15;
    for (let i = 0; i < GROUP; i++) {
      const code = (data[chunk + gi * 5 + (i >> 2)] >> (6 - 2 * (i & 3))) & 3, res = DQT[scale * 4 + code], v = sat16(predict(s) + res);
      update(s, v, res); out[outOff + g * GROUP + i] = v;
    }
  }
}
function decode(data, off, frames) {
  const nsb = Math.ceil(frames / SUPER), tmp = new Int16Array(nsb * SUPER);
  for (let b = 0; b < nsb; b++) decodeSuper(data, off + b * SUPER_BYTES, tmp, b * SUPER);
  return tmp.subarray(0, frames).slice();
}
LF.codec = { GROUP, SUPER, SUPER_BYTES, DQT, encode, encodeSteps, encodeAsync, decode, decodeSuper, encodedBytes };
})(typeof window !== 'undefined' ? (window.LF = window.LF || {}) : (module.exports = (global.LF = global.LF || {})));
