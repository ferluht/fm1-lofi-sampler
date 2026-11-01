// Drives the built single file editor (web/dist/index.html) in headless Chrome through the DevTools protocol, with a mock Web MIDI whose output goes to the
// real C device code (core/dev_server): add the demo pack, import a generated wav, slice, map, upload, check the flash image, import a .lfp. Screenshots in docs/img.
const fs = require('fs'), path = require('path'), cp = require('child_process');
const root = path.join(__dirname, '..', '..'), shots = path.join(root, 'docs', 'img'), tmp = fs.mkdtempSync('/tmp/lofi-ui-');
const chrome = cp.execSync('ls ~/.cache/ms-playwright/chromium_headless_shell-*/*/chrome-headless-shell | head -1').toString().trim();
let fails = 0; const check = (ok, msg) => { console.log((ok ? 'PASS  ' : 'FAIL  ') + msg); if (!ok) fails++; };
const sleep = ms => new Promise(r => setTimeout(r, ms));
const dev = (() => { const proc = cp.spawn(path.join(root, 'core/dev_server')); let buf = '', waiter = null; const q = [];
  proc.stdout.on('data', d => { buf += d; let i; while ((i = buf.indexOf('\n')) >= 0) { const l = buf.slice(0, i); buf = buf.slice(i + 1); if (waiter) { const w = waiter; waiter = null; w(l); } else q.push(l); } });
  let chain = Promise.resolve(); const ask = line => (chain = chain.then(() => new Promise(res => { waiter = res; proc.stdin.write(line + '\n'); }))); return { ask, kill: () => proc.kill() }; })();
