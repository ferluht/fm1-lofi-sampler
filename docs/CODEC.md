# The 2-bit codec

Files: `core/lofi_codec.h`, `core/lofi_dqt.inc`. The same header is the firmware decoder, the app's encoder (through JNI) and the host tools.

## Why a custom codec

The SDK of the FM-1 has a decoder (`fm1_sea_decode`) for the **SEA** format of the stock sampler, but it only accepts **5 or 6 bits** per sample and is fixed at 320-frame chunks of 228 bytes.
At 16 kHz that is 11.4 KB/s: about 46 s in the pack area. The lo-fi sampler wanted 2 bits, so it carries its own decoder (the stock one is MIT code and is generic in the bit depth, so the structure is the same: LMS predictor + 4 bit scale per group + a table of residuals).

## Format

* **16 kHz, mono, signed 16 bit input**, 2 bits per sample, no header per sample (the length is in the pack).
* A **group** is 20 samples. Per group: a 4 bit **scale** (0-15) and 20 two-bit **codes**.
* A **chunk** is 16 groups = 320 samples = 80 bytes of codes + 8 bytes of scales = **88 bytes**.
* A **superblock** is 2 chunks = **640 samples = 192 bytes**: `16 bytes seed | chunk 0 (88) | chunk 1 (88)`. **4.8 KB per second.**
* The **seed** is the predictor state at the start of the superblock: 8 × int16 (4 history samples, 4 weights, little endian). Every superblock therefore decodes independently of all the others: seeking, loop points, reverse play and scrubbing are cheap (at worst one superblock is decoded).
* Group data layout: group `g` of a chunk uses code bytes `g*5 .. g*5+4` (4 codes per byte, first code in the top two bits) and the scale nibble `chunk[80 + g/2]` (even groups in the high nibble).
* The length is rounded up to whole superblocks (zero padded); `lofi_encoded_bytes(frames) = ceil(frames/640) * 192`.

## Decoder (per sample)

```
prediction = (state[0..3] . state[4..7]) >> 13                         // 4 tap LMS, int64 accumulator
residual   = dqt[scale*4 + code]                                         // 16 scales x 4 levels
value      = clamp16(prediction + residual)
delta      = residual >> 4
state[4+i] += (state[i] < 0 ? -delta : delta)  for i in 0..3   (clamped to int16)     // sign-sign LMS weight update
history shifts, state[3] = value
```

The dequantiser table is `lofi_dqt[16][4]`: levels `+-round(w/2)` and `+-round(3w/2)` with `w = 2 * 1.75^scale` (min 1). It lives in RAM on the device (`LOFI_DQT_ATTR = FM1_AUDIO_DATA`) because audio runs while the flash is being programmed.
The decode functions are `always_inline` for the same reason: no call into flash code from the audio interrupt.

## Encoder (host / phone only)

For every group it tries **all 16 scales** (starting from the previous group's scale) and keeps the one with the smallest squared error plus a small penalty that keeps the LMS weights bounded; for every sample it picks the nearest of the 4 levels (analysis-by-synthesis, so the encoder tracks exactly what the decoder will reconstruct). Cost: about 16 × the decode work.
The encoder and decoder are **bit exact** (`core/test_core.c` checks that encode-then-decode equals the encoder's own reconstruction and that decoding superblocks in reverse order gives the same samples).

## Quality (measured, SNR against the 16 kHz low-passed original)

| Sample | 2 bit | 3 bit | 4 bit |
|---|---|---|---|
| Phonk bass | 52 dB | 60 | 67 |
| Shiny | 32 dB | 40 | 47 |
| Tape Key | 34 dB | 43 | 49 |
| TrapKit long 808 hit | 32 dB | | |
| TrapKit short hits (hi-hats, snares) | 9-12 dB (short hits start from silence, the predictor has no time to adapt) | | |

The 2 bit setting was **chosen by listening** (the user compared 2/3/4 bit mp3 renders and approved 2 bit, also for the short drum hits and for pitched-down playback to -4 octaves). The prototype that produced these numbers is `tools/lofi_codec.c` + `tools/run_test.sh`.

## Resampling to 16 kHz in the app

`AudioImport.resample`: windowed-sinc low-pass at 7.2 kHz (or 95 % of Nyquist for low rates) and decimation. WAV files are parsed by the app itself (16/24/32 bit int, 32 bit float, multichannel mixed to mono); everything else goes through `MediaExtractor` + `MediaCodec`.
