package com.ferluht.lofi;

/** One sample of a pack: 16 kHz mono PCM plus how the keys play it. */
final class Slot {
    static final int F_LOOP = 1, F_REVERSE = 2, F_ONESHOT = 4;
    String name = "SAMPLE";
    int lo = 0, hi = 0, root = 0, flags = F_ONESHOT;   // lo/hi: note ids = bank * 32 + key (a zone = bank of 27 keys); root: a kit key is its own pitch, a melodic sample's root is its pitch class 0..11
    int loopStart, loopEnd = -1;
    int voices = 0;     // polyphony of this zone: 0 unlimited, 1 mono, ..8
    int choke = 0;      // choke group 0 none, 1..15: a key of the group cuts every sounding voice of the group (open / closed hat)
    int glide = 0;      // legato glide in 10 ms steps (0 off)
    short[] pcm = new short[0];
    boolean isKit() { return lo == hi; }
    int bank() { return lo / 32; }
    int k0() { return lo % 32; }
    int k1() { return hi % 32; }
    void setBankKeys(int bank, int k0, int k1) { lo = bank * 32 + k0; hi = bank * 32 + k1; if (isKit()) root = lo; }
    int bytes() { return Lofi.bytesFor(pcm.length); }
    double seconds() { return pcm.length / 16000.0; }
}
