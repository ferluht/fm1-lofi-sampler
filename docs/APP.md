# The Android app

Package `com.ferluht.lofi`, Java, minSdk 23, targetSdk 30 (the same install quirks as the sibling apps: v1 + v2 signing), NDK 27 JNI of the C core. No libraries beyond the platform (the UI is built in code, no layouts). About 1 MB with the DEMO samples.

## Files (`android/app/src/main/java/com/ferluht/lofi/`)

| File | Role |
|---|---|
| `MainActivity` | all screens (programmatic views), dialogs, permissions, file pickers |
| `Pack`, `Slot` | the model: a pack of up to 32 slots (samples), zone (bank) logic: `fits`, `placeKit`, `placeMelodic`, `autoLayout`, `bankKind`, `build()` |
| `Store` | packs on disk, the one-time DEMO pack, migration of old packs |
| `BankView` | the zone map widget |
| `WaveView` | waveform + slice markers |
| `AudioImport` | decode any audio → 16 kHz mono; auto slicing (`autoSlice`) |
| `Lofi` | JNI wrapper (`buildPack`, `roundTrip`, `encodedBytes`) + scale / note names + capacity constants |
| `LofiProto`, `Uploader` | the upload protocol (pure Java, runs in the JVM tests) |
| `MidiLink` | USB / BLE MIDI connection (`android.media.midi`), BLE scan, SysEx reassembly |
| `cpp/lofi_jni.c` | JNI glue of `core/lofi_pack.h` + `lofi_codec.h` (the code the firmware decodes) |

## Screens

1. **Packs**: list with size bars (`kb used / 508 KB`), NEW PACK. On the first start a **DEMO** pack is created from the files in `assets/demo/` (nine TrapKit hits in zone 1, Tape Key / Shiny / Phonk bass in zones 2-4, C minor).
2. **Pack**: name (tap to rename), capacity bar, slots (name, zone and key, length, size, ▶ preview, tap to edit), ADD SAMPLE, ZONES, UPLOAD, EXPORT .LFP, DELETE PACK.
3. **ADD SAMPLE → slicer**: pick an audio file (decoded in the background), then
   * tap the waveform = new slice marker, drag = move, long press = delete, tap the lower strip = pick a slice, ZOOM +/−, a pan bar;
   * **AUTO** slices by onset detection (sensitivity slider; 4 ms RMS envelope, a hit starts when the level jumps over the quiet floor, min gap 90 ms), CLEAR;
   * **PLAY** plays the picked slice **through the real 2-bit codec** (the same code the FM-1 decodes), so you hear what the device will play;
   * **ALL → KIT** adds every slice as a kit hit (next free kit keys, new zones as needed), **SELECTED → HIT** adds one, **SELECTED → KEYS** adds the picked slice as a melodic sample in an empty zone (27 keys). Trailing silence is trimmed.
4. **ZONES**: the `BankView` (one row per zone, 27 cells; kit hits are single cells, a melodic sample spans the zone), scale buttons (OFF CHROM MIX MAJ MIN HMIN PHRYG MIN+) and root buttons, ◀ KEY / KEY ▶ / ZONE ▲ / ZONE ▼, PLAY, EDIT, AUTO LAYOUT. Drag a sample to another key or zone, drag the end cells of a melodic sample to resize, tap an empty cell to move the selected sample there, long press = edit. A red outline marks an invalid placement.
5. **Sample dialog**: name; **zone / first key / last key** (1-27; equal = kit hit, different = melodic sample and the zone then holds nothing else); **pitch** of the sample (melodic: its note name, used to place the keys); loop on/off with start / end (ms); one-shot; **voices** (0 = unlimited, 1 = mono), **choke group**, **glide** (ms); preview speed -4..+1 octave; SAVE / DELETE.
6. **Upload**: lists USB MIDI devices and scans for Bluetooth LE MIDI devices (service `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`); tap the FM-1; progress dialog; result message.

The envelope, LFO, start / end of a sample, reverse, mute, effects, arpeggiator, patterns are **edited on the FM-1**, not in the app.

## Storage

`files/packs/<id>/pack.json` + `slotN.pcm` (16 kHz mono int16 little endian, the *uncompressed* material, so a pack can be edited and re-encoded). JSON: name, `model` (2), scale, scale root, and per slot name / lo / hi / root / flags / loop points / voices / choke / glide. **Packs saved by an earlier version** (`model` missing = the old note-range model) are converted once when loaded: the pitch is kept (`root mod 12`) and the layout is rebuilt by `autoLayout` (kits first, then one melodic sample per zone); the old key arrangement is lost.
A pack can be exported as `.lfp` (the exact image that would be uploaded). There is **no import of `.lfp`** (the image holds 2-bit data; the PCM would have to be decoded back).

## Permissions

`BLUETOOTH`, `BLUETOOTH_ADMIN`, `ACCESS_FINE_LOCATION` (up to API 30, needed for BLE scanning), `BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT` (API 31+). Requested when UPLOAD is pressed. The MIDI feature is required (`android.software.midi`).

## What is verified

* The app compiles and packages (APK builds for arm64-v8a and armeabi-v7a); the JNI code is the same C as the host tests but the **glue has not run**.
* `Uploader`, `LofiProto` are run in a plain JVM against the real C device (`tools/run_e2e.sh`): 6 seeds × 0 / 10 / 25 % loss, the image arrives identical and survives a "reboot".
* Not run on a phone: every screen, file import (`MediaCodec`), the real MIDI / Bluetooth connection, permissions.
