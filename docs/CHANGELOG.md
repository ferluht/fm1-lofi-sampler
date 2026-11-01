# fm1-lofi change log (newest at the bottom)

This file is the running log of the project, written while it was built. The reference is in README.md and the other files of docs/.

# fm1-lofi

Lo-fi sampler running on the M-VAVE FM-1 itself (firmware fork of ~/fm1/fm1_open), with a new Android app
(import, slicing, sample packs, upload over BLE MIDI SysEx). The old fm1-sampler / fm1-daw apps are NOT reused.

## Decisions (2026-11-01)
- 16 kHz mono, SEA-style LMS-ADPCM at **2 bits/sample** (user listened: "2 бита норм").
- SDK decoder fm1_sea_decode only supports 5/6 bits -> own decoder in firmware (web/synth/sea_decode.c is MIT, generic in bits).
- Storage: user flash partition 536576 B (Build.ps1 variant.user_bytes), shared with projects/settings.
  2 bit ~5.2 KB/s with 24 B state per 320-frame chunk -> ~100 s; state every 4 chunks ~120 s.
- Upload: BLE MIDI (fm1_bluetooth_configure(FM1_BT_MIDI), peripheral), SysEx blocks with ACK/NAK, resume,
  sector-wise flash write, two-sector CRC'd records (power-loss safe). Upload only while playback stopped.

## Layout
- tools/lofi_codec.c: prototype encoder+decoder (bits param), tools/run_test.sh: resample to 16k + 2/3/4 bit test.
- samples/: Tape_Key, Shiny, Phonk (from LOOPA kits). Amen break not found on the machine (asked user).
- out/, mp3/: test renders (sent to Telegram).

## Test results (SNR vs 16k reference)
Phonk 52/60/67 dB, Shiny 32/40/47, Tape Key 34/43/49 (2/3/4 bit).

## Listening results (2026-11-01)
- 2-bit @16k approved on Tape Key, Shiny, Phonk and on TrapKit hits (9 hits via tools/slice_kit.py; short hits SNR only 9-12 dB but user: sounds fine).
- Same 2-bit for every slot; per-slot bit depth NOT needed for now.

## Open questions
- Speed range decided: 4 octaves down to 1 octave up (1/16x..2x) -> decode <=32k samples/s per voice (2x), cheaper below 1x. Reverse/scrub not answered: default = seed every 2 chunks (640 frames), reverse decodes a 2-chunk group.
- Amen break: not available, TrapKit used instead.

## Core codec (done 2026-11-01)
core/lofi_codec.h (+lofi_dqt.inc): final block format, shared header for firmware decoder and host/app encoder.
Superblock = 640 samples = 192 B (16 B seed + 2 chunks x 88 B), 4.8 KB/s -> ~111 s in 536576 B. Each superblock decodes independently (seek, reverse, loops).
core/test_core.c: encoder/decoder bit-exact, random-order superblock decode identical, SNR same as prototype. Speed change: Hermite resampler (tools/pitch_test.py), user approved sound -4..+1 oct.

## Pack format + upload protocol (done 2026-11-01, host-tested)
- core/lofi_pack.h: pack image (header 1152 B, 32 slots: offset/frames/loop/key range/root/flags/name), builder + validator.
- core/lofi_proto.h: SysEx framing (F0 7D 'L' 'F' 01 ..., 7-bit packed, <=192 B), device state machine over abstract flash:
  HELLO/BEGIN/DATA/QUERY/END/ABORT/DELETE; 128 B blocks, 32 per 4 KB sector, per-sector bitmap acks, no per-block reply, resume in same session.
  Flash: sectors 0,1 commit records (A/B by generation, CRC), 2.. image. BEGIN invalidates the old pack -> interrupted upload = empty bank.
- core/lofi_sender.h: reference sender (app implements the same). core/sim_upload.c: test (`cd core; gcc ... sim_upload.c; ./sim_upload`).
- Results: 12 slots/33 s/160 KB pack uploads bit-exact at 0/10/20/30% loss + duplicates; power-loss injected at every flash op: never corrupt.
- Not yet: real firmware integration (incoming SysEx queue instead of stock single buffer, flash ops only in control task), real BLE speed.

