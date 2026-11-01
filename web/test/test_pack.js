// pack.js: the image of the DEMO pack built in JavaScript must be identical to the one built by core/mkpack (C); and reading it back works.
const fs = require('fs'), path = require('path'); const LF = {}; for (const f of ['codec', 'proto', 'pack']) Object.assign(LF, require('../src/' + f + '.js'));
const root = path.join(__dirname, '..', '..'), A = path.join(root, 'android/app/src/main/assets/demo');
const P = LF.pack, mk = (name, f, lo, hi, root_, flags, extra = {}) => { const raw = fs.readFileSync(path.join(A, f + '.raw')); return new P.Slot({ name, pcm: new Int16Array(raw.buffer, raw.byteOffset, raw.length / 2), lo, hi, root: root_, flags, ...extra }); };
(async () => {
  const pack = new P.Pack({ name: 'DEMO', scale: 4, scaleRoot: 0 });
  for (let i = 1; i <= 9; i++) pack.slots.push(mk('hit' + i, 'hit' + i, i - 1, i - 1, i - 1, 4));
  pack.slots.push(mk('tape_key', 'tape_key', 32, 58, 0, 0, { tune: -14 }), mk('shiny', 'shiny', 64, 90, 0, 0, { tune: -2 }), mk('phonk', 'phonk', 96, 122, 0, 0));
  const img = await P.buildImage(pack), want = fs.readFileSync(path.join(root, 'out/demo.lfp'));
  let ok = img.length === want.length && Buffer.compare(Buffer.from(img), want) === 0;
  console.log((ok ? 'PASS  ' : 'FAIL  ') + 'image built in JS (' + img.length + ' bytes) is identical to the C image (' + want.length + ' bytes), crc ' + LF.proto.crc32(img).toString(16));
  if (!ok) { let d = 0; for (let i = 0; i < Math.min(img.length, want.length); i++) if (img[i] !== want[i]) { d = i; break; } console.log('first difference at byte ' + d); process.exit(1); }
  const back = P.parseImage(img); let bad = back.slots.length !== 12 || back.scale !== 4 || back.slots[9].lo !== 32 || back.slots[9].root !== 0 || back.slots[9].tune !== -14 || back.name !== 'DEMO';
  for (let i = 0; i < 12; i++) { const enc = await P.encodeSlot(pack.slots[i]); for (let k = 0; k < back.slots[i].pcm.length; k++) if (back.slots[i].pcm[k] !== enc.recon[k]) { bad = true; break; } }
  console.log((bad ? 'FAIL  ' : 'PASS  ') + 'the image reads back: 12 slots, decoded audio equals what the FM-1 plays');
  // model rules
  const q = new P.Pack(); const a = new P.Slot({ name: 'a' }), b = new P.Slot({ name: 'b' }), m = new P.Slot({ name: 'm' });
  const ok1 = q.placeKit(a); q.slots.push(a); console.log((ok1 && a.bank === 0 && a.k0 === 0 ? 'PASS  ' : 'FAIL  ') + 'first kit hit goes to zone 1 key 1'); q.placeKit(b); q.slots.push(b); console.log((b.k0 === 1 ? 'PASS  ' : 'FAIL  ') + 'the next one to key 2');
  const okm = q.placeMelodic(m); q.slots.push(m); console.log((okm && m.bank === 1 && m.k1 === 26 && !q.fits(new P.Slot(), 1, 3, 3, null) && !q.fits(new P.Slot(), 0, 0, 5, null) ? 'PASS  ' : 'FAIL  ') + 'a melodic sample takes an empty zone, nothing else may enter it, a kit zone is not melodic');
  process.exit(bad || !okm ? 1 : 0);
})();
