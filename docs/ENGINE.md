# The sampler engine

Files: `core/lofi_engine.h` (API), `core/lofi_engine.c` (about 640 lines). Portable C: on the FM-1 it runs in the **audio interrupt** (`le_render`, called by `app/board/audio.c` instead of the stock `fm1_engine_render`), on the host it is called by the tests. Output: 128 stereo frames of signed Q23 per block, 44 100 Hz (the FM-1 fixed block format); the master gain and USB/Bluetooth audio mixing stay in `audio_output.c`.

## Threads and the event ring

* **Control side** (`le_*` functions called from the panel code): never touches voices directly. Notes, transport commands, arpeggiator keys, zone cuts go through a 64-entry **single-producer ring** of events that `le_render` drains at the start of every block (so keys sound with at most 2.9 ms delay + the control tick).
* **Settings** (`le_zone(slot)->...`, `le_fx_params`, `le_arp`, `le_set_*`) are plain fields written by the control task and read by the audio side (single-byte/word races are benign).
* **Everything the audio side calls is `FM1_AUDIO_CODE` / `FM1_AUDIO_DATA`** (RAM): flash programming stops the instruction fetch from flash. Mutable data must not use `FM1_AUDIO_DATA` (section type conflict with the const tables): it lives in ordinary BSS.

## Voices

* **8 voices.** A voice is a stream decoder: it keeps a 64-sample ring of decoded samples and the 8-word predictor state. Per output frame it advances a 32.32 phase by `pitch ratio × 16000/44100`, decodes the groups it needs ahead (`while decoded < pos + 3`) and interpolates with a **cubic Hermite (Catmull-Rom)** polynomial. Voice stealing: a free voice, else the oldest released one, else the oldest.
* **Pitch.** `ratio = 2^(pitch/12) × live speed × bend × punch speed`, clamped to **1/16 .. 8** of the stored 16 kHz rate. `pitch` is in 1/48 octave × 256 units (smooth glide); keys of a melodic zone: see PACK_FORMAT.md. Live speed: `le_set_speed(steps48)` -192..+48 (4 octaves down, 1 up) applies to sounding voices every block; pitch bend ±2 semitones; the punch-ins half / double / stop multiply it.
* **Start / end / loop.** A voice starts at `zone.start` (seeking: decodes at most one superblock), ends at `zone.end` (0 = end of the sample); if the slot has a loop (`flags & 1`, loop points in the pack) it wraps at `loop_end` to `loop_start`.
* **Reverse** (`zone.rev`, or the pack flag): plays from the end backwards, decoding a whole superblock when the position crosses into another one (so it costs more CPU than forward play).

## Per zone (per sample) settings: `le_zone_t`

| Field | Range | Meaning |
|---|---|---|
| `a d s r` | 0..127 | **ADSR**; times `1 ms .. 4096 ms` exponential (`2^(i·12/127)` ms), sustain 0..127 = 0..100 % |
| `lfo_rate lfo_depth lfo_shape lfo_target` | | rate 0.1..25.6 Hz exponential; shape 0 sine 1 triangle 2 square 3 saw; target 0 pitch (±12 semitones at depth 127), 1 volume (tremolo), 2 pan; evaluated once per block per voice |
| `vol` | 0..127 | 100 = unity |
| `mute` | 0/1 | a muted sample ignores new notes (`le_zone_cut` silences its sounding voices) |
| `rev` | 0/1 | reverse play |
| `oct` | -3..3 | base octave of a melodic sample (default -1 for melodic, 0 for kit) |
| `send[4]` | 0..127 | send to REVERB, DELAY, CRUSH, FILTER |
| `start end` | frames | played region (end 0 = to the end) |

Defaults (`le_zone_defaults`): A 0 (1 ms), D 64 (66 ms), S 127, **R 106 (about 1 s)**, LFO rate 40 depth 0, vol 100, no sends.
Envelope: Q24 level; the **release** is used by the key release; choke / voice limit use a **fast release of about 5 ms**. One-shot samples ignore the key release and play to their end.
Pack-level per sample controls (voices, choke group, glide) come from the pack (PACK_FORMAT.md): voice limit steals the oldest voice of that sample, a choke group cuts all voices of the group, glide is legato.

`le_sounding_keys(bank)` (control side, reads the voices) reports which keys of a zone are sounding; used for the key LEDs and the screen strip.

## Effects (all mono sends, added to both channels)

Four **send buses** (one per effect); each voice adds `output × send/127` to the buses. A bus is only processed while it has signal (and for 3 s after, so the tails ring).