(async () => {
  const wav = path.join(tmp, 'drums.wav');           // 4 decaying bursts, 0.5 s apart
  cp.execSync(`ffmpeg -v error -y -f lavfi -i "aevalsrc='sin(2*PI*180*t)*exp(-t*40)*lt(mod(t,0.5),0.3)+0.3*sin(2*PI*3000*t)*exp(-mod(t,0.5)*80)':s=44100:d=2" -ac 2 ${wav}`);
  const port = 9333, proc = cp.spawn(chrome, ['--headless', '--no-sandbox', '--disable-gpu', '--remote-debugging-port=' + port, '--window-size=1000,1500', '--user-data-dir=' + tmp + '/profile', 'about:blank'], { stdio: 'ignore' });
  let target; for (let i = 0; i < 50 && !target; i++) { await sleep(200); try { const l = await (await fetch('http://127.0.0.1:' + port + '/json/list')).json(); target = l.find(t => t.type === 'page'); } catch (e) { /* not yet */ } }
  const ws = new WebSocket(target.webSocketDebuggerUrl); await new Promise(r => ws.onopen = r); let id = 0; const pend = new Map(), onEvent = [];
  ws.onmessage = m => { const d = JSON.parse(m.data); if (d.id && pend.has(d.id)) { pend.get(d.id)(d); pend.delete(d.id); } else onEvent.forEach(f => f(d)); };
  const send = (method, params = {}) => new Promise((res, rej) => { const i = ++id; pend.set(i, d => d.error ? rej(new Error(method + ': ' + JSON.stringify(d.error))) : res(d.result)); ws.send(JSON.stringify({ id: i, method, params })); });
  const ev = async expr => { const r = await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails.exception || r.exceptionDetails)); return r.result.value; };
  const shot = async name => { const r = await send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true }); fs.writeFileSync(path.join(shots, 'web_' + name + '.png'), Buffer.from(r.data, 'base64')); };
  const waitFor = async (expr, ms = 20000) => { const t = Date.now(); while (Date.now() - t < ms) { if (await ev(expr)) return true; await sleep(100); } return false; };
  const errors = []; onEvent.push(d => { if (d.method === 'Runtime.exceptionThrown') errors.push(JSON.stringify(d.params.exceptionDetails).slice(0, 300)); if (d.method === 'Runtime.consoleAPICalled' && d.params.type === 'error') errors.push(JSON.stringify(d.params.args).slice(0, 300)); });
  onEvent.push(async d => { if (d.method === 'Runtime.bindingCalled' && d.params.name === 'midiSend') { const r = await dev.ask(d.params.payload); if (r) await send('Runtime.evaluate', { expression: `window.__midiReply("${r}")` }); } });
  await send('Runtime.enable'); await send('Page.enable'); await send('DOM.enable'); await send('Runtime.addBinding', { name: 'midiSend' });
  await send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => { const listeners = []; const output = { id: 'out1', name: 'FM-1', send(d) { window.midiSend(Array.from(d).map(b => b.toString(16).padStart(2, '0')).join('')); } };
    const input = { id: 'in1', name: 'FM-1', addEventListener(t, f) { listeners.push(f); }, removeEventListener(t, f) { const i = listeners.indexOf(f); if (i >= 0) listeners.splice(i, 1); } };
    window.__midiReply = hex => { const d = Uint8Array.from(hex.match(/../g).map(h => parseInt(h, 16))); for (const f of listeners.slice()) f({ data: d }); };
    navigator.requestMIDIAccess = async () => ({ outputs: new Map([['out1', output]]), inputs: new Map([['in1', input]]), onstatechange: null }); })();` });
  await send('Emulation.setDeviceMetricsOverride', { width: 1000, height: 1400, deviceScaleFactor: 1, mobile: false });
  try { await send('Storage.clearDataForOrigin', { origin: 'file://', storageTypes: 'indexeddb' }); } catch (e) { /* older chrome */ }
  await send('Page.navigate', { url: 'file://' + path.join(root, 'web/dist/index.html') });
  await sleep(500); await ev("new Promise(r => { const q = indexedDB.deleteDatabase('fm1lofi'); q.onsuccess = q.onerror = q.onblocked = () => r(1); })").catch(() => 0); await send('Page.reload');
  const loaded = await waitFor('!!window.LF_APP', 8000); if (!loaded) console.log('errors:', errors, await ev('document.body.innerHTML.slice(0,300)')); check(loaded, 'the page loads'); await sleep(300); await shot('1_packs');
  // demo pack
  await ev(`document.querySelector('#demo-btn').click()`); const okd = await waitFor('LF_APP.S.packs.length === 1 && LF_APP.S.packs[0].slots.length === 12', 8000); if (!okd) console.log('      packs: ' + await ev('JSON.stringify(LF_APP.S.packs.map(p => [p.name, p.slots.length]))')); check(okd, 'ADD DEMO PACK: 12 samples read from the embedded image');
  await waitFor("!!document.querySelector('.card')"); await ev(`document.querySelector('.card').click()`); await sleep(500); check(await ev(`!!LF_APP.S.pack && document.querySelectorAll('.slot').length === 12`), 'the pack opens, 12 samples listed'); await shot('2_pack');
  // import a wav and slice
  const doc = await send('DOM.getDocument'); const inp = await send('DOM.querySelector', { nodeId: doc.root.nodeId, selector: '#audio-input' });
  await send('DOM.setFileInputFiles', { files: [wav], nodeId: inp.nodeId });
  check(await waitFor(`!!document.querySelector('#wave')`, 30000), 'ADD AUDIO: the wav is decoded and the slicer opens'); await sleep(300);
  await ev(`document.querySelector('#auto').click()`); await sleep(300); const marks = await ev(`document.querySelector('.modal .note:last-of-type, .modal .note') && [...document.querySelectorAll('.modal .note')].map(n => n.textContent).find(t => /slices/.test(t))`);
  console.log('      ' + marks); check(/^[4-9] slices|^\d\d slices/.test(marks || ''), 'AUTO found the 4 bursts (' + marks + ')'); await shot('3_slicer');
  await ev(`document.querySelector('#all-kit').click()`); check(await waitFor('LF_APP.S.pack.slots.length > 12'), 'ALL → KIT added the slices as kit hits (' + await ev('LF_APP.S.pack.slots.length') + ' samples)');
  const hits = await ev(`LF_APP.S.pack.slots.slice(12).map(s => 'Z' + (s.bank + 1) + 'k' + (s.k0 + 1)).join(' ')`); console.log('      new samples: ' + hits);
  await sleep(500); await ev(`window.scrollTo(0, document.getElementById('zones').getBoundingClientRect().top + scrollY - 40)`); await shot('4_zones');
  // upload to the real device code
  await ev(`document.querySelector('#connect').click()`); check(await waitFor(`document.querySelector('#midi-out').value === 'out1'`), 'CONNECT lists the MIDI output');
  await ev(`document.querySelector('#go').click()`); const done = await waitFor(`/done/.test(document.querySelector('#stage').textContent)`, 120000);
  check(done, 'UPLOAD finished: ' + await ev(`document.querySelector('#stage').textContent`)); await ev(`window.scrollTo(0, document.body.scrollHeight)`); await shot('5_upload');
  const crc = await ev('window.LF_LAST_IMAGE_CRC'), dump = await dev.ask('dump'); const m = /state=(\d) size=(\d+) crc=([0-9a-f]+)/.exec(dump);
  check(m && m[1] === '1' && parseInt(m[3], 16) === crc, 'the FM-1 (C device code) holds the pack: ' + dump + ' (page crc ' + crc.toString(16) + ')');
  // back to the list, import a .lfp
  await ev(`[...document.querySelectorAll('.btn')].find(b => /PACKS/.test(b.textContent)).click()`); await sleep(300);
  const doc2 = await send('DOM.getDocument'); const li = await send('DOM.querySelector', { nodeId: doc2.root.nodeId, selector: '#lfp-input' });
  await send('DOM.setFileInputFiles', { files: [path.join(root, 'out/demo.lfp')], nodeId: li.nodeId }); const okl = await waitFor('LF_APP.S.packs.length === 2', 8000); if (!okl) console.log('      packs: ' + await ev('JSON.stringify(LF_APP.S.packs.map(p => [p.name, p.slots.length]))')); check(okl, 'IMPORT .LFP reads a pack image back (2 packs now)');
  check(!errors.length, 'no errors in the page' + (errors.length ? ': ' + errors[0] : ''));
  ws.close(); proc.kill(); dev.kill(); console.log(fails ? 'FAILED ' + fails : 'ALL PASSED'); process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
