// The JavaScript Uploader against the real C device (core/dev_server) over a lossy link, then a "reboot" of the device.
const fs = require('fs'), path = require('path'), cp = require('child_process'); const LF = {}; for (const f of ['codec', 'proto', 'pack']) Object.assign(LF, require('../src/' + f + '.js'));
const root = path.join(__dirname, '..', '..'), img = new Uint8Array(fs.readFileSync(path.join(root, 'out/demo.lfp')));
function device() {
  const proc = cp.spawn(path.join(root, 'core/dev_server')), lines = []; let waiter = null, buf = '';
  proc.stdout.on('data', d => { buf += d; let i; while ((i = buf.indexOf('\n')) >= 0) { const l = buf.slice(0, i); buf = buf.slice(i + 1); if (waiter) { const w = waiter; waiter = null; w(l); } else lines.push(l); } });
  const ask = line => new Promise(res => { if (lines.length) return res(lines.shift()); waiter = res; proc.stdin.write(line + '\n'); });
  return { ask, kill: () => proc.kill() };
}
const hex = u8 => Buffer.from(u8).toString('hex'), unhex = s => new Uint8Array(Buffer.from(s, 'hex'));
function lossy(dev, loss, seed) {
  let x = seed; const rnd = () => (x = (x * 1664525 + 1013904223) >>> 0) / 4294967296; const replies = []; let chain = Promise.resolve();
  return { send(m) { chain = chain.then(async () => { if (rnd() * 100 < loss) return; const r = await ask(dev, m); if (r && rnd() * 100 >= loss) replies.push(unhex(r)); }); },
    async receive(ms) { await chain; return replies.length ? replies.shift() : null; }, drain() { replies.length = 0; } };
  async function ask(d, m) { return d.ask(hex(m)); }
}
(async () => {
  let bad = 0;
  for (const loss of [0, 10, 25]) for (const seed of [1, 2, 3, 4, 5, 6]) {
    const dev = device(), up = new LF.proto.Uploader(lossy(dev, loss, seed), { pacingMs: 0, replyTimeoutMs: 20 });
    await up.send(img); const crc = LF.proto.crc32(img), want = 'state=1 size=' + img.length + ' crc=' + crc.toString(16).padStart(8, '0');
    const d1 = await dev.ask('dump'); await dev.ask('reboot'); const d2 = await dev.ask('dump'); dev.kill();
    const ok = d1 === want && d2 === want; if (!ok) bad++;
    if (seed === 1 || !ok) console.log((ok ? 'PASS  ' : 'FAIL  ') + 'loss ' + loss + '% seed ' + seed + ': ' + d1 + ' | after reboot: ' + d2 + ' | blocks ' + up.blocksSent + ' resent ' + up.blocksResent + ' retries ' + up.retries);
  }
  console.log(bad ? 'FAILED ' + bad : 'ALL PASSED (18 uploads)'); process.exit(bad ? 1 : 0);
})();