* **REVERB**: Freeverb in small: four damped comb filters (lengths 1031, 1117, 1213, 1301) and two allpasses (225, 341), 10 KB. `p0` size = feedback 0.73..0.91, `p1` damp, `p2` return level.
* **DELAY**: mono, runs at half the sample rate (22 050 Hz, a lo-fi tape-ish echo), 22 000 samples (= 1 s) of 16 bit RAM (44 KB). `p0` time (1/16 .. whole note, tempo synced from the BPM, **clamped to 1 s**: at 140 BPM the 3/4 and whole-note settings are shorter than their label), `p1` feedback, `p2` tone (low pass in the feedback loop).
* **CRUSH**: bit depth `2 + p0·14/127`, sample-and-hold factor `1 + p1·31/127`, `p2` return.
* **FILTER**: Chamberlin state variable filter (coefficient table `f = 2 sin(π fc/fs)`, 40 Hz .. 10 kHz), `p1` resonance, `p2` type (< 43 low pass, < 86 high pass, else band pass).

**Punch-ins** (`le_punch(id, on)`, the FX screen keys): applied to the whole output after the effects: throws feed the dry mix into the reverb / delay buses; CRUSH (5 bit, hold 3) and DOWN (12 bit, hold 9); LPF / HPF (a state variable filter at fixed cutoffs); STUT8 / STUT16 / GATE gate the output on the beat clock (stutter = chopping, not a captured loop); HALF / DOUBLE change the speed, STOP slows to 1/16 with a fade and returns on release.

The output stage is a **soft knee** above 75 % of full scale (no hard clipping).

## Arpeggiator

`le_arp()` cfg: `on, rate (96/48/24/12 ticks), dir, range, gate`. Held keys (`le_arp_note_on/off`, up to 8, kept sorted) are stepped in time with the BPM (own tick clock that restarts when the first key of a chord goes down); each step plays one note through the normal note path (so it is **recorded** by the sequencer as if played) and releases it after `gate` % of the step. Range octaves add `n` keys (n = notes of the scale, 12 chromatic); if that key is empty the base note is used. Random uses an LCG.

## Sequencer (live loop recorder)

* **Clock:** 96 ticks per beat, `beats` per bar 2..7 (default 4), BPM 40-240 (default 140). The tick rate is `bpm·96/60` ticks/s accumulated per output frame in Q16; events fire at **block** resolution (2.9 ms). Measured bar length at 120 BPM: 88 192 frames against 88 200 ideal.
* **Transport:** `LS_STOP, LS_COUNTIN, LS_PLAY, LS_REC`. REC from stop: optional one-bar count-in (clicks), then recording from the pattern start. While playing REC punches in. REC again / PLAY while recording → PLAY (recording off, playback continues).
* **Shared clock.** `q.abs` counts ticks since the transport started; a pattern plays at position `abs mod its length`, so patterns of any length stay in step and **switching patterns while playing continues at the same point of the beat** (no restart).
* **Patterns:** 8, each 1/2/4/8 bars, up to **160 events** (note on/off pairs). Selecting or editing the length drops events beyond the end; changing the measure clears everything.
* **Recording:** note starts are quantised to the grid (OFF / 1/8 = 48 / 1/16 = 24 / 1/32 = 12 ticks, rounding to nearest); note ends keep the played length (shifted by the same amount), **at least 6 ticks**. Events recorded in the current pass are flagged "fresh" and not played back in that pass (so overdubbing does not double the notes you are playing); the flag is cleared when the loop wraps. Notes that sound across the loop point are released when a pattern is selected or the transport stops.
* **Metronome:** a 40 ms decaying square click (accent on the first beat of the bar). It sounds **only during REC (if METRO click is on) and during the count-in**, never during plain playback.
* Also available in the engine, not wired to a control: `le_seq_undo` (remove the events of the last REC pass).

## Saved data (written by the SAVE screen)

Two blobs, stored together in the last two flash sectors (PACK_FORMAT.md):

* **Loops** `'LFS2'`: `bpm u16, quant u8, metro u8, countin u8, beats u8`, then for each of the 8 patterns `bars u8, count u16` and `count` events of 5 bytes (`tick u16, note u8, velocity | on<<7, 0`).
* **Settings** `'LFST'`, version 2: `n_slots u8`, per slot 24 bytes (`a d s r lfo_rate lfo_depth lfo_shape lfo_target vol mute rev oct send0..3 start u32 end u32`), then the 16 fx parameters, the 5 arpeggiator bytes, the master level, scale and scale root. Imported only if `n_slots` matches the bound pack. The live speed, the punch-ins, the zone and the transport are not saved.

## Scales

`CHROM, MIX, MAJ, MIN, HMIN, PHRYG, MIN+` (steps in `scale_tab`), root C..B. The pack header gives the initial scale, the engine boots in **C minor**, GLO changes it live (and SAVE keeps it).

## CPU and memory (host estimates, not measured on the device)

* RAM of the engine: voices 8 × ~1.5 KB, delay 44 KB, reverb 10 KB, patterns 8 × 160 × 8 B = 10 KB, buses 2 KB. The firmware's `.ram0_bss` is 289 KB of 523 KB with everything.
* Decoding costs about 40 cycles per source sample; a voice at 1x needs 16 000 samples/s, at 8x 128 000. Reverse play decodes whole superblocks. The effects add roughly 60 operations per output sample while active.
