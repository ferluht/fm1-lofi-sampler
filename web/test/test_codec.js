// codec.js against the C implementation: the DEMO pack image built by core/mkpack (out/demo.lfp) from the raw files of the app must be reproduced byte for byte.
const fs = require('fs'), path = require('path'); const LF = require('../src/codec.js');
const root = path.join(__dirname, '..', '..'), A = path.join(root, 'android/app/src/main/assets/demo');
const img = fs.readFileSync(path.join(root, 'out/demo.lfp'));
const names = ['hit1','hit2','hit3','hit4','hit5','hit6','hit7','hit8','hit9','tape_key','shiny','phonk'];
let off = 1152, bad = 0, total = 0, t0 = Date.now();
for (let i = 0; i < names.length; i++) {
  const raw = fs.readFileSync(path.join(A, names[i] + '.raw')), pcm = new Int16Array(raw.buffer, raw.byteOffset, raw.length / 2);
  const { bytes, recon } = LF.codec.encode(pcm), want = img.subarray(off, off + bytes.length);
  const same = Buffer.compare(Buffer.from(bytes), want) === 0; if (!same) bad++; total += bytes.length;
  const dec = LF.codec.decode(bytes, 0, pcm.length); let dm = 0; for (let k = 0; k < pcm.length; k++) if (dec[k] !== recon[k]) dm++;
  console.log((same && !dm ? 'PASS  ' : 'FAIL  ') + names[i] + ': ' + pcm.length + ' frames -> ' + bytes.length + ' bytes, identical to the C encoder: ' + same + ', decoder = encoder reconstruction: ' + (dm === 0));
  if (dm) bad++; off += bytes.length;
}
console.log((bad ? 'FAILED ' : 'ALL PASSED ') + total + ' bytes in ' + (Date.now() - t0) + ' ms');
process.exit(bad ? 1 : 0);
