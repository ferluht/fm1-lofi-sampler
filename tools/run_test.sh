#!/bin/bash
cd "$(dirname "$0")/.."
for f in samples/*.wav; do n=$(basename "$f" .wav)
  ffmpeg -v error -y -i "$f" -ac 1 -ar 16000 -af "lowpass=f=7000" -f s16le out/$n.raw
  ffmpeg -v error -y -f s16le -ar 16000 -ac 1 -i out/$n.raw out/${n}_16k_ref.wav
  for b in 2 3 4; do echo -n "$n "; tools/lofi_codec out/$n.raw $b out/${n}_b$b.raw
    ffmpeg -v error -y -f s16le -ar 16000 -ac 1 -i out/${n}_b$b.raw out/${n}_16k_${b}bit.wav; done
done
