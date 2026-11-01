package com.ferluht.lofi;

import android.content.Context;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.net.Uri;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Any audio file -> 16 kHz mono int16 (low-passed at 7.2 kHz before decimation). */
final class AudioImport {
    static final int RATE = 16000, MAX_SECONDS = 300;

    static short[] load(Context c, Uri uri) throws Exception {
        byte[] head = new byte[12]; int got = 0;
        try (InputStream in = c.getContentResolver().openInputStream(uri)) { got = in.read(head); }
        if (got == 12 && head[0] == 'R' && head[1] == 'I' && head[2] == 'F' && head[3] == 'F' && head[8] == 'W') {
            try (InputStream in = c.getContentResolver().openInputStream(uri)) { return wav(in); }
        }
        return codec(c, uri);
    }

    private static short[] wav(InputStream in) throws Exception {
        ByteArrayOutputStream o = new ByteArrayOutputStream(); byte[] b = new byte[65536]; int n;
        while ((n = in.read(b)) > 0) o.write(b, 0, n);
        ByteBuffer bb = ByteBuffer.wrap(o.toByteArray()).order(ByteOrder.LITTLE_ENDIAN);
        int fmt = 1, ch = 1, rate = 44100, bits = 16, dataAt = -1, dataLen = 0, p = 12;
        while (p + 8 <= bb.limit()) {
            int id = bb.getInt(p), len = bb.getInt(p + 4);
            if (id == 0x20746d66) { fmt = bb.getShort(p + 8) & 0xffff; ch = bb.getShort(p + 10); rate = bb.getInt(p + 12); bits = bb.getShort(p + 22); if (fmt == 0xfffe && len >= 26) fmt = bb.getShort(p + 32) & 0xffff; }
            else if (id == 0x61746164) { dataAt = p + 8; dataLen = Math.min(len < 0 ? Integer.MAX_VALUE : len, bb.limit() - dataAt); break; }
            p += 8 + len + (len & 1);
        }
        if (dataAt < 0 || ch < 1) throw new Exception("not a readable WAV file");
        int bytes = bits / 8, frames = dataLen / (bytes * ch); short[] mono = new short[frames];
        for (int i = 0; i < frames; i++) {
            double acc = 0;
            for (int k = 0; k < ch; k++) {
                int at = dataAt + (i * ch + k) * bytes; double v;
                if (fmt == 3 && bits == 32) v = bb.getFloat(at) * 32768.0;
                else if (bits == 16) v = bb.getShort(at);
                else if (bits == 24) v = ((bb.get(at) & 0xff) | (bb.get(at + 1) & 0xff) << 8 | bb.get(at + 2) << 16) / 256.0;
                else if (bits == 32) v = bb.getInt(at) / 65536.0;
                else v = ((bb.get(at) & 0xff) - 128) * 256.0;
                acc += v;
            }
            mono[i] = clip(acc / ch);
        }
        return resample(mono, rate);
    }

