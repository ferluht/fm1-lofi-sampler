// pitch detector: synthetic tones and detuned tones -> root + tune
require('../src/audio.js'); const A = global.LF.audio;
let bad = 0;
for (const [hz, wave] of [[220, 'sin'], [196.5, 'saw'], [261.63 * 1.0116, 'saw'], [110, 'sq'], [440 * Math.pow(2, -35 / 1200), 'sin'], [82.41, 'saw']]) {
  const n = 16000, x = new Int16Array(n); for (let i = 0; i < n; i++) { const ph = (i * hz / 16000) % 1, env = Math.exp(-i / 12000);
    x[i] = 12000 * env * (wave === 'sin' ? Math.sin(2 * Math.PI * ph) + 0.3 * Math.sin(4 * Math.PI * ph) : wave === 'saw' ? 2 * ph - 1 : ph < 0.5 ? 1 : -1); }
  const r = A.rootAndTune(x), midi = 69 + 12 * Math.log2(hz / 440), n0 = Math.round(midi);
  const ok = r && r.root === ((n0 % 12) + 12) % 12 && Math.abs(r.tune - Math.round((n0 - midi) * 100)) <= 2;
  console.log(hz.toFixed(2), wave, r ? r.root + ' ' + r.tune + ' (' + r.hz.toFixed(2) + ')' : 'none', ok ? 'ok' : 'FAIL'); if (!ok) bad++;
}
const noise = new Int16Array(16000).map(() => (Math.random() - 0.5) * 20000); console.log('noise ->', A.rootAndTune(noise) ? 'pitch?!' : 'none (ok)');
process.exit(bad ? 1 : 0);
