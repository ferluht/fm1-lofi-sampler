# Build and test

All commands are relative to the repository root (`~/fm1-lofi`). Tools used on this machine: `gcc`, `ffmpeg`, Python 3 (the PIL / numpy parts use `~/pcb-ai/.venv/bin/python`), Gradle 8.9 (`~/gradle/gradle-8.9`), Android SDK (`~/android-sdk`, NDK 27.2.12479018, CMake 3.22.1), the JieLi toolchain 2.5.3 installed under wine (`~/fm1/wine/drive_c/JL/pi32`), `tg-send` to push files to Telegram.

## 1. Host tests of the core (no hardware, seconds)

```sh
cd core
FLAGS="-O1 -DFM1_HOST_TEST -Wno-misleading-indentation -Wno-unused-function -I."
gcc -O2 -o test_core test_core.c -lm && ./test_core ../out/Shiny.raw                 # codec: encode/decode bit exact, random access
for t in test_scale test_zone test_seq test_fx test_defaults; do
  gcc $FLAGS -o $t $t.c lofi_engine.c -lm && ./$t | tail -1; done                     # engine tests
gcc $FLAGS -Wno-unused-result -o test_engine test_engine.c lofi_engine.c -lm && ./test_engine ../out/demo.lfp ../out/engine_test.wav   # renders a sweep
gcc -O2 -Wno-misleading-indentation -Wno-unused-result -o sim_upload sim_upload.c -lm && ./sim_upload   # protocol: packet loss, power-loss sweep
```

| Test | What it proves |
|---|---|
| `test_core` | encoder and decoder are bit exact; superblocks decode in any order; SNR |
| `test_scale` | scale mapping from F (C minor F G Ab Bb C D Eb, D minor, C major, chromatic), kit key ignores scale |
| `test_zone` | voice limit, choke groups, glide, unlimited zones |
| `test_seq` | quantise, gate length, loop playback, undo, import/export, overdub, clear, tempo accuracy, patterns stay in step |
| `test_fx` | ADSR, volume, mute, LFO pan / pitch, start / end, reverse, the four sends (tails), filter, punch-ins (half speed, double, stutter), arpeggiator, measure, settings round trip |
| `test_defaults` | C minor / 140 BPM at boot, release about 1 s, melodic zone starts at octave -1 and its 27 keys all differ, base octave, metronome silent in PLAY and audible in REC |
| `test_engine` | renders a script (note, octave, live speed sweep, polyphony) to a wav; used for listening |
| `sim_upload` | upload of the real pack at 0 / 10 / 20 / 30 % loss with duplicates, replacing a pack, **a flash failure injected at every flash operation** |

`test_engine` was also compared with an independent numpy Hermite resampler: 58.6 dB.

## 2. End-to-end upload test

`node web/test/test_e2e.js` runs the JS uploader against the C device (`core/dev_server`) at 0 / 10 / 25 % loss.

## 3. The firmware

```sh
python3 tools/build_fw.py firmware ~/fm1/wine/drive_c/JL/pi32
# → firmware/build/dx7/generated/FM1_OFFICIAL_OPEN.fwsc and the flash page firmware/build/FM1-Lofi-Workbench.html (about 30 s)
# prints "OK  app image N bytes (limit 453921)"
```

It is a Linux/wine port of the vendor's PowerShell build (`sdk/Build.ps1`): compiles every `app/*.c` with the JieLi clang 4.0.1 (`-target pi32v2 -mcpu=r3 -flto`), links with the vendor linker script and the closed library, packs the update, wraps it into the flash page (with the stock V15 as rollback). To flash: open the page in a browser with Web MIDI, connect to the FM-1 and follow the page.
Compile warnings about `-Wframe-larger-than=2560` or section conflicts are real errors for the device (see FIRMWARE.md gotchas).

### Host harness of the real UI code

```sh
cd firmware
gcc -O1 -g -DFM1_HOST_TEST -Wno-misleading-indentation -Wno-unused-result -Wno-unused-function \
    -Isdk/include -Iapp -Iapp/synth -Iapp/board -Ihosttest -o hosttest/lofi_host hosttest/lofi_host.c app/lofi_engine.c -lm
cd hosttest && ./lofi_host ../../out/demo.lfp          # 25+ PASS lines, "ALL CHECKS PASSED", writes shot_*.ppm of every screen
```

Screenshots: `shot_*.ppm` (240×240); the sheets in `docs/img/` were made with a few lines of PIL (`Image.open(...).resize((360,360), NEAREST)` pasted in a grid).


## 4b. The web editor

```sh
python3 web/build.py          # -> web/dist/index.html and releases/FM1-Lofi-Editor.html
node web/test/test_codec.js && node web/test/test_pack.js && node web/test/test_e2e.js && node web/test/ui_test.js
```
See WEB.md. `ui_test.js` needs the headless Chrome of Playwright (`~/.cache/ms-playwright/chromium_headless_shell-*`), `ffmpeg` and `core/dev_server` (built by `tools/run_e2e.sh`).

## 5. Making packs and test material on the host

```sh
# 16 kHz mono raw from any audio
ffmpeg -y -i in.wav -ac 1 -ar 16000 -af "lowpass=f=7000" -f s16le sample.raw
# a pack: file:lo:hi:root:flags (note ids = zone*32+key; root = pitch class for melodic samples), scale via environment
gcc -O2 -DFM1_HOST_TEST -Icore -o core/mkpack core/mkpack.c
A=samples/demo; args=""; for i in 1 2 3 4 5 6 7 8 9; do k=$((i-1)); args="$args $A/hit$i.raw:$k:$k:$k:4"; done
LFP_SCALE=4 LFP_ROOT=0 core/mkpack out/demo.lfp DEMO $args $A/tape_key.raw:32:58:0:0:0:4294967295:-14 $A/shiny.raw:64:90:0:0:0:4294967295:-2 $A/phonk.raw:96:122:0:0
# slice a drum kit WAV into hits by onsets
~/pcb-ai/.venv/bin/python -I tools/slice_kit.py samples/TrapKit.wav slices
# codec listening test (2 / 3 / 4 bit renders of the samples) and the pitch sweep renders
tools/run_test.sh ; python tools/pitch_test.py out/Shiny_b2.raw out/pitch/Shiny.wav
```

## 6. Release checklist used so far

1. core tests, `sim_upload`, `web/test/*.js` (all green).
2. `build_fw.py` → check the image size, run the host harness (all checks), look at the screenshots.
3. copy the page to `releases/FM1-Lofi-Workbench.html`.
4. Update `docs/CHANGELOG.md` (and these docs if behaviour changed).