    private static short[] codec(Context c, Uri uri) throws Exception {
        MediaExtractor ex = new MediaExtractor(); ex.setDataSource(c, uri, null);
        int track = -1; MediaFormat f = null;
        for (int i = 0; i < ex.getTrackCount(); i++) { MediaFormat t = ex.getTrackFormat(i); String m = t.getString(MediaFormat.KEY_MIME); if (m != null && m.startsWith("audio/")) { track = i; f = t; break; } }
        if (track < 0) throw new Exception("no audio track in this file");
        ex.selectTrack(track);
        MediaCodec dec = MediaCodec.createDecoderByType(f.getString(MediaFormat.KEY_MIME)); dec.configure(f, null, null, 0); dec.start();
        int rate = f.getInteger(MediaFormat.KEY_SAMPLE_RATE), ch = f.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
        short[] mono = new short[rate * 10]; int n = 0; boolean inDone = false, outDone = false; MediaCodec.BufferInfo bi = new MediaCodec.BufferInfo();
        while (!outDone) {
            if (!inDone) {
                int ib = dec.dequeueInputBuffer(10000);
                if (ib >= 0) {
                    int sz = ex.readSampleData(dec.getInputBuffer(ib), 0);
                    if (sz < 0) { dec.queueInputBuffer(ib, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inDone = true; }
                    else { dec.queueInputBuffer(ib, 0, sz, ex.getSampleTime(), 0); ex.advance(); }
                }
            }
            int ob = dec.dequeueOutputBuffer(bi, 10000);
            if (ob == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                MediaFormat of = dec.getOutputFormat(); rate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE); ch = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
            } else if (ob >= 0) {
                ByteBuffer out = dec.getOutputBuffer(ob).order(ByteOrder.nativeOrder()); out.position(bi.offset); out.limit(bi.offset + bi.size);
                int frames = bi.size / (2 * ch);
                if (n + frames > mono.length) mono = java.util.Arrays.copyOf(mono, Math.max(mono.length * 2, n + frames));
                for (int i = 0; i < frames; i++) { int acc = 0; for (int k = 0; k < ch; k++) acc += out.getShort(); mono[n++] = (short) (acc / ch); }
                dec.releaseOutputBuffer(ob, false);
                if ((bi.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0 || n > rate * MAX_SECONDS) outDone = true;
            }
        }
        dec.stop(); dec.release(); ex.release();
        return resample(java.util.Arrays.copyOf(mono, n), rate);
    }

    static short clip(double v) { return (short) Math.max(-32768, Math.min(32767, Math.round(v))); }

    /** Windowed-sinc low-pass + decimation to 16 kHz. */
    static short[] resample(short[] in, int rate) {
        if (rate == RATE) return in;
        double ratio = (double) rate / RATE, cutoff = Math.min(7200.0, rate / 2.0 * 0.95) / rate;   // cycles per input sample
        int half = (int) Math.ceil(12 * Math.max(1.0, ratio)); int outN = (int) (in.length / ratio); short[] out = new short[outN];
        for (int i = 0; i < outN; i++) {
            double t = i * ratio; int c = (int) Math.floor(t); double acc = 0, norm = 0;
            for (int j = Math.max(0, c - half); j <= Math.min(in.length - 1, c + half); j++) {
                double x = j - t, a = 2 * Math.PI * cutoff * x, s = Math.abs(a) < 1e-9 ? 1.0 : Math.sin(a) / a;
                double w = 0.5 + 0.5 * Math.cos(Math.PI * x / (half + 1));
                double k = 2 * cutoff * s * w; acc += in[j] * k; norm += k;
            }
            out[i] = clip(norm > 0.05 ? acc / norm : acc);
        }
        return out;
    }

    /** Onset detection: RMS envelope in 4 ms windows, a hit starts when the level jumps over the quiet floor. sensitivity 0..1 */
    static int[] autoSlice(short[] x, double sensitivity, int minGapMs) {
        int win = RATE * 4 / 1000, n = x.length / win; if (n < 4) return new int[0];
        double[] env = new double[n];
        for (int i = 0; i < n; i++) { double s = 0; for (int k = 0; k < win; k++) { double v = x[i * win + k]; s += v * v; } env[i] = Math.sqrt(s / win); }
        double max = 1; for (double e : env) max = Math.max(max, e);
        double ratio = 6.0 - 4.5 * sensitivity, floorLevel = max * (0.004 + 0.03 * (1 - sensitivity));
        int gap = minGapMs / 4; java.util.ArrayList<Integer> cuts = new java.util.ArrayList<>(); int last = -gap - 1; double slow = env[0];
        for (int i = 1; i < n; i++) {
            if (env[i] > floorLevel && env[i] > ratio * Math.max(slow, 1.0) && i - last > gap) { cuts.add(Math.max(0, i - 1) * win); last = i; }
            slow = slow * 0.9 + env[i] * 0.1;
        }
        int[] r = new int[cuts.size()]; for (int i = 0; i < r.length; i++) r[i] = cuts.get(i); return r;
    }
}
