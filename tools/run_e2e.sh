#!/bin/bash
# Real Java sender (android/.../Uploader.java + LofiProto.java) against the C device (core/dev_server) over a lossy link.
set -e; cd "$(dirname "$0")/.."; S=${TMPDIR:-/tmp}/lofi_e2e; rm -rf $S; mkdir -p $S/com/ferluht/lofi; J=android/app/src/main/java/com/ferluht/lofi
gcc -O2 -Wno-misleading-indentation -o core/dev_server core/dev_server.c
sed 's/^final class/public final class/; s/^    static long crc32/    public static long crc32/' $J/LofiProto.java > $S/com/ferluht/lofi/LofiProto.java
sed 's/^final class/public final class/; s/^    interface Link/    public interface Link/; s/^    interface Progress/    public interface Progress/; s/^    static final class Failure/    public static final class Failure/; s/^    Uploader(/    public Uploader(/; s/^    void send(/    public void send(/; s/int retries, blocksSent, blocksResent; long pacingMs = 3, replyTimeoutMs = 500;/public int retries, blocksSent, blocksResent; public long pacingMs = 3, replyTimeoutMs = 500;/' $J/Uploader.java > $S/com/ferluht/lofi/Uploader.java
javac -d $S $S/com/ferluht/lofi/*.java tools/e2e/E2E.java
for l in 0 10 25; do for seed in 1 2 3 4 5 6; do java -cp $S E2E ${1:-out/test.lfp} $l core/dev_server $seed | grep -v reboot; done; done