## Sampler engine + firmware (done 2026-11-01, builds; NOT run on a device)
- core/lofi_engine.[ch]: 8 voices, streams the 2-bit data straight from memory mapped flash (fm1_flash_map), Hermite resampler,
  live speed 1/16x..2x (1/48 octave steps) + pitch bend, attack/release, loop, soft knee limiter. Host test core/test_engine.c:
  output matches a numpy Hermite reference at 58.6 dB; mp3 of the sweep test was sent to the user.
- firmware/ = copy of ~/fm1-daw/firmware (fm1_open fork, stock engine untouched in the SDK lib but no longer rendered):
  app/board/audio.c calls le_render() instead of fm1_engine_render(); app/lofi_ui.c replaces ctrl.c (screen, keys, encoders, LEDs,
  SysEx queue 40 x 192 B, MIDI-in hook, flash glue to lf_dev_handle); transfer.c only forwards lofi SysEx (stock project transfer
  disabled because the whole user flash belongs to the pack); midi_router.c forwards notes/bend/CC20 to lofi_midi.
- core headers are copied into firmware/app (lofi_codec.h lofi_dqt.inc lofi_pack.h lofi_proto.h lofi_engine.[ch]): re-copy after editing core/.
- Build: `python3 tools/build_fw.py firmware ~/fm1/wine/drive_c/JL/pi32` -> firmware/build/FM1-Lofi-Workbench.html (image 412392 B, limit 453921).
- Panel: enc0 slot, K1 speed (live), K2 attack, K3 release, K4 level, PLAY audition, HOME all off, OCT -/+ transpose keys, keys 53+k (F3..).
  MIDI: notes, pitch bend +-2 st, CC20 speed.
- Unverified on hardware: everything (upload speed over BLE, flash ops from control task, ISR load, screen, whether stock boot code touches flash).

## Android app (done 2026-11-01, builds; NOT run on a phone)
- android/ (Java, package com.ferluht.lofi, minSdk 23, NDK JNI of the same core headers). Build: `cd android && ANDROID_HOME=~/android-sdk ~/gradle/gradle-8.9/bin/gradle assembleDebug` -> fm1lofi-v0.1.apk.
- Screens: pack list (capacity bar), pack editor (slots, keys, loop, preview that plays the real 2-bit decode via JNI round trip, speed -4..+1 oct),
  slicer (waveform, tap = marker, drag, long-press delete, AUTO onset slicing, ALL->KIT / SELECTED->HIT / SELECTED->KEYS), export .lfp, upload (USB MIDI list + BLE MIDI scan, progress, retries).
- Storage: files/packs/<id>/pack.json + slotN.pcm (16 kHz mono int16), so packs stay editable. No .lfp import yet (export only).
- Upload verified off-device: tools/run_e2e.sh runs the real Java Uploader against the C device (core/dev_server) at 0/10/25% loss, pack identical after "reboot".
- Unverified: everything that needs Android/BLE hardware (JNI glue, MediaCodec import, android.media.midi, BLE scan/permissions, real speed).

## Zones, scale, per-zone voices/glide/choke, sequencer (2026-11-01, host-tested, NOT on hardware)
- Pack header: byte 28 scale (0 off, 1..7 CHROM MIX MAJ MIN HMIN PHRYG MIN+), byte 29 scale root; table at 1088: per zone voices(0..8)/choke group(0..15)/glide (x10 ms).
- Zones = slots with lo..hi key ranges. Zones of several keys follow the scale (first key = tonic, sample pitch moved to the scale root); one key zones (drums) ignore it.
  le_pitch_offset(), core/test_scale.c. Per zone: voice limit (steal oldest), choke group (fast cut of the whole group), legato glide (core/test_zone.c).
- LEDs of the FM-1 are single colour on/off (software scan 10 kHz, no PWM yet): keys of every mapped zone light; zone colours are on the firmware screen (27 key strip) and in the app (ZoneView, same palette).
- Firmware: hold SCL = scale screen (K1 scale, K2 root); hold SEQ = sequencer page (K1 tempo, K2 loop bars 1/2/4/8, K3 quantize off/1/8/1/16/1/32, K4 pattern A-D, ALG metronome, PRESETS count-in);
  PLAY start/stop, REC record (count-in 1 bar, REC again = back to play, overdub), ARP undo last REC pass, EDIT held 0.9 s clears the pattern, SAVE writes the loops to the last flash sector (valid for the pack crc only), HOME all notes off.
