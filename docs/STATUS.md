# Status

Version at the time of writing: firmware / app **v0.7**. Image 447 404 / 453 921 bytes.

## What has been run where

| Part | Host (automated) | Real FM-1 / phone |
|---|---|---|
| 2-bit codec | bit exact encode/decode, random access, SNR; listening tests of 2/3/4 bit (user approved 2 bit, incl. drum hits and -4 octave pitch) | decoder runs in every firmware since v0.1 |
| Pack format, scales, zones | tests | used by the user in versions up to 0.6 |
| Engine (voices, env, LFO, start/end/reverse, sends, punch-ins, arp, sequencer) | `test_zone`, `test_fx`, `test_seq`, `test_scale`, `test_defaults`, `test_engine` | the user played versions up to 0.6 (feedback: key lag, pattern restart, one-octave melodic zone, defaults) – these were fixed afterwards; **no instrumented measurements** |
| Upload protocol | `sim_upload` (loss, duplicates, power cut at every flash operation), `run_e2e.sh` (Java sender ↔ C device) | the pack must have been delivered to the FM-1 for the user's tests; **speed and failure behaviour over real BLE were never measured** |
| Firmware UI | `lofi_host` harness (25+ checks, per-tick drawing limits, all screens rendered) | the user's feedback up to 0.6; v0.7 not yet reported on |
| Web editor | codec / image byte exact with C; uploader against the real C device; every page flow in headless Chrome with a mocked MIDI port | real Web MIDI to the FM-1 (USB / BLE), Chrome for Android, touch |
| Android app | builds; Java protocol classes in a JVM | **screens, import (MediaCodec), slicing, BLE scanning, permissions: no screenshots or measurements exist** (no emulator / KVM here) |

Evidence of real use: the changes of versions 0.5-0.7 were requested because of what the user saw on the instrument (lag of the keys, patterns restarting, melodic keys sounding the same, "keyboard starts with F"). Everything below is therefore *known on the host*, *reported by the user*, or *unknown*.

## Open risks (unmeasured)

1. **CPU in the audio interrupt.** 8 voices, up to 8x pitch (128 000 decoded samples/s per voice), reverse play, reverb + delay + filter + crush, arpeggiator, sequencer. No cycle count exists. First thing to look at if the audio crackles: fewer active effects (sends at 0 stop their buses), 4 voices instead of 8, limiting the pitch range.
2. **RAM.** `.ram0_bss` is 289 KB of 523 KB; the Bluetooth stack allocates from the rest at run time (unknown amount).
3. **Control task period / SPI speed** of the screen are unknown, so the real latency of the keys after the screen queue change is not known (the work per tick is bounded; time per tick is not measured).
4. **Flash writes** (upload, SAVE) silence the audio for their duration and block the control task; SAVE during playback produces a short dropout.
5. **BLE throughput and robustness** (connection interval, MTU, Android versions, pairing) are unknown; USB is the fallback.
6. **Image size**: 6.5 KB of program flash left.

## Known limitations and small defects

* Delay time labels: the options 3/4 and "1 BEAT" (a whole note) exceed the 1 s buffer at most tempi and are clamped to about 1 s.
* SEQ screen `K4 SCROLL` is a placeholder (stored but not used by the roll).
* `le_seq_undo` (undo the last REC pass) exists in the engine but is not wired to a button.
* Settings and loops are saved only by SAVE (and only for the current pack). There is no autosave, no storage of several settings sets per pack.
* The app cannot edit envelope / LFO / effects / start-end (device only) and cannot import `.lfp` files. Packs live on the phone as PCM; **packs created by an earlier app version are re-laid out** on first open (zones model changed).
* Uploading while playing is not supported (the audio is silenced by the flash writes; the upload screen takes over the display).
* A melodic zone at 27 keys uses all keys; a seven-note scale then spans almost four octaves, at the top the playback speed is 6-8x the stored rate (very bright and aliased, partially lo-fi on purpose).
* Mono output effects (the delay and the reverb are mono sends into both channels); stereo only from the LFO pan.
* MIDI notes address only zones 0-3; the tempo is not synced to MIDI clock; no MIDI out of the played notes.
* The amen break asked for at the start was not found on the machine; TrapKit (sliced into 9 hits) was used instead.
* The firmware identifies itself as the stock product id (V14) in the update page.

## Ideas that were discussed and not done

* LED brightness levels (software PWM in the 10 kHz scan interrupt, ~900 Hz frame → 4 levels) to mark zones or velocities.
* Per-slot bit depth (3-4 bits for short hits) – not needed after the listening test.
* Autosave of settings when idle; undo on a button; delay time labels in ms.
* `.lfp` import in the app; editing envelopes / zones of the loaded pack from the phone; a live view of the FM-1 state.
* Reverse play and scrubbing control beyond the existing EDIT reverse; the superblock seeds already allow it.
* Sample editing in the app (normalise, trim by drag); recording from the phone microphone.
