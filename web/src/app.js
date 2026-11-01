/* FM1 LOFI editor: the browser version of the Android app. Packs live in IndexedDB; import audio, slice, map zones, preview the real 2 bit sound, upload over Web MIDI. */
(function () {
'use strict';
const LF = window.LF, P = LF.pack, COLORS = ['#ff8a00', '#00ffff', '#00ff00', '#ff00ff', '#ffff00', '#0080ff', '#ff0000', '#9a60ff', '#a0ff30', '#ff8080', '#00a0a0', '#dcdcdc'];
const $ = (s, r = document) => r.querySelector(s);
function el(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  for (const k in attrs || {}) { const v = attrs[k]; if (k === 'class') e.className = v; else if (k.startsWith('on')) e.addEventListener(k.slice(2), v); else if (k === 'text') e.textContent = v; else if (v !== false && v != null) e.setAttribute(k, v === true ? '' : v); }
  for (const c of kids.flat()) if (c != null) e.append(c.nodeType ? c : document.createTextNode(c)); return e;
}
const kb = n => (n / 1024).toFixed(1) + ' KB', noteName = i => P.NOTES[i % 12];
const zoneText = s => 'Z' + (s.bank + 1) + ' · ' + (s.isKit ? 'key ' + (s.k0 + 1) : 'keys ' + (s.k0 + 1) + '–' + (s.k1 + 1) + ' · pitch ' + noteName(s.root));

/* ------------------------------------------------------------------ storage (IndexedDB) */
const db = {
  h: null,
  open() { return this.h ? Promise.resolve(this.h) : new Promise((res, rej) => { const r = indexedDB.open('fm1lofi', 1); r.onupgradeneeded = () => r.result.createObjectStore('packs', { keyPath: 'id' }); r.onsuccess = () => res(this.h = r.result); r.onerror = () => rej(r.error); }); },
  async tx(mode, fn) { const h = await this.open(); return new Promise((res, rej) => { const t = h.transaction('packs', mode), s = t.objectStore('packs'), q = fn(s); t.oncomplete = () => res(q && q.result); t.onerror = () => rej(t.error); }); },
  async all() { return (await this.tx('readonly', s => s.getAll())) || []; },
  async put(pack) { const o = { id: pack.id, name: pack.name, scale: pack.scale, scaleRoot: pack.scaleRoot, slots: pack.slots.map(s => ({ name: s.name, pcm: s.pcm, lo: s.lo, hi: s.hi, root: s.root, flags: s.flags, loopStart: s.loopStart, loopEnd: s.loopEnd, voices: s.voices, choke: s.choke, glide: s.glide, tune: s.tune })) }; return this.tx('readwrite', s => s.put(o)); },
  async del(id) { return this.tx('readwrite', s => s.delete(id)); },
};
const toPack = o => new P.Pack({ id: o.id, name: o.name, scale: o.scale, scaleRoot: o.scaleRoot, slots: o.slots.map(s => new P.Slot(s)) });

/* ------------------------------------------------------------------ state + dialogs */
const S = { packs: [], pack: null, sel: -1, midi: null, link: null, log: [], uploading: false, progress: 0, stage: '' };
const root = () => $('#app');
function toast(msg) { const t = el('div', { class: 'toast', text: msg }); document.body.append(t); setTimeout(() => t.remove(), 3500); }
async function save() { if (S.pack) { try { await db.put(S.pack); } catch (e) { toast('could not save: ' + e.message); } } }
function modal(title, body, buttons) {
  const back = el('div', { class: 'modal-back' }), box = el('div', { class: 'modal' }, el('h2', { text: title }), body, el('div', { class: 'row end' }, buttons.map(b => el('button', { class: 'btn' + (b.primary ? ' primary' : ''), text: b.label, onclick: () => { if (!b.keep) back.remove(); if (b.fn) b.fn(); } }))));
  back.append(box); document.body.append(back); return back;
}

/* ------------------------------------------------------------------ views */
function render() { const r = root(); r.replaceChildren(); if (S.pack) packView(r); else packsView(r); }
function packsView(r) {
  r.append(el('h1', { text: 'FM1 LOFI' }), el('p', { class: 'note', text: 'Sample packs for the FM-1 lo-fi sampler: 16 kHz, 2 bit. Build a pack here, send it over USB or Bluetooth MIDI. Everything stays in this browser.' }));
  const file = el('input', { type: 'file', accept: '.lfp', hidden: true, id: 'lfp-input', onchange: async e => { const f = e.target.files[0]; if (!f) return; try { const p = P.parseImage(new Uint8Array(await f.arrayBuffer()), f.name.replace(/\.lfp$/i, '').slice(0, 15)); S.packs.push(p); await db.put(p); toast('imported ' + p.name); render(); } catch (err) { toast(err.message); } e.target.value = ''; } });
  r.append(el('div', { class: 'row' }, el('button', { class: 'btn primary', text: 'NEW PACK', onclick: async () => { const p = new P.Pack(); S.packs.push(p); await db.put(p); S.pack = p; S.sel = -1; render(); } }),
    el('button', { class: 'btn', text: 'IMPORT .LFP', onclick: () => file.click() }), el('button', { class: 'btn', id: 'demo-btn', text: 'ADD DEMO PACK', onclick: addDemo }), file));
  if (!S.packs.length) r.append(el('p', { class: 'note', text: 'No packs yet. Try ADD DEMO PACK (TrapKit hits, Tape Key, Shiny, Phonk bass) to see how it works.' }));
  for (const p of S.packs.slice().sort((a, b) => a.name.localeCompare(b.name))) {
    r.append(el('div', { class: 'card', onclick: () => { S.pack = p; S.sel = p.slots.length ? 0 : -1; render(); } }, el('div', { class: 'title', text: p.name }),
      el('div', { class: 'note', text: p.slots.length + ' samples · ' + p.seconds.toFixed(1) + ' s · ' + kb(p.imageBytes) + ' of ' + kb(P.CAPACITY) }), bar(p.imageBytes / P.CAPACITY)));
  }
}
const bar = f => el('div', { class: 'bar' }, el('div', { style: 'width:' + Math.max(0, Math.min(1, f)) * 100 + '%' + (f > 1 ? ';background:#ff3b30' : '') }));
async function addDemo() {
  const bin = atob(window.DEMO_LFP_B64), u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
  const p = P.parseImage(u, 'DEMO'); p.id = 'demo-' + Date.now().toString(36); S.packs.push(p); await db.put(p); toast('DEMO pack added'); render();
}
function packView(r) {
  const p = S.pack;
  const name = el('input', { class: 'name', value: p.name, maxlength: 15, onchange: async e => { p.name = e.target.value.trim().slice(0, 15) || p.name; await save(); } });
  r.append(el('div', { class: 'row' }, el('button', { class: 'btn', text: '◀ PACKS', onclick: async () => { await save(); S.pack = null; render(); } }), name));
  r.append(el('div', { class: 'note', id: 'cap', text: p.slots.length + ' of ' + P.MAX_SLOTS + ' samples · ' + p.seconds.toFixed(1) + ' s · ' + kb(p.imageBytes) + ' of ' + kb(P.CAPACITY) + (p.freeBytes < 0 ? '  (TOO BIG)' : '') }), bar(p.imageBytes / P.CAPACITY));
  const fi = el('input', { type: 'file', id: 'audio-input', accept: 'audio/*,.wav,.mp3,.flac,.ogg,.m4a', multiple: true, hidden: true, onchange: async e => { for (const f of e.target.files) await importAudio(f); e.target.value = ''; } });
  const askDelete = () => modal('Delete "' + p.name + '"?', el('p', { text: 'This cannot be undone.' }), [{ label: 'Cancel' }, { label: 'Delete', primary: true, fn: async () => { await db.del(p.id); S.packs = S.packs.filter(x => x !== p); S.pack = null; render(); } }]);
  r.append(el('div', { class: 'row' }, el('button', { class: 'btn primary', id: 'add-audio', text: 'ADD AUDIO', onclick: () => fi.click() }), fi,
    el('button', { class: 'btn', text: 'EXPORT .LFP', onclick: exportPack }), el('button', { class: 'btn danger', text: 'DELETE PACK', onclick: askDelete })));
  r.append(el('h2', { text: 'Samples' }));
  const list = el('div', { class: 'slots' });
  p.slots.forEach((s, i) => list.append(el('div', { class: 'slot' + (i === S.sel ? ' sel' : ''), onclick: () => { S.sel = i; zonesRedraw(); markSel(); } },
    el('span', { class: 'chip', style: 'background:' + COLORS[i % 12] }), el('div', { class: 'grow' }, el('div', { class: 'title', text: (i + 1) + '  ' + s.name }), el('div', { class: 'note', text: zoneText(s) + ' · ' + s.seconds.toFixed(2) + ' s · ' + kb(s.bytes) + (s.flags & P.F_LOOP ? ' · loop' : '') })),
    el('button', { class: 'btn', text: '▶', title: 'preview (the real 2 bit sound)', onclick: ev => { ev.stopPropagation(); preview(s, 0); } }), el('button', { class: 'btn', text: 'EDIT', onclick: ev => { ev.stopPropagation(); editSlot(i); } }))));
  if (!p.slots.length) list.append(el('p', { class: 'note', text: 'Add an audio file: it is cut into slices that become the keys of a kit, or one melodic sample.' }));
  r.append(list, el('h2', { text: 'Zones' }), el('p', { class: 'note', text: 'A zone is 27 keys; OCT- / OCT+ on the FM-1 switch zones. A zone is a kit (one sample per key) or one melodic sample. Drag a sample to another key or zone, drag the ends of a melodic sample, click an empty cell to move the selected sample there, double-click to edit.' }),
    zonesBox(), el('h2', { text: 'Send to the FM-1' }), uploadBox());
}
function markSel() { document.querySelectorAll('.slot').forEach((e, i) => e.classList.toggle('sel', i === S.sel)); }

/* ------------------------------------------------------------------ zones (canvas) */
let zc = null;
function zonesBox() {
  const p = S.pack, box = el('div', { class: 'zones' }), canvas = el('canvas', { id: 'zones' }); const info = el('div', { class: 'note', id: 'zinfo' });
  const scaleRow = el('div', { class: 'chips' }, P.SCALES.map((n, i) => el('button', { class: 'chip-btn' + (p.scale === i ? ' on' : ''), text: n, onclick: async () => { p.scale = i; await save(); render(); } })));
  const rootRow = p.scale ? el('div', { class: 'chips' }, P.NOTES.map((n, i) => el('button', { class: 'chip-btn' + (p.scaleRoot === i ? ' on' : ''), text: n, onclick: async () => { p.scaleRoot = i; await save(); render(); } }))) : null;
  const act = (f) => async () => { if (S.sel < 0 || S.sel >= p.slots.length) return; f(p.slots[S.sel]); await save(); render(); };
  const move = (db_, dk) => act(s => { const nb = s.bank + db_, nk = s.k0 + dk; if (db_ && !p.fits(s, nb, s.k0, s.k1, s)) return toast('that zone cannot take it (a zone is a kit or one melodic sample)'); if (dk && !s.isKit) return toast('a melodic sample takes the whole zone'); if (dk && !p.fits(s, s.bank, nk, nk, s)) return toast('that key is taken'); s.setBankKeys(s.bank + db_, dk ? nk : s.k0, dk ? nk : s.k1); });
  box.append(canvas, info, el('div', { class: 'row' }, el('button', { class: 'btn', text: '◀ KEY', onclick: move(0, -1) }), el('button', { class: 'btn', text: 'KEY ▶', onclick: move(0, 1) }), el('button', { class: 'btn', text: 'ZONE ▲', onclick: move(-1, 0) }), el('button', { class: 'btn', text: 'ZONE ▼', onclick: move(1, 0) }),
    el('button', { class: 'btn', text: 'PLAY', onclick: () => { if (S.sel >= 0) preview(p.slots[S.sel], 0); } }), el('button', { class: 'btn', text: 'EDIT', onclick: () => S.sel >= 0 && editSlot(S.sel) }),
    el('button', { class: 'btn', text: 'AUTO LAYOUT', onclick: async () => { p.autoLayout(); await save(); render(); } })),
    el('p', { class: 'note', text: 'scale of the melodic samples (kit keys ignore it); the keys start at F' }), scaleRow, rootRow);
  setTimeout(() => zonesInit(canvas), 0); return box;
}
const ROWH = 40, LABW = 54;
function zonesInit(canvas) {
  const p = S.pack; zc = { canvas, drag: null };
  const rows = () => Math.min(P.MAX_BANKS, p.banks() + 1), cw = () => (canvas.clientWidth - LABW) / P.KEYS;
  const size = () => { const dpr = window.devicePixelRatio || 1, w = canvas.clientWidth, h = rows() * ROWH + 4; canvas.style.height = h + 'px'; canvas.width = w * dpr; canvas.height = h * dpr; canvas.getContext('2d').setTransform(dpr, 0, 0, dpr, 0, 0); };
  const draw = zc.draw = () => {
    size(); const g = canvas.getContext('2d'), w = canvas.clientWidth; g.fillStyle = '#0b0b0b'; g.fillRect(0, 0, w, canvas.clientHeight); g.font = '11px system-ui'; const c = cw();
    for (let b = 0; b < rows(); b++) { const y = b * ROWH; g.fillStyle = b % 2 ? '#0b0b0b' : '#101010'; g.fillRect(0, y, w, ROWH); const k = p.bankKind(b); g.fillStyle = '#8a8a8a'; g.fillText('Z' + (b + 1), 6, y + 17); g.fillStyle = '#555'; g.fillText(k === 0 ? 'empty' : k === 1 ? 'kit' : 'melodic', 6, y + 31);
      for (let k2 = 0; k2 < P.KEYS; k2++) { g.fillStyle = '#1a1a1a'; g.fillRect(LABW + k2 * c + 1, y + 5, c - 2, ROWH - 10); } }
    p.slots.forEach((s, i) => { const b = s.bank; if (b >= rows()) return; const y = b * ROWH, x0 = LABW + s.k0 * c, x1 = LABW + (s.k1 + 1) * c, sel = i === S.sel;
      g.globalAlpha = sel ? 1 : 0.72; g.fillStyle = COLORS[i % 12]; g.fillRect(x0 + 1, y + 5, x1 - x0 - 2, ROWH - 10); g.globalAlpha = 1;
      if (!p.fits(s, b, s.k0, s.k1, s)) { g.strokeStyle = '#ff3b30'; g.lineWidth = 3; g.strokeRect(x0 + 1, y + 5, x1 - x0 - 2, ROWH - 10); } else if (sel) { g.strokeStyle = '#fff'; g.lineWidth = 2; g.strokeRect(x0 + 1, y + 5, x1 - x0 - 2, ROWH - 10); }
      g.fillStyle = '#000'; let n = s.name; const mw = x1 - x0 - 6; while (n.length > 1 && g.measureText(n).width > mw) n = n.slice(0, -1); if (mw > 12) g.fillText(n, x0 + 3, y + ROWH / 2 + 4); });
    const s = p.slots[S.sel], info = $('#zinfo'); if (info) info.textContent = s ? s.name + '  ' + zoneText(s) + (s.isKit ? '  · a kit key plays at its own pitch' : '  · keys start at F, follow ' + (p.scale ? P.SCALES[p.scale] + ' ' + P.NOTES[p.scaleRoot] : 'chromatic')) + (s.voices ? '  · ' + (s.voices === 1 ? 'mono' : s.voices + ' voices') : '') + (s.choke ? '  · choke ' + s.choke : '') + (s.glide ? '  · glide ' + s.glide * 10 + ' ms' : '') : 'Click a sample to pick it.';
  };
  const cell = ev => { const r = canvas.getBoundingClientRect(); return { b: Math.max(0, Math.min(rows() - 1, Math.floor((ev.clientY - r.top) / ROWH))), k: Math.max(0, Math.min(P.KEYS - 1, Math.floor((ev.clientX - r.left - LABW) / cw()))) }; };
  canvas.onpointerdown = ev => { canvas.setPointerCapture(ev.pointerId); const { b, k } = cell(ev), i = p.slotAt(b, k); zc.drag = { i, moved: false, x: ev.clientX, y: ev.clientY, k0: 0 };
    if (i >= 0) { const s = p.slots[i]; S.sel = i; Object.assign(zc.drag, { bank: s.bank, lo: s.k0, hi: s.k1, ck: k, mode: s.hi - s.lo >= 2 ? (k === s.k0 ? 1 : k === s.k1 ? 2 : 0) : 0 }); markSel(); } draw(); };
  canvas.onpointermove = ev => { const d = zc.drag; if (!d || d.i < 0) return; if (Math.abs(ev.clientX - d.x) > 6 || Math.abs(ev.clientY - d.y) > 6) d.moved = true; if (!d.moved) return; const { b, k } = cell(ev), s = p.slots[d.i];
    if (d.mode === 0) { const w = d.hi - d.lo, nk = Math.max(0, Math.min(P.KEYS - 1 - w, d.lo + (k - d.ck))); if (p.fits(s, b, nk, nk + w, s)) s.setBankKeys(b, nk, nk + w); }
    else if (d.mode === 1) { const nk = Math.min(k, d.hi - 1); if (p.fits(s, d.bank, nk, d.hi, s)) s.setBankKeys(d.bank, nk, d.hi); } else { const nk = Math.max(k, d.lo + 1); if (p.fits(s, d.bank, d.lo, nk, s)) s.setBankKeys(d.bank, d.lo, nk); } draw(); };
  canvas.onpointerup = async ev => { const d = zc.drag; zc.drag = null; if (!d) return; const { b, k } = cell(ev);
    if (d.i < 0 && !d.moved && S.sel >= 0 && S.sel < p.slots.length) { const s = p.slots[S.sel], w = s.hi - s.lo; let nk = Math.min(k, P.KEYS - 1 - w); if (w > 0) nk = 0; if (p.fits(s, b, nk, nk + w, s)) s.setBankKeys(b, nk, nk + w); }
    await save(); render(); };
  canvas.ondblclick = ev => { const { b, k } = cell(ev), i = p.slotAt(b, k); if (i >= 0) editSlot(i); };
  draw(); window.addEventListener('resize', draw, { once: true });
}
const zonesRedraw = () => zc && zc.draw && zc.draw();

/* ------------------------------------------------------------------ slot editing */
function editSlot(i) {
  const p = S.pack, s = p.slots[i], num = (v, lo, hi, d) => { const n = parseInt(v, 10); return isNaN(n) ? d : Math.max(lo, Math.min(hi, n)); };
  const f = {}; const field = (label, key, val, type = 'number', extra = {}) => el('label', {}, label, f[key] = el('input', { type, value: val, ...extra }));
  const pitch = f.pitch = el('select', {}, P.NOTES.map((n, k) => el('option', { value: k, text: n, selected: k === (s.isKit ? 0 : s.root % 12) })));
  const loop = f.loop = el('input', { type: 'checkbox', checked: !!(s.flags & P.F_LOOP) }), one = f.one = el('input', { type: 'checkbox', checked: !!(s.flags & P.F_ONESHOT) });
  const spd = f.spd = el('input', { type: 'range', min: -4, max: 1, step: 1, value: 0 }), spdt = el('span', { text: '0 oct' }); spd.oninput = () => spdt.textContent = spd.value + ' oct';
  const body = el('div', { class: 'form' }, field('name', 'name', s.name, 'text', { maxlength: 11 }),
    el('div', { class: 'grid3' }, field('zone 1-8', 'zone', s.bank + 1, 'number', { min: 1, max: 8 }), field('first key 1-27', 'k0', s.k0 + 1, 'number', { min: 1, max: 27 }), field('last key 1-27', 'k1', s.k1 + 1, 'number', { min: 1, max: 27 })),
    el('p', { class: 'note', text: 'One key = a kit hit; several keys = a melodic sample (then the zone holds nothing else).' }), el('label', {}, 'pitch of the sample (melodic: the keys start at F, the zone begins one octave below the sample)', pitch), el('div', { class: 'grid2' }, field('fine tune, cents (-50..50)', 'tune', s.tune, 'number', { min: -50, max: 50 }), el('button', { class: 'btn', text: 'DETECT PITCH', onclick: () => { const rt = LF.audio.rootAndTune(s.pcm); if (!rt) return toast('no clear pitch found'); f.pitch.value = rt.root; f.tune.value = rt.tune; toast('detected ' + P.NOTES[rt.root] + ' ' + (rt.tune > 0 ? '+' : '') + rt.tune + ' ct (' + rt.hz.toFixed(1) + ' Hz)'); } })),
    el('label', { class: 'inline' }, loop, 'loop'), el('div', { class: 'grid2' }, field('loop start ms', 'ls', Math.round(s.loopStart / 16), 'number'), field('loop end ms', 'le', s.loopEnd < 0 ? '' : Math.round(s.loopEnd / 16), 'number')),
    el('label', { class: 'inline' }, one, 'one-shot (ignore the key release)'), el('div', { class: 'grid3' }, field('voices (0 = unlimited, 1 = mono)', 'vo', s.voices, 'number', { min: 0, max: 8 }), field('choke group (0 = none)', 'ch', s.choke, 'number', { min: 0, max: 15 }), field('glide ms (0 = off)', 'gl', s.glide * 10, 'number', { min: 0, max: 2550 })),
    el('label', {}, 'preview speed ', spd, spdt), el('button', { class: 'btn', text: '▶ PLAY', onclick: () => preview(s, +spd.value) }));
  modal('Sample ' + (i + 1), body, [{ label: 'Delete', fn: async () => { p.slots.splice(i, 1); S.sel = Math.min(S.sel, p.slots.length - 1); await save(); render(); } }, { label: 'Cancel', fn: () => LF.audio.stop() },
    { label: 'Save', primary: true, fn: async () => {
      s.name = f.name.value.trim().slice(0, 11) || s.name; const nb = num(f.zone.value, 1, 8, s.bank + 1) - 1, a0 = num(f.k0.value, 1, 27, s.k0 + 1) - 1, a1 = Math.max(a0, num(f.k1.value, 1, 27, s.k1 + 1) - 1);
      if (p.fits(s, nb, a0, a1, s)) { s.setBankKeys(nb, a0, a1); if (!s.isKit) s.root = +f.pitch.value; } else toast('that place is taken: the sample keeps its old keys');
      const a = Math.max(0, num(f.ls.value, 0, 1e9, 0)) * 16, e = f.le.value === '' ? -1 : num(f.le.value, 0, 1e9, 0) * 16, lp = f.loop.checked && e > a;
      s.flags = (lp ? P.F_LOOP : 0) | (f.one.checked ? P.F_ONESHOT : 0) | (s.flags & P.F_REVERSE); s.loopStart = a; s.loopEnd = lp ? Math.min(e, s.pcm.length) : -1;
      s.voices = num(f.vo.value, 0, 8, 0); s.choke = num(f.ch.value, 0, 15, 0); s.glide = Math.round(num(f.gl.value, 0, 2550, 0) / 10); s.tune = num(f.tune.value, -50, 50, 0); LF.audio.stop(); await save(); render(); } }]);
}
async function preview(slot, oct) { try { const e = await P.encodeSlot(slot, f => status('encoding ' + Math.round(f * 100) + '%')); status(''); LF.audio.play(e.recon, oct + (slot.tune || 0) / 1200); } catch (err) { toast(err.message); } }
function status(t) { const e = $('#status'); if (e) e.textContent = t; }

/* ------------------------------------------------------------------ import + slicer */
async function importAudio(file) {
  const busy = el('div', { class: 'busy', text: 'decoding ' + file.name + ' ...' }); document.body.append(busy);
  try { const pcm = await LF.audio.importFile(file); busy.remove(); if (pcm.length < 160) return toast(file.name + ' is empty'); slicer(pcm, file.name.replace(/\.[^.]+$/, '').slice(0, 9)); }
  catch (e) { busy.remove(); toast(e.message); }
}
function slicer(pcm, base) {
  const p = S.pack; const st = { marks: [], sel: 0, v0: 0, v1: 1 }; const canvas = el('canvas', { id: 'wave' }), info = el('div', { class: 'note' });
  const bounds = () => [0, ...st.marks, pcm.length], seg = i => { const b = bounds(); i = Math.max(0, Math.min(i, b.length - 2)); return pcm.subarray(b[i], b[i + 1]); };
  const xOf = n => (n / pcm.length - st.v0) / (st.v1 - st.v0) * canvas.clientWidth, at = x => Math.round((st.v0 + (st.v1 - st.v0) * x / canvas.clientWidth) * pcm.length);
  const refresh = () => { const sg = seg(st.sel); info.textContent = (st.marks.length + 1) + ' slices · selected ' + (st.sel + 1) + ': ' + (sg.length / 16000).toFixed(2) + ' s, ' + kb(LF.codec.encodedBytes(sg.length)) + '  ·  pack free ' + kb(p.freeBytes); };
  const draw = () => {
    const dpr = window.devicePixelRatio || 1, w = canvas.clientWidth, h = 220; canvas.style.height = h + 'px'; canvas.width = w * dpr; canvas.height = h * dpr; const g = canvas.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = '#0b0b0b'; g.fillRect(0, 0, w, h); const b = bounds(); g.fillStyle = '#1d1308'; g.fillRect(Math.max(0, xOf(b[st.sel])), 0, Math.min(w, xOf(b[st.sel + 1])) - Math.max(0, xOf(b[st.sel])), h);
    const mid = h / 2, s0 = st.v0 * pcm.length, span = (st.v1 - st.v0) * pcm.length; g.strokeStyle = '#444'; g.beginPath(); g.moveTo(0, mid); g.lineTo(w, mid); g.stroke();
    for (let x = 0; x < w; x++) { const a = Math.floor(s0 + span * x / w), e = Math.max(a + 1, Math.floor(s0 + span * (x + 1) / w)), step = Math.max(1, ((e - a) / 64) | 0); let mn = 32767, mx = -32768; for (let i = a; i < e && i < pcm.length; i += step) { const v = pcm[i]; if (v < mn) mn = v; if (v > mx) mx = v; }
      const sample = at(x), inSel = sample >= b[st.sel] && sample < b[st.sel + 1]; g.fillStyle = inSel ? '#ff8a00' : '#efefef'; g.fillRect(x, mid - mx / 32768 * mid * 0.95, 1, Math.max(1, (mx - mn) / 32768 * mid * 0.95)); }
    g.fillStyle = '#ff8a00'; for (const m of st.marks) { const x = xOf(m); if (x >= 0 && x <= w) g.fillRect(x - 1, 0, 3, h); } refresh();
  };
  let drag = -1, moved = false, down = null, pressT = null;
  const nearMark = x => { let best = -1, bd = 12; st.marks.forEach((m, i) => { const d = Math.abs(xOf(m) - x); if (d < bd) { bd = d; best = i; } }); return best; };
  const pos = ev => ev.clientX - canvas.getBoundingClientRect().left;
  canvas.onpointerdown = ev => { canvas.setPointerCapture(ev.pointerId); const x = pos(ev); drag = nearMark(x); moved = false; down = { x, y: ev.clientY - canvas.getBoundingClientRect().top };
    if (drag >= 0) pressT = setTimeout(() => { if (!moved && drag >= 0) { st.marks.splice(drag, 1); drag = -1; draw(); } }, 600); };
  canvas.onpointermove = ev => { if (!down) return; const x = pos(ev); if (Math.abs(x - down.x) > 6) moved = true; if (drag >= 0 && moved) { st.marks[drag] = Math.max(1, Math.min(pcm.length - 1, at(x))); draw(); } };
  canvas.onpointerup = ev => { clearTimeout(pressT); if (!down) return; const x = pos(ev);
    if (drag >= 0) { if (moved) { const m = st.marks[drag]; st.marks.sort((a, b) => a - b); drag = st.marks.indexOf(m); } }
    else if (!moved) { const strip = down.y > 220 * 0.8, s = Math.max(1, Math.min(pcm.length - 1, at(x))); if (!strip) st.marks.push(s), st.marks.sort((a, b) => a - b); const b = bounds(); for (let i = 0; i + 1 < b.length; i++) if (s >= b[i] && s < b[i + 1]) st.sel = i; }
    down = null; drag = -1; draw(); };
  canvas.ondblclick = ev => { const i = nearMark(pos(ev)); if (i >= 0) { st.marks.splice(i, 1); draw(); } };
  canvas.onwheel = ev => { ev.preventDefault(); const c = (st.v0 + st.v1) / 2, f = ev.deltaY < 0 ? 0.7 : 1.4, w = Math.min(1, Math.max(0.005, (st.v1 - st.v0) * f)); st.v0 = Math.max(0, Math.min(1 - w, c - w / 2)); st.v1 = st.v0 + w; pan.value = st.v0 / Math.max(1e-9, 1 - w) * 1000; draw(); };
  const zoom = f => { const c = (st.v0 + st.v1) / 2, w = Math.min(1, Math.max(0.005, (st.v1 - st.v0) * f)); st.v0 = Math.max(0, Math.min(1 - w, c - w / 2)); st.v1 = st.v0 + w; draw(); };
  const pan = el('input', { type: 'range', min: 0, max: 1000, value: 0, oninput: e => { const w = st.v1 - st.v0; st.v0 = e.target.value / 1000 * (1 - w); st.v1 = st.v0 + w; draw(); } });
  const sens = el('input', { type: 'range', min: 0, max: 100, value: 50 });
  const room = bytes => { if (p.slots.length >= P.MAX_SLOTS) { toast('a pack holds ' + P.MAX_SLOTS + ' samples'); return false; } if (p.imageBytes + bytes > P.CAPACITY) { toast('not enough room: ' + kb(P.CAPACITY - p.imageBytes) + ' free'); return false; } return true; };
  const finish = async () => { await save(); back.remove(); render(); };
  const addKit = async () => { let n = st.marks.length + 1, added = 0; for (let i = 0; i < n; i++) { const sg = LF.audio.trimTail(seg(i)); if (sg.length < 160) continue; if (!room(LF.codec.encodedBytes(sg.length))) break; const s = new P.Slot({ name: (base + (n > 1 ? i + 1 : '')).slice(0, 11), pcm: sg.slice(), flags: P.F_ONESHOT }); if (!p.placeKit(s)) { toast('no free kit key left (8 zones)'); break; } p.slots.push(s); added++; } if (added) { toast(added + ' samples added'); finish(); } };
  const addHit = async () => { const sg = LF.audio.trimTail(seg(st.sel)); if (sg.length < 160 || !room(LF.codec.encodedBytes(sg.length))) return; const s = new P.Slot({ name: base.slice(0, 11), pcm: sg.slice(), flags: P.F_ONESHOT }); if (!p.placeKit(s)) return toast('no free kit key left (8 zones)'); p.slots.push(s); finish(); };
  const addKeys = async () => { const sg = LF.audio.trimTail(seg(st.sel)); if (sg.length < 160 || !room(LF.codec.encodedBytes(sg.length))) return; const s = new P.Slot({ name: base.slice(0, 11), pcm: sg.slice(), flags: 0 }); if (!p.placeMelodic(s)) return toast('no empty zone left (8 zones)'); const rt = LF.audio.rootAndTune(s.pcm); if (rt) { s.root = rt.root; s.tune = rt.tune; toast('pitch detected: ' + P.NOTES[rt.root] + (rt.tune ? ' ' + (rt.tune > 0 ? '+' : '') + rt.tune + ' ct' : '') + ' (' + rt.hz.toFixed(1) + ' Hz)'); } else toast('no clear pitch found: set it by hand (EDIT)'); p.slots.push(s); finish(); };
  const body = el('div', { class: 'form' }, el('p', { class: 'note', text: 'Click the waveform to add a slice point, drag a point to move it, double-click (or hold) to remove it, click the lower strip to pick a slice, mouse wheel zooms.' }), canvas, pan,
    el('div', { class: 'row' }, el('button', { class: 'btn', text: 'ZOOM +', onclick: () => zoom(0.5) }), el('button', { class: 'btn', text: 'ZOOM −', onclick: () => zoom(2) }), el('button', { class: 'btn', text: '▶ PLAY SLICE', onclick: () => preview(new P.Slot({ pcm: seg(st.sel).slice() }), 0) })),
    el('label', {}, 'auto-slice sensitivity', sens), el('div', { class: 'row' }, el('button', { class: 'btn', id: 'auto', text: 'AUTO', onclick: () => { st.marks = LF.audio.autoSlice(pcm, sens.value / 100, 90); st.sel = 0; draw(); } }), el('button', { class: 'btn', text: 'CLEAR', onclick: () => { st.marks = []; st.sel = 0; draw(); } })), info,
    el('div', { class: 'row' }, el('button', { class: 'btn primary', id: 'all-kit', text: 'ALL → KIT', onclick: addKit }), el('button', { class: 'btn primary', text: 'SELECTED → HIT', onclick: addHit }), el('button', { class: 'btn primary', text: 'SELECTED → KEYS (melodic, 27 keys)', onclick: addKeys })));
  const back = modal('Slice  ' + base, body, [{ label: 'Close', fn: () => LF.audio.stop() }]); back.classList.add('wide'); setTimeout(draw, 0);
}

/* ------------------------------------------------------------------ export */
async function exportPack() {
  const p = S.pack; if (!p.slots.length) return toast('the pack is empty'); try { const img = await P.buildImage(p, (f, t) => status(t)); status('');
    const a = el('a', { href: URL.createObjectURL(new Blob([img], { type: 'application/octet-stream' })), download: p.name.replace(/ /g, '_') + '.lfp' }); document.body.append(a); a.click(); a.remove(); toast('exported ' + kb(img.length)); } catch (e) { toast(e.message); }
}

/* ------------------------------------------------------------------ upload */
function uploadBox() {
  const box = el('div', { class: 'upload' }), sel = el('select', { id: 'midi-out' }), prog = el('div', { class: 'bar big' }, el('div', { id: 'prog', style: 'width:' + S.progress * 100 + '%' })), logEl = el('pre', { id: 'log', class: 'log', text: S.log.join('\n') });
  const fillOutputs = () => { sel.replaceChildren(); const outs = S.midi ? LF.midi.list(S.midi).outputs : []; if (!outs.length) sel.append(el('option', { value: '', text: S.midi ? 'no MIDI output found' : 'press CONNECT' })); for (const o of outs) sel.append(el('option', { value: o.id, text: o.name })); };
  const connect = async () => { try { S.midi = await LF.midi.open(); S.midi.onstatechange = fillOutputs; fillOutputs(); log('MIDI ready: ' + LF.midi.list(S.midi).outputs.map(o => o.name).join(', ')); } catch (e) { toast(e.message); log(e.message); } };
  let up = null;
  const go = async () => {
    const p = S.pack; if (S.uploading) return; if (!p.slots.length) return toast('the pack is empty'); if (p.freeBytes < 0) return toast('the pack does not fit: remove some samples');
    const out = S.midi && [...S.midi.outputs.values()].find(o => o.id === sel.value); if (!out) return toast('connect and pick the FM-1 output first');
    const inp = LF.midi.matchInput(S.midi, out); if (!inp) return toast('no MIDI input of the FM-1 found (the FM-1 has to answer)');
    S.uploading = true; $('#go').disabled = true; try {
      const img = await P.buildImage(p, (f, t) => setProg(f * 0.3, t)); window.LF_LAST_IMAGE_CRC = LF.proto.crc32(img);
      const link = new LF.midi.Link(out, inp); up = new LF.proto.Uploader(link); log('uploading ' + kb(img.length));
      await up.send(img, (s, t, stage) => setProg(0.3 + 0.7 * (t ? s / t : 0), stage + ' ' + s + '/' + t));
      log('done: ' + kb(img.length) + (up.blocksResent ? ', ' + up.blocksResent + ' blocks re-sent' : '')); setProg(1, 'done'); link.close(); toast('uploaded to the FM-1');
    } catch (e) { log('failed: ' + e.message); setProg(0, 'failed'); toast(e.message); } S.uploading = false; $('#go') && ($('#go').disabled = false);
  };
  const setProg = (f, t) => { S.progress = f; const e = $('#prog'); if (e) e.style.width = f * 100 + '%'; const s = $('#stage'); if (s) s.textContent = t; };
  const log = t => { S.log.push(t); const l = $('#log'); if (l) l.textContent = S.log.slice(-8).join('\n'); };
  box.append(el('p', { class: 'note', text: 'Switch the FM-1 on and connect it with a USB cable, or pair it over Bluetooth in the system settings (Chrome lists paired BLE MIDI devices). Use Chrome or Edge; the page must be opened from https, localhost or as a local file.' }),
    el('div', { class: 'row' }, el('button', { class: 'btn', id: 'connect', text: 'CONNECT', onclick: connect }), sel, el('button', { class: 'btn primary', id: 'go', text: 'UPLOAD', onclick: go }), el('button', { class: 'btn', text: 'CANCEL', onclick: () => up && (up.aborted = true) })),
    prog, el('div', { class: 'note', id: 'stage', text: S.stage }), logEl, el('div', { class: 'note', id: 'status' }));
  fillOutputs(); return box;
}

/* ------------------------------------------------------------------ start */
(async () => { try { S.packs = (await db.all()).map(toPack); } catch (e) { toast('storage is not available: ' + e.message); } render(); window.LF_APP = { S, render, db, addDemo }; })();
})();
