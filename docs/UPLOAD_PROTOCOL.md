# Pack upload protocol (SysEx over BLE MIDI / USB MIDI)

Files: `core/lofi_proto.h` (framing + the **device state machine**, with the flash abstracted), `core/lofi_sender.h` (C reference sender), `android/.../LofiProto.java` + `Uploader.java` (the phone side, same logic), `firmware/app/lofi_ui.c` (glue: queue, flash functions, `protocol_pump`).
Tests: `core/sim_upload.c`, `web/test/test_e2e.js` (see BUILD_AND_TEST.md).

## Why SysEx

The FM-1 exposes BLE MIDI (as a peripheral, switched on at start-up by `groove_project_poll`) and USB MIDI, and the SDK hands complete SysEx frames to `fm1_app_sysex` (control task context, at most 1024 bytes). The stock firmware limits its own frames to 192 bytes and the SDK output queue to 128 bytes; this protocol keeps to both (frames <= 192 bytes, replies < 40 bytes). The manufacturer id is `7D` (non-commercial) with the tag `'L' 'F'`, different from the stock transfer (`'F' 'M'`) and the DAW protocol (`'D' 'A' 'W'`).

## Frame

```
F0 7D 'L' 'F' 01 cmd seq_lo seq_hi  <payload packed 8->7 bit, LSB first>  checksum F7
```

* `seq` is 14 bits (two 7-bit bytes); the reply echoes it, replies have `cmd | 0x40`.
* The payload is raw bytes packed into 7-bit MIDI data (`n` raw bytes become `ceil(8n/7)`); the checksum makes the sum of the bytes from `7D` up to and including the checksum 0 mod 128.
* A DATA frame is 10 + 152 = **162 bytes**.

## Commands (payloads little endian)

| cmd | name | payload | reply payload |
|---|---|---|---|
| 1 | HELLO | - | `status, ver(1), data_sectors u16, state u8 (0 empty, 1 pack, 2 uploading), pack_size u32, pack_crc u32, generation u32, next_sector u16` |
| 2 | BEGIN | `pack_size u32, pack_crc u32` | `status, next_sector u16, sector_count u16` |
| 3 | DATA | `sector u16, block u8, 128 bytes, crc16 u16` | **none** |
| 4 | QUERY | `sector u16` | `status, sector u16, done u8, bitmap u32` |
| 5 | END | `pack_crc u32` | `status` |
| 6 | ABORT | - | `status` |
| 7 | DELETE | - | `status` |

Status: 0 ok, 1 no space, 2 bad argument, 3 wrong state, 4 CRC mismatch, 5 flash I/O error.
CRC-32 is the usual reflected one (poly `EDB88320`), CRC-16 is CCITT (poly 1021, init FFFF) over `sector, block, data`.

## Device state machine (`lf_dev_handle`)

* **BEGIN** first **invalidates** the current pack (it writes an *empty* commit record), then starts a new upload (`state = uploading`, `next = 0`). If a BEGIN with the same size and CRC arrives while uploading, it is a **resume**: the reply tells the sender which sector to continue from.
* **DATA** is accepted only for `sector == next_sector` and only while uploading; its CRC-16 must match. Blocks are collected in a 4 KB RAM buffer; when all blocks of the sector (32, fewer in the last) have arrived the sector is **erased and programmed** and `next_sector` advances. No reply is sent (bandwidth); wrong frames are silently dropped.
* **QUERY(sector)** answers whether the sector is already written (`done = 1`) or which blocks of the sector in progress have arrived (bitmap).
* **END(crc)** requires `next_sector == total`; the device reads the whole image back from flash, checks the CRC-32 against the one given in BEGIN, and writes the commit record `state = pack`. A repeated END of the already committed pack (same CRC) is answered **ok**: a lost reply to END can be retried safely (this was a real bug found by the loss tests).
* **ABORT / DELETE** drop an upload / the pack (writes an empty commit record).

## Sender algorithm (`lf_send_pack`, `Uploader.send`)

1. HELLO (retry up to 12 times with 500 ms timeouts) → gets `data_sectors`; the pack must fit.
2. BEGIN → `next_sector`, `total`.
3. For each sector from `next_sector`: send all missing blocks back to back (3 ms apart in the app), then QUERY; the reply says what is missing; resend only those (up to 40 rounds), until `done`.
4. END.

Requests with replies are retried with the same `seq`; replies with another `seq` are ignored.

## Reliability and speed

* No per-block acknowledgements: one QUERY per 4 KB sector. A DEMO pack (159 936 bytes) is 1250 blocks.
* Tested by a lossy link simulation (drops requests and replies, duplicates delivery): at 0 / 10 / 25 / 30 % loss the image arrives bit-identical; re-sent blocks at 25 % loss: about 420 of 1250.
* The real speed depends on BLE (connection interval, MTU, write without response). Estimated 1-3 minutes for a full 500 KB pack; **not measured on hardware**. USB MIDI is faster.
* The receiving side is a queue of 40 frames (7.8 KB); a burst of a whole sector (32 frames) fits. A frame arriving when the queue is full is dropped and re-sent by the bitmap mechanism.
* Writing a sector silences the audio briefly (SDK behaviour), so the FM-1 shows the upload screen and the instrument should not be played during an upload.

## Power-loss safety

* The commit record sectors 0/1 alternate by generation and carry a CRC: a cut during a commit leaves the previous record valid.
* Because BEGIN writes the *empty* record first, an upload that is interrupted at any point leaves the bank **empty or complete**, never half written. `core/sim_upload.c` injects a flash failure at **every** flash operation of an upload (10 operations) on a device that holds an old pack: the result is always empty, the old pack intact (failure before BEGIN), or the new pack complete: 0 corrupt states.
* The simulated flash enforces real flash rules (programming only clears bits, erase before write; any violation aborts the test).
* Resume across a device restart is **not** supported (the progress is kept in RAM): after a power cycle the upload starts again from sector 0.

## Using the protocol from your own tool

Any MIDI SysEx sender can speak it: build frames as above, send HELLO, BEGIN, the DATA blocks of the image, QUERY, END. `core/dev_server.c` is the device side as a stdin/stdout process (hex frames in, hex replies out) used by `tools/e2e/E2E.java` to run the real Java `Uploader` against the real C device code.
