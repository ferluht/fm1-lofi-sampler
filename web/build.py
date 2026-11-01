#!/usr/bin/env python3
"""Builds the single file editor: web/dist/index.html (also copied to ../releases/FM1-Lofi-Editor.html).
The modules are inlined, plus the DEMO pack image (out/demo.lfp) as base64 for the ADD DEMO PACK button."""
import base64, os, shutil
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
src = lambda n: open(os.path.join(here, 'src', n)).read()
demo = base64.b64encode(open(os.path.join(root, 'out', 'demo.lfp'), 'rb').read()).decode()
scripts = ''.join('<script>\n%s\n</script>\n' % src(f) for f in ['codec.js', 'proto.js', 'pack.js', 'audio.js', 'midi.js'])
html = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>FM1 Lofi editor</title>
<style>
%s
</style></head><body><div id="app"></div>
<script>window.DEMO_LFP_B64 = "%s";</script>
%s<script>
%s
</script></body></html>
''' % (src('style.css'), demo, scripts, src('app.js'))
os.makedirs(os.path.join(here, 'dist'), exist_ok=True)
out = os.path.join(here, 'dist', 'index.html'); open(out, 'w').write(html)
os.makedirs(os.path.join(root, 'releases'), exist_ok=True); shutil.copy(out, os.path.join(root, 'releases', 'FM1-Lofi-Editor.html'))
print('built', out, len(html), 'bytes')
