# Architecture

## The hardware (what limits the design)

| Fact | Value | Consequence |
|---|---|---|
| CPU | JieLi **PI32v2** (`-mcpu=r3`), fixed-point friendly, no real-time OS in our code | all DSP is integer; load is unmeasured |
| Program flash | 1 MiB total; our app image must be **<= 453 921 bytes** | code is tight (6.5 KB free) |
| User flash | **536 576 bytes = 131 sectors of 4096** (SDK variant) | holds the pack, the loops, the commit records; erase = a whole sector; **writing/erasing silences the audio** |
| RAM | `ram0` 523 596 bytes (code of the audio interrupt runs from RAM, `FM1_AUDIO_CODE`) | 289 KB BSS in use |
| Audio | **44 100 Hz stereo, blocks of 128 frames**, Q23, interrupt driven | 2.9 ms event granularity; `le_render` must be fast |
| Screen | 240 × 240 RGB565, SPI, drawing is synchronous | all drawing goes through a budgeted queue |
| Panel | 27 keys (**first key = F**), 14 buttons, 7 encoders, master knob, 41 single colour LEDs | LEDs are on/off only |
| Radio | BLE MIDI (peripheral), USB MIDI, DIN MIDI; SysEx frames up to 1024 bytes in, our frames <= 192 | the upload protocol |
| Engine | **closed library** with the stock instrument, sequencer, BLE stack, flash driver | we keep the platform parts and replace the sound and the UI |

## Data flow

```
 phone app (Java)                                   FM-1 firmware
 ┌────────────────────────┐                    ┌─────────────────────────────────────────┐
 │ audio file             │                    │ routing task: fm1_app_sysex / midi hooks│
 │  AudioImport (16 kHz)  │                    │        │ queues (no work here)          │
 │  WaveView slicing      │   BLE / USB MIDI   │        ▼                                │
 │  Pack/Slot zones       │ ── SysEx frames ─▶ │ control task: groove_screen_control     │
 │  Lofi (JNI):           │   (lofi protocol)  │   protocol_pump → lf_dev_handle → flash │
 │   encode 2 bit, build  │                    │   UI pages, keys, LEDs, screen queue    │
 │  Uploader              │                    │        │ event ring (le_note_on, ...)   │
 └────────────────────────┘                    │        ▼                                │
                                               │ audio interrupt: le_render(128 frames)  │
 core/ (shared C)                              │   voices ← fm1_flash_map (2-bit data)   │
 codec · pack · protocol · engine              │   fx buses, arp, sequencer, soft limit  │
                                               │        ▼ fm1_output_mix → DAC           │
                                               └─────────────────────────────────────────┘
```

The same C files are compiled three ways: into the firmware (with the JieLi clang), and into the host tests (gcc). Only the platform glue differs, so what the tests verify is the code that ships.

## Design decisions and why

* **Own audio engine in `audio.c`.** The vendor engine lives in a closed library whose sampler is fixed (8 slots, 5-bit SEA, ~20 s). The firmware app still owns `audio.c` (open source), so `le_render` replaces the engine call there; the library keeps doing Bluetooth, USB, MIDI routing and flash.
* **16 kHz, 2 bit.** Chosen by listening to 2, 3 and 4 bit renders of the actual material (pitched samples and short drum hits) and pitch-sweeps down to -4 octaves: 2 bit was accepted. It gives about 108 s per pack instead of 20 s.
* **Independent superblocks (a seed every 640 samples).** 4 % bigger than a stream with one seed per sample, but any position can be decoded from one superblock: loop points, start, reverse and fast scrubbing need no search.
* **Streaming from mapped flash, nothing preloaded in RAM.** The voices decode on the fly (`fm1_flash_map` pointer, allowed in the audio interrupt). 100 s of audio never has to fit in the 523 KB RAM.
* **Cubic Hermite resampling**, speed from 1/16x to 8x. The user's requirement was a live speed range of -4 / +1 octave; melodic zones need pitches up to three octaves above the sample.
* **Note ids, zones as banks of 27 keys.** The user's mapping idea: OCT- / OCT+ switch zones (the whole keyboard), a zone is a kit or one melodic sample. Encoding it as `zone × 32 + key` keeps one byte per key reference everywhere (pack, sequencer events, MIDI up to zone 3) and lets the pack format stay the plain range table (`lo..hi`).
* **Scale follows the FM-1 keyboard that starts at F** (key 1 = the first scale note at or above F), and the melodic zone starts one octave below the sample, so the whole 27 keys stay inside the pitch range.
* **SysEx upload with a bitmap per 4 KB sector** instead of per-block acknowledgements: BLE round trips are the slow part. The device is the authority: it only accepts the sector it expects and commits through an A/B record with CRCs, and BEGIN invalidates the old pack, so power cuts and aborted uploads can never leave a half pack.
* **Everything the user can change on the device is saved only by SAVE** (flash writes silence the audio, so no silent auto-saves while playing); `*` marks unsaved state.
* **One redraw queue** for all drawing (see FIRMWARE.md) after a measurement showed single ticks of 385 calls / 92 573 pixels.
* **Sequencer on a shared transport clock** so that patterns of different lengths and pattern switching stay in step.
* **Metronome only while recording** (and the count-in), so it does not disturb playing.
* **No colours on the LEDs.** They are single colour on/off hardware. Zone colours are on the screen and in the app. (Brightness by software PWM in the 10 kHz scan interrupt was considered: ~900 Hz frame rate would allow 4 levels; not done.)

## History in one paragraph

The project started from the question "how many seconds of samples fit on the FM-1" (19.55 s with the stock SEA bank). The answer drove a new firmware: custom 2-bit codec (prototype, listening tests), pack format and a resumable SysEx upload with power-loss tests, the engine (voices, scales, zones, then envelope / LFO / effects / arpeggiator / sequencer), the firmware UI (first a list, then the ten-page menu), the Android app (import, slicing, zones, upload), then fixes from the user's trials on the FM-1 (key lag, pattern sync, melodic range, zones as banks, defaults). Details and numbers per step are in CHANGELOG.md.

## Reused and external parts

* The FM-1 open firmware and SDK (M-VAVE, GPLv3 with linking exception; closed `libfm1_engine.a` under its own terms).
* SEA decoder structure (MIT, Dani Biro / M-VAVE).
* The fm1-daw firmware tree (build script `tools/build_fw.py`, flash page maker) as the base.
* No LOOPA code is used. (The demo kit samples come from LOOPA's resources as *data*.)
