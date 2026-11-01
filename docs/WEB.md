# The web editor

`releases/FM1-Lofi-Editor.html` is **one self-contained file** (about 270 KB, no network needed): open it in Chrome or Edge (desktop or phone), build packs and send them to the FM-1 over **Web MIDI** (USB, or a Bluetooth LE MIDI device paired in the system settings). It does everything the Android app does, runs the same codec byte for byte, and in addition reads `.lfp` files back.

Sources are in `web/src/`, built into the single file by `python3 web/build.py` (output `web/dist/index.html`, copied to `releases/`).

## Requirements

* A browser with **Web MIDI + SysEx** (Chrome / Edge; Firefox needs an add-on; Safari has no Web MIDI).
* A **secure context**: open the file locally (`file://`), from `http://localhost`, or from an `https://` address. (The same requirement as the firmware flash page.)
* Chrome asks once for the permission to use MIDI devices with SysEx.
* Everything you make is stored in the browser (IndexedDB, per browser and address): clear-site-data deletes the packs, so keep `.lfp` exports of packs you care about.

## Using it

1. **NEW PACK** (or **ADD DEMO PACK**: the TrapKit hits, Tape Key, Shiny and Phonk bass; or **IMPORT .LFP**).
2. **ADD AUDIO** (any file the browser decodes, several at once). The slicer opens on the waveform:
   click = new slice point, drag = move, double-click or press-and-hold = remove, click the strip at the bottom of the waveform = pick a slice, wheel / ZOOM = zoom, the slider = scroll; **AUTO** cuts at the onsets (sensitivity slider), **▶ PLAY SLICE** plays the picked slice through the real 2-bit codec; **ALL → KIT** puts every slice on the next free kit key, **SELECTED → HIT** one slice, **SELECTED → KEYS** the picked slice as a melodic sample over a whole zone (27 keys).
3. **Zones** (canvas): one row per zone of 27 keys. Drag a sample to another key or zone, drag the end cells of a melodic sample, click an empty cell to move the selected sample there, double-click to edit; ◀ KEY / KEY ▶ / ZONE ▲ / ZONE ▼, AUTO LAYOUT, scale (OFF … MIN+) and root.
4. **EDIT** (dialog): name, zone / first key / last key, pitch of a melodic sample, loop with start / end, one-shot, voices, choke group, glide, preview speed.
5. **Send to the FM-1**: CONNECT → pick the FM-1 output → UPLOAD (encodes, then uploads with the progress bar and a log; CANCEL stops). The FM-1 shows the upload screen and the instrument replaces the old pack.
6. **EXPORT .LFP** saves the exact image that would be uploaded.

The rules of zones, scales and keys are those of the instrument (USER_GUIDE.md); the pack format is PACK_FORMAT.md and the protocol is UPLOAD_PROTOCOL.md.

## Modules (`web/src/`)

| File | Role |
|---|---|
| `codec.js` | the 2-bit LMS-ADPCM encoder and decoder, **byte exact with `core/lofi_codec.h`** (plain JS numbers, all intermediate values are exact below 2^53) |
| `proto.js` | CRC-32 / CRC-16, 7-bit packing, SysEx frames, and the `Uploader` (port of `lofi_sender.h`) |
| `pack.js` | `Slot` / `Pack` model (zone rules `fits`, `placeKit`, `placeMelodic`, `autoLayout`), `buildImage` (the image, byte exact with `lfp_build_ex`), `parseImage` (reads a `.lfp` back; the audio is the decoded 2-bit signal) |
| `audio.js` | import (`decodeAudioData` + an `OfflineAudioContext` with a 7.2 kHz low pass to 16 kHz mono), onset slicing, tail trimming, preview playback |
| `midi.js` | Web MIDI access, input/output matching by name, SysEx reassembly, the link used by the uploader |
| `app.js`, `style.css` | the user interface (DOM + two canvases), IndexedDB storage |
| `build.py` | inlines everything (and the DEMO pack image as base64) into one HTML file |

Encoding runs on the main thread in slices of about 40 ms (the page stays responsive); a pack of 160 KB encodes in about 0.2 s, a full 520 KB pack in under a second on a desktop. Encoded slots are cached until their audio changes.

## Tests (`web/test/`, run with node 24 and headless Chrome)

```sh
node web/test/test_codec.js     # the 12 DEMO samples are encoded identically to the C encoder; the decoder equals the encoder's reconstruction
node web/test/test_pack.js      # the whole DEMO image built in JS is identical to core/mkpack's (crc 8ca79e99); parseImage reads it back; zone rules
node web/test/test_e2e.js       # the JS Uploader against the real C device code (core/dev_server): 6 seeds x 0 / 10 / 25 % loss, then a reboot of the device
node web/test/ui_test.js        # the built page in headless Chrome with a mock Web MIDI wired to the C device: demo pack, wav import, auto slice, all->kit,
                                # zones, upload (flash image checked by CRC), .lfp import, no errors in the page; screenshots go to docs/img/web_*.png
```

## Verified / not verified

* Verified (above): codec, pack image, protocol against the real device code, and every user flow of the page in headless Chrome with a mocked MIDI port (including decoding a real wav with the browser's own decoder).
* **Not verified**: a real FM-1 over real Web MIDI (USB or BLE), Chrome on a phone, touch gestures (the canvas uses pointer events and should work), other browsers. The page cannot know whether the OS exposes the FM-1's BLE MIDI to Chrome.