- Sequencer: core/lofi_engine.c (96 ticks/beat, 4/4, 4 patterns x 192 events, quantize of the note on with the gate kept, metronome click, block accurate). core/test_seq.c. Loops are loaded at boot (host harness: hosttest/lofi_host.c).
- Data capacity is now 128 sectors (524288 B): the last sector keeps the loops.
- App: pack screen ZONES = keyboard zone map (drag bars, resize ends, tap to jump, long-press edit, AUTO LAYOUT, scale + root selectors), slot dialog has voices / choke / glide.

## Button menu rewrite (2026-11-01, host-tested, NOT on hardware) - v0.5
- Every panel button is a screen of 4 tiles (K1..K4 = encoders 2..5, SELECT = encoder 0, ALG = encoder 1 on FX); the active page button is the only page LED lit, pressing it again goes HOME.
  FX(2) reverb/delay/crush/filter, SEL(3), ENV(4), LFO(5), EDIT(6), GLO(7), HOME(8), SAVE(9), ARP(10), SEQ(11), PLAY(12), REC(13). The old SCL overlay is gone (scale is on GLO).
- Engine (core/lofi_engine.c, tests core/test_fx.c + test_seq/zone/scale): per zone ADSR, LFO (pitch/volume/pan), start/end, reverse, volume, mute, 4 sends; 4 send effects (reverb freeverb-lite, delay half rate 0.9 s max,
  bitcrusher, SVF filter); 12 punch-ins (FX page keys): REV DLY CRU DWN LPF HPF ST8 S16 GAT HLF DBL STP; arpeggiator (rate/dir/range/gate, SELECT turns on); 8 patterns x 160 events; measure (beats per bar).
- Keys per page: SEL = mute/unmute the zone of the key, FX = punch-ins, SAVE = pattern select, others = notes (or arp). Pressing a key also makes its zone the active one.
- PLAY: while recording stops recording and keeps playing; otherwise start/stop. REC toggles recording (count-in from stop).
- Persistence: last two flash sectors = loops + settings (zone controls, fx, arp, level, scale), keyed by pack crc; SAVE page K1 writes them ('*' in the header = unsaved). Pack capacity is now 127 sectors (520192 B).
- Image 439724 of 453921 bytes (tight), RAM bss 289 KB of 523 KB.
- Host harness firmware/hosttest/lofi_host.c: 20+ checks (upload through the firmware glue, every page LED, SEL mute, FX punch, arp, REC/PLAY semantics, power cycle restores loops + settings) + page screenshots.

## Key lag + synchronous patterns (2026-11-01, host-tested)
- Lag: the page redraw ran inside the control task: up to 385 screen calls / 92573 pixels in one tick (text = one blit per character, waveform = 220 fills, full screen clears), so keys were polled late.
  Now all drawing goes through a queue (gfx_* in lofi_ui.c): code only enqueues, every control tick flushes a bounded amount (cost 4 + px/64, budget 150, big fills split in 3000 px bands), text is one blit per string,
  the SEQ playhead and single keys are redrawn alone, redraw is tiles first and the picture only when it depends on the knob, upload screen draws only the percentage. Worst tick now 38 calls / 11520 px.
  Hardware latency not measured; if it persists look at the control task period and fm1_screen_blit speed.
- Patterns: one shared transport clock (q.abs); a pattern plays at abs % its length, so switching keeps the position (test_seq 'patterns play in step').
- Image 445836 of 453921 bytes: about 8 KB left.

## Zones as banks of 27 keys, new defaults (2026-11-01, v0.6, host-tested)
- A zone is a bank of 27 keys; OCT- / OCT+ (and HOME K4) switch zones; note id = bank*32 + key (lo/hi in the pack are ids). A zone is a kit (one sample per key) or one melodic sample over its keys. Filled keys are lit.
  Melodic samples: first key = tonic, follows the scale (no scale = chromatic), slot.root = the PITCH CLASS of the sample; base octave per zone on HOME K3 (le_zone(slot)->oct, saved in the settings blob v2). Kit keys play at their own pitch.
