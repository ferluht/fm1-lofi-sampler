package com.ferluht.lofi;

import java.util.ArrayList;
import java.util.List;

final class Pack {
    String id = Long.toString(System.currentTimeMillis(), 36);
    String name = "NEW PACK"; int scale = 4, scaleRoot = 0;   // scale: 0 off, 1..7 (see Lofi.SCALES), default C minor; melodic samples follow it, kit keys do not
    final List<Slot> slots = new ArrayList<>();
    int imageBytes() { int n = Lofi.HEADER; for (Slot s : slots) n += s.bytes(); return n; }
    int freeBytes() { return Lofi.CAPACITY - imageBytes(); }
    double seconds() { double t = 0; for (Slot s : slots) t += s.seconds(); return t; }
    static final int KEYS = 27, MAX_BANKS = 8;
    /** zones (banks) in use */
    int banks() { int n = 0; for (Slot s : slots) n = Math.max(n, s.bank() + 1); return n; }
    /** 0 empty, 1 kit, 2 melodic */
    int bankKind(int b) { int k = 0; for (Slot s : slots) if (s.bank() == b) { if (!s.isKit()) return 2; k = 1; } return k; }
    int slotAt(int b, int k) { for (int i = 0; i < slots.size(); i++) { Slot s = slots.get(i); if (s.bank() == b && k >= s.k0() && k <= s.k1()) return i; } return -1; }
    /** can `s` live at bank b, keys k0..k1 (a bank is a kit or one melodic sample)? `self` is ignored in the check. */
    boolean fits(Slot s, int b, int k0, int k1, Slot self) {
        if (b < 0 || b >= MAX_BANKS || k0 < 0 || k1 >= KEYS || k1 < k0) return false;
        boolean melodic = k1 > k0;
        for (Slot o : slots) {
            if (o == self || o.bank() != b) continue;
            if (melodic || !o.isKit()) return false;                      // a melodic sample needs the whole zone, and nothing else may sit in a zone with one
            if (o.k0() == k0) return false;                                // a kit key holds one sample
        }
        return true;
    }
    /** put a sample on the first free kit key (a new zone when the kits are full); false when the pack has no room */
    boolean placeKit(Slot s) {
        for (int b = 0; b < MAX_BANKS; b++) { int kind = bankKind(b); if (kind == 2) continue; for (int k = 0; k < KEYS; k++) if (fits(s, b, k, k, null)) { s.setBankKeys(b, k, k); return true; } }
        return false;
    }
    /** a melodic sample takes the first empty zone, all 27 keys */
    boolean placeMelodic(Slot s) {
        for (int b = 0; b < MAX_BANKS; b++) if (bankKind(b) == 0) { s.setBankKeys(b, 0, KEYS - 1); s.root = 0; s.flags = 0; return true; }
        return false;
    }
    /** kits first (27 keys per zone), then every melodic sample in a zone of its own */
    void autoLayout() {
        int b = 0, k = 0;
        for (Slot s : slots) if (s.isKit()) { if (k >= KEYS) { b++; k = 0; } s.setBankKeys(b, k, k); k++; }
        if (k > 0) b++;
        for (Slot s : slots) if (!s.isKit()) { s.setBankKeys(Math.min(b, MAX_BANKS - 1), 0, KEYS - 1); b++; }
    }
    byte[] build() {
        int n = slots.size();
        short[][] pcm = new short[n][]; String[] names = new String[n]; int[] p = new int[9 * n];
        for (int i = 0; i < n; i++) {
            Slot s = slots.get(i); pcm[i] = s.pcm; names[i] = s.name;
            p[9 * i] = s.lo; p[9 * i + 1] = s.hi; p[9 * i + 2] = s.root; p[9 * i + 3] = s.flags; p[9 * i + 4] = s.loopStart; p[9 * i + 5] = s.loopEnd;
            p[9 * i + 6] = s.voices; p[9 * i + 7] = s.choke; p[9 * i + 8] = s.glide;
        }
        return Lofi.buildPack(name.length() > 15 ? name.substring(0, 15) : name, scale, scaleRoot, pcm, names, p);
    }
}
