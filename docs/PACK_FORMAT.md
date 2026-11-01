# Pack format and flash layout

Files: `core/lofi_pack.h` (build, parse), `core/mkpack.c` (command line builder), `web/src/pack.js` (the editor side model).

## The pack image

A pack is **one flat image**, uploaded as is and stored from the first data sector of the flash. All integers are little endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `LFP1` |
| 4 | 2 | version = 1 |
| 6 | 2 | number of slots, 0..32 |
| 8 | 16 | pack name (ASCII, zero padded, 15 chars) |
| 24 | 4 | total frames of all slots |
| 28 | 1 | **scale**: 0 off, 1 CHROM, 2 MIX, 3 MAJ, 4 MIN, 5 HMIN, 6 PHRYG, 7 MIN+ |
| 29 | 1 | **scale root** pitch class 0..11 (C = 0) |
| 30..63 | | reserved (0) |
| 64 | 32 × 32 | **slot table**, one entry per slot (below) |
| 1088 | 32 × 2 | **zone extras**, one entry per slot (below) |
| 1152 | | slot data, one after the other, each `lofi_encoded_bytes(frames)` bytes (whole 192-byte superblocks) |

### Slot table entry (32 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `offset` of the slot's superblocks from the start of the image (>= 1152) |
| 4 | 4 | `frames` (16 kHz samples) |
| 8 | 4 | `loop_start` (frame) |
| 12 | 4 | `loop_end` (frame; `0xFFFFFFFF` = no loop) |
| 16 | 1 | `lo` note id of the first key |
| 17 | 1 | `hi` note id of the last key (`lo == hi`: a kit key) |
| 18 | 1 | `root`: melodic sample = **pitch class of the sample** (0..11); a kit key = its id (unused) |
| 19 | 1 | flags: bit 0 loop on, bit 1 reverse by default, bit 2 **one-shot** (ignore the key release) |
| 20 | 12 | name (11 chars + 0) |

### Zone extras (2 bytes per slot, at 1088 + 2·i)

* byte 0: low nibble = **max voices** of the sample (0 = unlimited, 1 = mono; when full the oldest voice of that sample is cut), high nibble = **choke group** (0 none, 1..15; a key of the group cuts every sounding voice of every sample of the group).
* byte 1: **glide** time in units of 10 ms (0 = off). With glide a new key while one is held slides the existing voice to the new pitch (legato, no retrigger); a new voice also glides in from the last pitch that sample played.

`lfp_parse` checks the magic/version, that every slot's data lies inside the image and that loop points are sane.

## Note ids, zones, kits and melodic samples

The engine does not use MIDI note numbers for the keys but **note ids**: `id = zone × 32 + key`, key 0..26, zone 0..7 (so ids 0..250). The panel plays the zone chosen with OCT- / OCT+; MIDI notes 0..127 address zones 0..3 directly.

* **A zone is a kit or one melodic sample.** A kit zone has slots with `lo == hi`, at most one per key. A melodic zone has exactly one slot with `lo < hi` (normally key 0..26, all 27 keys) and nothing else. The firmware does not enforce this (it plays whatever the ranges say), the app does (`Pack.fits`).
* **Melodic pitch.** Key `i` of the zone (counted from `lo`) plays at `shift + semis(i) + 12·oct` semitones relative to the stored sample:
  * `semis(i)`: the keyboard of the FM-1 starts at **F**, so key 0 is the first note of the scale at or above F. With a scale the keys are the next notes of the scale (`semis = 12·(i/n) + r[i mod n]`, `r` = the scale's pitch classes counted from F, sorted); without a scale they are chromatic (`semis = i`).
  * `shift`: the distance from the sample's pitch class (`root`) to the **nearest F**, in -6..+5 semitones.
  * `oct`: the zone octave, default **-1** for melodic zones, -3..+3 (edited on HOME, saved in the settings).
  * the voice speed is limited to 1/16x .. 8x of the stored rate, so very high keys of a very high sample saturate.
* **Kit keys** (`lo == hi`) always play at their own pitch (offset 0).

## Capacity

The pack area is **127 sectors × 4096 = 520 192 bytes** (header 1152 bytes included): about 108 s of 2-bit audio at 4.8 KB/s, e.g. the DEMO pack uses 159 936 bytes = 33 s. Slots are limited to 32.

## Flash layout (user partition of the firmware: 536 576 bytes = 131 sectors)

| Sectors | Content | Written by |
|---|---|---|
| 0, 1 | **commit records** (see below) | upload protocol |
| 2 .. 128 | pack image (127 sectors) | upload protocol |
| 129, 130 | **loops + settings** of the pack (see ENGINE.md) | the SAVE screen |

The size 536 576 is fixed by the SDK variant (`sdk/Build.ps1`: `user_bytes = 536576`); the firmware image itself must stay under 453 921 bytes.

### Commit record (32 bytes, sectors 0 and 1 alternate by generation)

`'LFCM' | generation u32 | state u32 (0 empty, 1 pack) | pack size u32 | pack CRC-32 u32 | 8 bytes 0 | record CRC-32 u32 (of the first 28 bytes)`.
The record with the highest valid generation wins. A new record goes to sector `generation & 1`, so a power cut while it is written leaves the previous record intact.

### Loops + settings (sectors 129-130, 8192 bytes)

`'LFPS' | pack CRC-32 u32 | loops length u16 | settings length u16 | CRC-32 of the payload u32 | payload` where the payload is the loops blob followed by the settings blob (formats in ENGINE.md). They are ignored if the pack CRC does not match, so uploading another pack starts clean.

## Building packs

* In the app (normal way).
* On the host: `core/mkpack out.lfp NAME file.raw:lo:hi:root:flags[:loop_start:loop_end] ...` (raw = 16 kHz mono s16le), `LFP_SCALE` / `LFP_ROOT` environment variables set the scale. Example (the DEMO pack): see `docs/BUILD_AND_TEST.md`.
* From C: `lfp_build_ex(name, scale, scale_root, inputs, n, &bytes)`.


## Fine tune (v0.9)
Slot table byte 31 (the last byte of the 32-byte entry; the name uses bytes 20..30 and is NUL padded) = `i8 tune`, cents added to the playback pitch of the slot (the editor sets it to -(deviation of the sample from `root`)). Old packs have 0 there.
