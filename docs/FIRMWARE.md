# The firmware

`firmware/` is a copy of the open FM-1 firmware as used by the sibling project **fm1-daw** (which is a fork of M-VAVE's `fm1_open`). The vendor SDK and the closed engine library `sdk/lib/libfm1_engine.a` (Bluetooth stack, USB, MIDI routing, flash driver, the stock instrument and sequencer) are unchanged. What this project changed is in `firmware/app/`.

## What replaces what

| Stock | Here |
|---|---|
| `app/screen.c` (the stock UI, 3000 lines) | **`app/lofi_ui.c`**: panel, screens, MIDI-in hook, upload glue, persistence (the library calls `groove_screen_init/control/tap/mmc`, which this file provides) |
| the instrument engine of the library (`fm1_engine_render`) | **`app/lofi_engine.c`** called from `app/board/audio.c` (`le_render(rendered, 128)`); the library's engine is still linked but never rendered |
| stock project / sample transfer (`transfer.c`) | `fm1_app_sysex` only forwards frames of the lofi protocol (`lofi_sysex`); everything else is ignored, because the whole user flash now belongs to the pack |
| project / settings save-restore (`project.c`) | `groove_project_init/poll` no longer restore or write projects; `poll` only configures **BLE MIDI** (`fm1_bluetooth_configure(FM1_BT_MIDI)`, peripheral role) |
| `midi_router.c: fm1_app_midi_received` posts to the stock instrument | forwards notes, bend and CC 20 to `lofi_midi` (a queue read by the control task) |

Copies of `core/` headers and the engine are kept in `firmware/app/` (`lofi_codec.h`, `lofi_dqt.inc`, `lofi_pack.h`, `lofi_proto.h`, `lofi_sender.h` (host harness only), `lofi_engine.[ch]`): **after editing `core/` copy the files over** (`cp core/lofi_*.h core/lofi_dqt.inc core/lofi_engine.c firmware/app/`).

## Control flow

* The library's control task calls `groove_screen_control()` repeatedly (and when `fm1_control_request()` is called, which the SysEx / MIDI hooks do). It must stay **short**: the keys, encoders and buttons are read once per call.
* The audio interrupt (every 128 frames) runs `le_render`.
* The routing task (BLE / USB / DIN MIDI) calls `fm1_app_sysex` and `fm1_app_midi_received`; both only copy into queues (`rxq` 40 frames, `midiq` 32 messages) that the control task drains. **No flash, screen or audio work happens in those callbacks** (SDK rule).

## `groove_screen_control` in order

1. read the panel; the master knob goes to `groove_master_update` (without it the output gain stays 0 and the jack is silent); `groove_project_poll` (Bluetooth bring-up);
2. `protocol_pump`: initialise the upload device from the flash when it is ready, process queued SysEx frames (up to 8 per call), send replies (retry when the port queue is full); while frames keep coming the **upload screen** replaces the instrument (it ends 1.5 s after the last frame);
3. MIDI input events;
4. one step of the **waveform analysis** (2 superblocks per call);
5. buttons: page buttons (the active one again → HOME), OCT-/OCT+ (zone), PLAY, REC;
6. encoders: SELECT, ALG (zone for the FX send), K1..K4 → `page_select` / `page_adjust`;
7. keys → `key_press` / `key_release` (what a key does depends on the page: play, mute, punch-in, select pattern; the decision is remembered per key so the release matches even if the page changes while held);
8. redraw (when dirty and the screen queue is empty), the 100 ms background work (header, LEDs, strip keys, playhead), then **flush a bounded amount of drawing**.

## The screen queue (why the keys are not late)

A redraw used to run inside the control task: up to **385 calls / 92 573 pixels** of SPI traffic in one tick (one blit per character, 220 fills per waveform, full-screen clears), so the keys were read late and felt laggy. Now:

* every drawing helper (`gfx_fill`, `draw_text`) only **enqueues** an operation (768 entries) with its pixels in a 12 288-pixel pool; text is rendered into the pool and sent as **one blit per string**;
* each control tick `gfx_flush(150)` executes operations while the cost budget (`4 + pixels/64` per operation) is not used up, and big fills are split into 3000-pixel bands, so one tick draws **at most about 40 calls / 11 500 pixels**;
* a page redraw enqueues the tiles first (they change while a knob is turned), then the header and the picture; the picture is redrawn only when the changed parameter affects it (`dirty` bit 2); a new page request resets the queue; while a redraw is still being flushed a new request waits until the queue is empty (no starvation, no flicker of half drawn pages);
* held keys, the sequencer playhead and the upload percentage are updated by tiny operations.

The host harness asserts the per-tick limits. The real SPI speed and the control task period of the device have not been measured.

## Screens and tiles

`page_tiles(page, tile[4])` builds name / value / bar for K1..K4, `page_adjust(page, k, delta)` edits, `draw_vis()` draws the picture of the page. All parameters are read and written through `le_*` / `le_zone()`; the only UI state is `page, az (active sample), bank, fx_sel, seq_scroll, unsaved` and the key bookkeeping. See USER_GUIDE.md for what each tile does.

## LEDs

Only the LED of the open page button is on (FX..SEQ), PLAY is lit while the transport runs, REC while recording (blinking during the count-in). Key LEDs (`leds_refresh`, `led_force` writes all 41 the first time): the filled keys of the zone (SEL: only the audible ones; FX: the 12 punch-in keys; SAVE: the patterns with notes + the selected one) plus every key that is down. The FM-1 LEDs are **single colour on/off**; colours of the samples appear on the screen (the 27-key strip) and in the app.

### Sounding keys
`le_sounding_keys(bank)` returns the keys of the zone that have a sounding voice (key held, sequencer, arpeggiator or MIDI note; released tails do not count). Every control tick `groove_screen_control` compares it with the previous mask and, on a change, flips those LEDs (`leds_refresh`) and redraws the keys of the screen strip.

## Persistence (`tail_save` / `tail_load`)

The last two flash sectors hold `'LFPS'`, the pack CRC, lengths, a CRC and the loops + settings blobs (ENGINE.md). Loading happens right after the pack is bound (`bind_pack`) and only if the CRC matches. `tail_save` refuses to run while recording or counting in (the audio side is writing events). The write erases and programs both sectors, which **briefly silences the audio** (SDK behaviour).

## Build constraints and gotchas

* **Image limit 453 921 bytes** (the `fm1_open` layout); the current build is **447 404** (6.5 KB free). Adding features means removing others or compiling more with `-Oz` (see `OZ` in `tools/build_fw.py`).
* RAM: `ram0` is 523 596 bytes; `.ram0_bss` is 289 KB with this firmware (the Bluetooth stack's `malloc` heap needs are unknown, watch for it).
* **`FM1_AUDIO_DATA` is for const tables only.** A mutable variable in that section fails the link ("section type conflict with lofi_dqt"); mutable engine state is in the engine's BSS struct.
* Everything called from the audio interrupt must be `FM1_AUDIO_CODE` or `always_inline` (the codec functions are `LOFI_INL`), and must not read the flash with `fm1_flash_read` (the sample data is read through the **mapped** pointer `fm1_flash_map`, which is allowed).
* `fm1_flash_read/write/erase` are for the **control task only** (not from audio, timer, MIDI or SysEx callbacks).
* The flash page (`FM1-Lofi-Workbench.html`) is produced by `tools/build_fw.py` + `make_workbench.py`; it uses the stock update product id `FM-1_8000014` (the version shown by the updater is V14) and embeds the stock V15 firmware as a rollback.
* The SDK's decoder only supports 5/6 bits; the 2-bit decoder is ours (see CODEC.md).

## Host harness

`firmware/hosttest/lofi_host.c` includes the real `lofi_ui.c` and `lofi_engine.c` with stubs of the SDK functions (screen into a framebuffer, controls from a script, flash in an array, SysEx replies back to the sender). It uploads the DEMO pack through `lofi_sysex` with the reference sender, walks all pages, checks LEDs, mutes, punch-ins, zones, transport rules, a power cycle (loops + settings come back) and the per-tick drawing limits, and writes `shot_*.ppm` of every screen. 25+ checks, all passing. Run it as described in BUILD_AND_TEST.md.
