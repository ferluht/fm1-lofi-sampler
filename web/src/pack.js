/* Packs in JavaScript: the model (zones = banks of 27 keys), building the image that is uploaded (byte exact with core/lofi_pack.h) and reading an image back.
 * Documented in docs/PACK_FORMAT.md. Verified by web/test/test_pack.js (the DEMO pack image is identical to the C one). */
(function (LF) {
'use strict';
const KEYS = 27, MAX_BANKS = 8, MAX_SLOTS = 32, HEADER = 1152, CAPACITY = 127 * 4096;
const SCALES = ['OFF', 'CHROM', 'MIX', 'MAJ', 'MIN', 'HMIN', 'PHRYG', 'MIN+'], NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const F_LOOP = 1, F_REVERSE = 2, F_ONESHOT = 4;

class Slot {
  constructor(o = {}) {
    this.name = o.name || 'SAMPLE'; this.pcm = o.pcm || new Int16Array(0); this.lo = o.lo || 0; this.hi = o.hi || 0; this.root = o.root || 0;
    this.flags = o.flags == null ? F_ONESHOT : o.flags; this.loopStart = o.loopStart || 0; this.loopEnd = o.loopEnd == null ? -1 : o.loopEnd;
    this.voices = o.voices || 0; this.choke = o.choke || 0; this.glide = o.glide || 0; this.tune = o.tune || 0;   // tune: cents added to the playback pitch (-50..50)
  }
  get isKit() { return this.lo === this.hi; }
  get bank() { return this.lo >> 5; } get k0() { return this.lo & 31; } get k1() { return this.hi & 31; }
  setBankKeys(bank, k0, k1) { this.lo = bank * 32 + k0; this.hi = bank * 32 + k1; if (this.isKit) this.root = this.lo; }
  get bytes() { return LF.codec.encodedBytes(this.pcm.length); }
  get seconds() { return this.pcm.length / 16000; }
}
class Pack {
  constructor(o = {}) { this.id = o.id || Date.now().toString(36) + Math.random().toString(36).slice(2, 5); this.name = o.name || 'NEW PACK'; this.scale = o.scale == null ? 4 : o.scale; this.scaleRoot = o.scaleRoot || 0; this.slots = o.slots || []; }
  get imageBytes() { let n = HEADER; for (const s of this.slots) n += s.bytes; return n; }
  get freeBytes() { return CAPACITY - this.imageBytes; }
  get seconds() { return this.slots.reduce((a, s) => a + s.seconds, 0); }
  banks() { let n = 0; for (const s of this.slots) n = Math.max(n, s.bank + 1); return n; }
  bankKind(b) { let k = 0; for (const s of this.slots) if (s.bank === b) { if (!s.isKit) return 2; k = 1; } return k; }       // 0 empty, 1 kit, 2 melodic
  slotAt(b, k) { return this.slots.findIndex(s => s.bank === b && k >= s.k0 && k <= s.k1); }
  fits(s, b, k0, k1, self) {
    if (b < 0 || b >= MAX_BANKS || k0 < 0 || k1 >= KEYS || k1 < k0) return false; const melodic = k1 > k0;
    for (const o of this.slots) { if (o === self || o.bank !== b) continue; if (melodic || !o.isKit) return false; if (o.k0 === k0) return false; }
    return true;
  }
  placeKit(s) { for (let b = 0; b < MAX_BANKS; b++) { if (this.bankKind(b) === 2) continue; for (let k = 0; k < KEYS; k++) if (this.fits(s, b, k, k, null)) { s.setBankKeys(b, k, k); return true; } } return false; }
  placeMelodic(s) { for (let b = 0; b < MAX_BANKS; b++) if (this.bankKind(b) === 0) { s.setBankKeys(b, 0, KEYS - 1); s.root = 0; s.flags = 0; return true; } return false; }
  autoLayout() {
    let b = 0, k = 0;
    for (const s of this.slots) if (s.isKit) { if (k >= KEYS) { b++; k = 0; } s.setBankKeys(b, k, k); k++; }
    if (k > 0) b++;
    for (const s of this.slots) if (!s.isKit) { s.setBankKeys(Math.min(b, MAX_BANKS - 1), 0, KEYS - 1); b++; }
  }
}
const ascii = (u8, off, n, str) => { for (let i = 0; i < n; i++) u8[off + i] = i < str.length ? str.charCodeAt(i) & 0x7f : 0; };
const w32 = (u8, o, v) => { u8[o] = v & 255; u8[o + 1] = (v >>> 8) & 255; u8[o + 2] = (v >>> 16) & 255; u8[o + 3] = (v >>> 24) & 255; };
const r32 = (u8, o) => (u8[o] | (u8[o + 1] << 8) | (u8[o + 2] << 16) | (u8[o + 3] << 24)) >>> 0;
/** encode every sample (cached on the slot while its pcm does not change) */
async function encodeSlot(slot, progress) {
  if (slot._enc && slot._enc.pcm === slot.pcm) return slot._enc;
  const r = await LF.codec.encodeAsync(slot.pcm, progress); slot._enc = { pcm: slot.pcm, bytes: r.bytes, recon: r.recon }; return slot._enc;
}
/** the image that goes to the FM-1 (and into a .lfp file) */
async function buildImage(pack, progress = () => {}) {
  const n = pack.slots.length, enc = [];
  for (let i = 0; i < n; i++) enc.push(await encodeSlot(pack.slots[i], f => progress((i + f) / n, 'encoding ' + (i + 1) + '/' + n)));
  let total = HEADER, frames = 0; for (let i = 0; i < n; i++) { total += enc[i].bytes.length; frames += pack.slots[i].pcm.length; }
  const img = new Uint8Array(total);
  img.set([0x4c, 0x46, 0x50, 0x31], 0); img[4] = 1; img[6] = n; ascii(img, 8, 15, pack.name.slice(0, 15)); w32(img, 24, frames); img[28] = pack.scale; img[29] = pack.scaleRoot;
  let off = HEADER;
  for (let i = 0; i < n; i++) {
    const s = pack.slots[i], t = 64 + i * 32;
    w32(img, t, off); w32(img, t + 4, s.pcm.length); w32(img, t + 8, s.loopStart); w32(img, t + 12, s.loopEnd < 0 ? 0xffffffff : s.loopEnd);
    img[t + 16] = s.lo; img[t + 17] = s.hi; img[t + 18] = s.root; img[t + 19] = s.flags; ascii(img, t + 20, 11, s.name.slice(0, 11)); img[t + 31] = s.tune & 255;
    img[1088 + 2 * i] = (s.voices & 15) | (s.choke << 4); img[1089 + 2 * i] = s.glide;
    img.set(enc[i].bytes, off); off += enc[i].bytes.length;
  }
  return img;
}
/** read an image (a .lfp file or a pack read back) into an editable pack; the pcm is the decoded 2 bit audio */
function parseImage(img, name) {
  if (img.length < HEADER || r32(img, 0) !== 0x3150464c || (img[4] | (img[5] << 8)) !== 1) throw new Error('this is not a lofi pack');
  const n = img[6] | (img[7] << 8); if (n > MAX_SLOTS) throw new Error('bad slot count');
  const str = (o, m) => { let s = ''; for (let i = 0; i < m && img[o + i]; i++) s += String.fromCharCode(img[o + i]); return s; };
  const pack = new Pack({ name: name || str(8, 15) || 'IMPORTED', scale: Math.min(img[28], 7), scaleRoot: img[29] % 12 });
  for (let i = 0; i < n; i++) {
    const t = 64 + i * 32, off = r32(img, t), frames = r32(img, t + 4), le = r32(img, t + 12);
    if (off < HEADER || off + LF.codec.encodedBytes(frames) > img.length) throw new Error('slot ' + (i + 1) + ' is outside the image');
    pack.slots.push(new Slot({ name: str(t + 20, 11), pcm: LF.codec.decode(img, off, frames), lo: img[t + 16], hi: img[t + 17], root: img[t + 18], flags: img[t + 19],
      loopStart: r32(img, t + 8), loopEnd: le === 0xffffffff ? -1 : le, voices: img[1088 + 2 * i] & 15, choke: img[1088 + 2 * i] >> 4, glide: img[1089 + 2 * i], tune: (img[t + 31] << 24) >> 24 }));
  }
  return pack;
}
LF.pack = { KEYS, MAX_BANKS, MAX_SLOTS, HEADER, CAPACITY, SCALES, NOTES, F_LOOP, F_REVERSE, F_ONESHOT, Slot, Pack, buildImage, parseImage, encodeSlot };
})(typeof window !== 'undefined' ? (window.LF = window.LF || {}) : (module.exports = (global.LF = global.LF || {})));
