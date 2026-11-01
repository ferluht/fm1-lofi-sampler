/* Web MIDI link to the FM-1 (USB MIDI, or a Bluetooth LE MIDI device the operating system / Chrome has paired). Needs a secure context (https, localhost or a local file)
 * and the SysEx permission. Implements the Link of proto.js: send(), receive(timeoutMs), drain(). */
(function (LF) {
'use strict';
async function open() {
  if (!navigator.requestMIDIAccess) throw new Error('this browser has no Web MIDI (use Chrome or Edge, on a phone Chrome)');
  const access = await navigator.requestMIDIAccess({ sysex: true });
  return access;
}
const list = (access) => ({ outputs: [...access.outputs.values()], inputs: [...access.inputs.values()] });
/** pick the input that belongs to an output (same name), or the only input */
function matchInput(access, out) { const ins = [...access.inputs.values()]; return ins.find(i => i.name === out.name) || ins.find(i => (i.name || '').replace(/in|out/gi, '') === (out.name || '').replace(/in|out/gi, '')) || (ins.length === 1 ? ins[0] : null); }
class Link {
  constructor(output, input) {
    this.out = output; this.in = input; this.queue = []; this.waiters = []; this.sx = []; this.inSx = false;
    this.handler = ev => this.feed(ev.data); input.addEventListener('midimessage', this.handler);
  }
  feed(d) {
    for (let i = 0; i < d.length; i++) {
      const b = d[i];
      if (b === 0xf0) { this.sx = [b]; this.inSx = true; } else if (this.inSx && b === 0xf7) { this.sx.push(b); this.push(Uint8Array.from(this.sx)); this.inSx = false; this.sx = []; }
      else if (this.inSx && b < 0x80) this.sx.push(b); else if (b >= 0x80 && b < 0xf8) this.inSx = false;
    }
  }
  push(m) { const w = this.waiters.shift(); if (w) { clearTimeout(w.t); w.res(m); } else this.queue.push(m); }
  send(m) { this.out.send(m); }
  receive(ms) { if (this.queue.length) return Promise.resolve(this.queue.shift()); return new Promise(res => { const w = { res }; w.t = setTimeout(() => { const i = this.waiters.indexOf(w); if (i >= 0) this.waiters.splice(i, 1); res(null); }, ms); this.waiters.push(w); }); }
  drain() { this.queue.length = 0; }
  close() { this.in.removeEventListener('midimessage', this.handler); }
}
LF.midi = { open, list, matchInput, Link };
})(typeof window !== 'undefined' ? (window.LF = window.LF || {}) : (module.exports = (global.LF = global.LF || {})));
