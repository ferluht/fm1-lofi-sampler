/* The pack upload protocol (SysEx) in JavaScript: framing, CRCs and the sender. Same logic as core/lofi_proto.h (device) and core/lofi_sender.h /
 * core/lofi_sender.h; documented in docs/UPLOAD_PROTOCOL.md. Verified against the real C device code by web/test/test_e2e.js. */
(function (LF) {
'use strict';
const SECTOR = 4096, BLOCK = 128, MAXMSG = 192;
const CMD = { HELLO: 1, BEGIN: 2, DATA: 3, QUERY: 4, END: 5, ABORT: 6, DELETE: 7 };
const crcTable = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(d, off = 0, len = d.length - off) { let c = 0xffffffff; for (let i = 0; i < len; i++) c = crcTable[(c ^ d[off + i]) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
function crc16(d, n) { let c = 0xffff; for (let i = 0; i < n; i++) { c ^= d[i] << 8; for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff; } return c; }
function pack7(inp, n) { const out = []; let acc = 0, bits = 0; for (let i = 0; i < n; i++) { acc |= inp[i] << bits; bits += 8; while (bits >= 7) { out.push(acc & 0x7f); acc >>>= 7; bits -= 7; } } if (bits > 0) out.push(acc & 0x7f); return out; }
function unpack7(inp, off, n) { const out = []; let acc = 0, bits = 0; for (let i = 0; i < n; i++) { acc |= (inp[off + i] & 0x7f) << bits; bits += 7; if (bits >= 8) { out.push(acc & 0xff); acc >>>= 8; bits -= 8; } } return Uint8Array.from(out); }
function frame(cmd, seq, payload) {
  const body = pack7(payload || new Uint8Array(0), payload ? payload.length : 0), m = [0xf0, 0x7d, 0x4c, 0x46, 1, cmd, seq & 0x7f, (seq >> 7) & 0x7f, ...body];
  let sum = 0; for (let i = 1; i < m.length; i++) sum += m[i];
  m.push((128 - (sum & 0x7f)) & 0x7f, 0xf7); return Uint8Array.from(m);
}
function parse(m) {
  const n = m.length; if (n < 10 || n > MAXMSG || m[0] !== 0xf0 || m[n - 1] !== 0xf7 || m[1] !== 0x7d || m[2] !== 0x4c || m[3] !== 0x46 || m[4] !== 1) return null;
  let sum = 0; for (let i = 1; i < n - 1; i++) { if (m[i] & 0x80) return null; sum += m[i]; } if (sum & 0x7f) return null;
  return { cmd: m[5], seq: m[6] | (m[7] << 7), payload: unpack7(m, 8, n - 10) };
}
const p32 = (b, o, v) => { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; };
const g32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const sleep = ms => new Promise(r => setTimeout(r, ms));

/** link = { send(Uint8Array), receive(timeoutMs) -> Promise<Uint8Array|null>, drain() }. Throws Error with a readable message on failure. */
class Uploader {
  constructor(link, opts = {}) { this.link = link; this.seq = 0; this.pacingMs = opts.pacingMs == null ? 3 : opts.pacingMs; this.replyTimeoutMs = opts.replyTimeoutMs || 500;
    this.retries = 0; this.blocksSent = 0; this.blocksResent = 0; this.aborted = false; }
  async request(cmd, payload) {
    const s = ++this.seq & 0x3fff, msg = frame(cmd, s, payload);
    for (let attempt = 0; attempt < 12; attempt++) {
      if (this.aborted) throw new Error('cancelled'); if (attempt) this.retries++;
      this.link.drain(); this.link.send(msg);
      const end = Date.now() + this.replyTimeoutMs;
      for (;;) { const left = end - Date.now(); if (left <= 0) break; const r = await this.link.receive(left); if (!r) break; const m = parse(r); if (m && m.cmd === (cmd | 0x40) && m.seq === s) return m.payload; }
    }
    throw new Error('no answer from the FM-1');
  }
  async send(img, progress = () => {}) {
    const size = img.length, crc = crc32(img);
    progress(0, 1, 'connecting');
    let p = await this.request(CMD.HELLO);
    if (p.length < 19 || p[0] !== 0) throw new Error('the FM-1 refused HELLO (is the lofi firmware installed?)');
    const dataSectors = p[2] | (p[3] << 8); if (size > dataSectors * SECTOR) throw new Error('the pack does not fit: ' + size + ' > ' + dataSectors * SECTOR);
    const b = new Uint8Array(3 + BLOCK + 2); p32(b, 0, size); p32(b, 4, crc);
    p = await this.request(CMD.BEGIN, b.subarray(0, 8));
    if (p.length < 5 || p[0] !== 0) throw new Error('BEGIN rejected (status ' + (p.length ? p[0] : -1) + ')');
    const next = p[1] | (p[2] << 8), total = p[3] | (p[4] << 8);
    for (let sec = next; sec < total; sec++) {
      progress(sec, total, 'sending');
      const rem = Math.min(SECTOR, size - sec * SECTOR), blocks = Math.ceil(rem / BLOCK), want = blocks >= 32 ? 0xffffffff : ((1 << blocks) - 1) >>> 0;
      let have = 0, done = false;
      for (let round = 0; round < 40 && !done; round++) {
        for (let blk = 0; blk < blocks; blk++) {
          if ((have >>> blk) & 1) continue; if (this.aborted) throw new Error('cancelled');
          b.fill(0); b[0] = sec & 255; b[1] = sec >> 8; b[2] = blk; const off = sec * SECTOR + blk * BLOCK; b.set(img.subarray(off, Math.min(img.length, off + BLOCK)), 3);
          const c = crc16(b, 3 + BLOCK); b[3 + BLOCK] = c & 255; b[4 + BLOCK] = c >> 8;
          this.link.send(frame(CMD.DATA, 0, b)); this.blocksSent++; if (round) this.blocksResent++;
          if (this.pacingMs > 0) await sleep(this.pacingMs);
        }
        const q = await this.request(CMD.QUERY, Uint8Array.of(sec & 255, sec >> 8));
        if (q.length < 8 || q[0] !== 0) throw new Error('QUERY failed (status ' + (q.length ? q[0] : -1) + ')');
        if (q[3]) done = true; else have = (g32(q, 4) & want) >>> 0;
      }
      if (!done) throw new Error('sector ' + sec + ' would not complete');
    }
    progress(total, total, 'verifying');
    const e = new Uint8Array(4); p32(e, 0, crc); p = await this.request(CMD.END, e);
    if (p.length < 1 || p[0] !== 0) throw new Error(p.length && p[0] === 4 ? 'CRC mismatch in the FM-1 flash' : 'commit failed (status ' + (p.length ? p[0] : -1) + ')');
    progress(total, total, 'done');
  }
}
LF.proto = { SECTOR, BLOCK, MAXMSG, CMD, crc32, crc16, pack7, unpack7, frame, parse, Uploader };
})(typeof window !== 'undefined' ? (window.LF = window.LF || {}) : (module.exports = (global.LF = global.LF || {})));
