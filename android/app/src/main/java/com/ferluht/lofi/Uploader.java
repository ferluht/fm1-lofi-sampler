package com.ferluht.lofi;

/** Pack upload over any MIDI link: BEGIN, DATA bursts per 4 KiB sector with bitmap acks, END (device verifies CRC32 and commits). */
final class Uploader {
    interface Link {
        void send(byte[] msg);
        byte[] receive(long timeoutMs) throws InterruptedException;   // one complete SysEx or null on timeout
        void drain();
    }
    interface Progress { void on(int sectors, int total, String stage); }
    static final class Failure extends Exception { Failure(String m) { super(m); } }

    private final Link link; private int seq; int retries, blocksSent, blocksResent; long pacingMs = 3, replyTimeoutMs = 500;
    Uploader(Link l) { link = l; }

    private byte[] request(int cmd, byte[] payload, int n) throws InterruptedException, Failure {
        int s = ++seq & 0x3fff; byte[] msg = LofiProto.frame(cmd, s, payload, n);
        for (int attempt = 0; attempt < 12; attempt++) {
            if (attempt > 0) retries++;
            link.drain(); link.send(msg);
            long end = System.currentTimeMillis() + replyTimeoutMs;
            while (true) {
                long left = end - System.currentTimeMillis(); if (left <= 0) break;
                byte[] r = link.receive(left); if (r == null) break;
                LofiProto.Msg m = LofiProto.parse(r);
                if (m != null && m.cmd == (cmd | 0x40) && m.seq == s) return m.payload;
            }
        }
        throw new Failure("no answer from the FM-1");
    }

    /** @return null on success, otherwise a message */
    void send(byte[] img, Progress prog) throws InterruptedException, Failure {
        int size = img.length; long crc = LofiProto.crc32(img, 0, size);
        prog.on(0, 1, "connecting");
        byte[] p = request(LofiProto.HELLO, null, 0);
        if (p.length < 19 || p[0] != 0) throw new Failure("FM-1 refused HELLO (is the lofi firmware installed?)");
        int dataSectors = (p[2] & 0xff) | (p[3] & 0xff) << 8;
        if (size > dataSectors * LofiProto.SECTOR) throw new Failure("pack does not fit: " + size + " > " + dataSectors * LofiProto.SECTOR);
        byte[] b = new byte[3 + LofiProto.BLOCK + 2];
        LofiProto.p32(b, 0, size); LofiProto.p32(b, 4, crc);
        p = request(LofiProto.BEGIN, b, 8);
        if (p.length < 5 || p[0] != 0) throw new Failure("BEGIN rejected (status " + (p.length > 0 ? p[0] : -1) + ")");
        int next = (p[1] & 0xff) | (p[2] & 0xff) << 8, total = (p[3] & 0xff) | (p[4] & 0xff) << 8;
        for (int sec = next; sec < total; sec++) {
            prog.on(sec, total, "sending");
            int rem = Math.min(LofiProto.SECTOR, size - sec * LofiProto.SECTOR), blocks = (rem + LofiProto.BLOCK - 1) / LofiProto.BLOCK;
            long want = blocks >= 32 ? 0xffffffffL : (1L << blocks) - 1, have = 0; boolean done = false;
            for (int round = 0; round < 40 && !done; round++) {
                for (int blk = 0; blk < blocks; blk++) {
                    if ((have >> blk & 1) != 0) continue;
                    java.util.Arrays.fill(b, (byte) 0);
                    b[0] = (byte) sec; b[1] = (byte) (sec >> 8); b[2] = (byte) blk;
                    int off = sec * LofiProto.SECTOR + blk * LofiProto.BLOCK;
                    System.arraycopy(img, off, b, 3, Math.min(LofiProto.BLOCK, size - off));
                    int c = LofiProto.crc16(b, 3 + LofiProto.BLOCK); b[3 + LofiProto.BLOCK] = (byte) c; b[4 + LofiProto.BLOCK] = (byte) (c >> 8);
                    link.send(LofiProto.frame(LofiProto.DATA, 0, b, b.length));
                    blocksSent++; if (round > 0) blocksResent++;
                    if (pacingMs > 0) Thread.sleep(pacingMs);
                }
                byte[] q = request(LofiProto.QUERY, new byte[]{(byte) sec, (byte) (sec >> 8)}, 2);
                if (q.length < 8 || q[0] != 0) throw new Failure("QUERY failed (status " + (q.length > 0 ? q[0] : -1) + ")");
                if (q[3] != 0) done = true; else have = LofiProto.g32(q, 4) & want;
            }
            if (!done) throw new Failure("sector " + sec + " would not complete");
        }
        prog.on(total, total, "verifying");
        LofiProto.p32(b, 0, crc);
        p = request(LofiProto.END, b, 4);
        if (p.length < 1 || p[0] != 0) throw new Failure(p.length > 0 && p[0] == LofiProto.CRC ? "CRC mismatch in the FM-1 flash" : "commit failed (status " + (p.length > 0 ? p[0] : -1) + ")");
        prog.on(total, total, "done");
    }
}
