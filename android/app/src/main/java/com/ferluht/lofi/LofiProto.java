package com.ferluht.lofi;

import java.util.zip.CRC32;

/** SysEx framing of the pack upload protocol; the C reference is core/lofi_proto.h (keep both in step). */
final class LofiProto {
    static final int SECTOR = 4096, BLOCK = 128, MAXMSG = 192;
    static final int HELLO = 1, BEGIN = 2, DATA = 3, QUERY = 4, END = 5, ABORT = 6, DELETE = 7;
    static final int OK = 0, NOSPACE = 1, BADARG = 2, STATE = 3, CRC = 4, IO = 5;

    static final class Msg { int cmd, seq; byte[] payload; }

    static long crc32(byte[] d, int off, int len) { CRC32 c = new CRC32(); c.update(d, off, len); return c.getValue(); }
    static int crc16(byte[] p, int n) {
        int c = 0xffff;
        for (int i = 0; i < n; i++) { c ^= (p[i] & 0xff) << 8; for (int k = 0; k < 8; k++) c = (c & 0x8000) != 0 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff; }
        return c;
    }
    static int pack7(byte[] in, int n, byte[] out, int off) {
        long acc = 0; int bits = 0, o = off;
        for (int i = 0; i < n; i++) { acc |= (long) (in[i] & 0xff) << bits; bits += 8; while (bits >= 7) { out[o++] = (byte) (acc & 0x7f); acc >>= 7; bits -= 7; } }
        if (bits > 0) out[o++] = (byte) (acc & 0x7f);
        return o - off;
    }
    static int unpack7(byte[] in, int off, int n, byte[] out) {
        long acc = 0; int bits = 0, o = 0;
        for (int i = 0; i < n; i++) { acc |= (long) (in[off + i] & 0x7f) << bits; bits += 7; if (bits >= 8) { out[o++] = (byte) (acc & 0xff); acc >>= 8; bits -= 8; } }
        return o;
    }
    static byte[] frame(int cmd, int seq, byte[] payload, int n) {
        byte[] out = new byte[10 + (n * 8 + 6) / 7 + 2]; int o = 0;
        out[o++] = (byte) 0xf0; out[o++] = 0x7d; out[o++] = 'L'; out[o++] = 'F'; out[o++] = 1; out[o++] = (byte) cmd;
        out[o++] = (byte) (seq & 0x7f); out[o++] = (byte) ((seq >> 7) & 0x7f);
        o += pack7(payload == null ? new byte[0] : payload, n, out, o);
        int sum = 0; for (int i = 1; i < o; i++) sum += out[i];
        out[o++] = (byte) ((128 - (sum & 0x7f)) & 0x7f); out[o++] = (byte) 0xf7;
        byte[] r = new byte[o]; System.arraycopy(out, 0, r, 0, o); return r;
    }
    static Msg parse(byte[] m) {
        int n = m.length;
        if (n < 10 || n > MAXMSG || (m[0] & 0xff) != 0xf0 || (m[n - 1] & 0xff) != 0xf7 || m[1] != 0x7d || m[2] != 'L' || m[3] != 'F' || m[4] != 1) return null;
        int sum = 0; for (int i = 1; i < n - 1; i++) { if ((m[i] & 0x80) != 0) return null; sum += m[i]; }
        if ((sum & 0x7f) != 0) return null;
        Msg r = new Msg(); r.cmd = m[5] & 0xff; r.seq = (m[6] & 0x7f) | (m[7] & 0x7f) << 7;
        byte[] tmp = new byte[MAXMSG]; int k = unpack7(m, 8, n - 10, tmp); r.payload = new byte[k]; System.arraycopy(tmp, 0, r.payload, 0, k);
        return r;
    }
    static void p32(byte[] b, int o, long v) { b[o] = (byte) v; b[o + 1] = (byte) (v >> 8); b[o + 2] = (byte) (v >> 16); b[o + 3] = (byte) (v >> 24); }
    static long g32(byte[] b, int o) { return (b[o] & 0xffL) | (b[o + 1] & 0xffL) << 8 | (b[o + 2] & 0xffL) << 16 | (b[o + 3] & 0xffL) << 24; }
}