- Defaults: release about 1 s (idx 106), bpm 140, scale C minor (engine boot + new app packs), metronome only while recording (count-in only when recording starts).
- App: BankView (rows = zones, 27 cells, drag/resize/tap-to-move, edit dialog with zone/first key/last key/pitch), Pack.fits/placeKit/placeMelodic/autoLayout, old packs are converted once (Store.load model 1 -> 2), new DEMO pack (zone 1 hits, zones 2-4 Tape Key / Shiny / Phonk).
- Tests: core/test_defaults.c, firmware/hosttest/lofi_host.c (OCT switching, LEDs of kit vs melodic zone, base octave on HOME). Image 447276 of 453921 bytes: 6.6 KB left.

## Melodic zone range + keyboard starts at F (2026-11-01, v0.7, host-tested)
- Bug: only one octave sounded in a melodic zone, higher keys all played the same pitch: the voice pitch was clamped at 2x the sample rate. Now up to 8x (three octaves above the sample), decoding cost grows with the speed.
- Melodic zones start one octave down (zone octave -1 by default, HOME K3). Keys follow the FM-1 keyboard that starts at F: key 1 = the first scale note at or above F (F in C minor), then the scale; no scale = chromatic from F.
  Octave 0 = the F nearest to the sample's pitch class (slot.root). Test: core/test_scale.c (C minor F G Ab Bb C D Eb, D minor, C major), core/test_defaults.c (27 keys all different).

## Next
1. Flash FM1-Lofi-Workbench.html (rollback to V15 is embedded), install fm1lofi-v0.1.apk, upload a pack, listen. 2. Measure: BLE speed, ISR load per voice, flash write behaviour. 3. .lfp import, reverse/scrub (seeds already allow it), per-slot lo-fi effects.
- 2026-11-01 docs/CHANGELOG: web editor added (web/), firmware v0.8 sounding keys flip LED

## v0.9 (2026-11-01, host-tested, NOT on hardware): tuning between zones, readable roll, free first take
- Zones were out of tune with each other: the pitch of a melodic sample was a hand-set pitch CLASS (default C, the DEMO pack had guesses 6/3/10 for samples that are all C) and a sample that is 14 cents off
  stayed off. Now the slot has a fine tune in cents: pack slot byte 31 (i8, the name is 11 chars + NUL, so it was free), added to the voice pitch in the engine (`ev_pitch`, 1 semitone = 4*256 units).
  Web editor: SELECTED -> KEYS detects the pitch (YIN on 1024-sample windows, median, `LF.audio.rootAndTune`, test `web/test/test_pitch.js`) and sets root + tune, EDIT has "fine tune" and DETECT PITCH, preview plays with the tune.
  DEMO pack rebuilt: all three melodic samples root C, tune -14 / -2 / 0 ct (mkpack takes `...:tune` as the 8th field).
- SEQ roll: rows are now the pitches really used (sorted), not the raw note-id scale: notes of different zones (32 ids apart) were squeezed onto the same rows and ran together into one long bar. One pixel gap at the end of a bar,
  white tick at its start.
- SEQ roll, second pass (the user: notes still drawn with glitches and wrong length): (1) length = the release that FOLLOWS the press in the list (recording order); matching by nearest time broke when passes overlap
  (overdub of one key) and drew short / long bars; (2) a key still held while recording grows with the playhead (was a 12 tick stub); (3) the whole roll is redrawn when the event count changes (partial strips kept stale bars);
  host check: strips drawn while playing == full redraw (0 pixel difference).
- Free first take (engine existed since 13:57, UI missing): SEQ page K4 "1ST TAKE" CLICK / FREE. REC with FREE = armed (no count-in, REC LED blinks), the clock starts with the first key, REC or PLAY ends it,
  length -> 1/2/4/8 bars and BPM (40..240), events are scaled onto the grid, the loop plays. Header shows FREE, the screen shows a seconds clock. Test: test_seq.c (engine) and lofi_host.c (UI).
- Image 449740 of 453921 bytes (4.1 KB free).
