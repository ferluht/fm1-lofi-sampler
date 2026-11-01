package com.ferluht.lofi;

/** Native bridge to the shared C codec (core/lofi_codec.h, lofi_pack.h): the very code the firmware decoder is built from. */
final class Lofi {
    static boolean loaded;
    static { try { System.loadLibrary("lofi"); loaded = true; } catch (Throwable t) { loaded = false; } }
    static native byte[] buildPack(String name, int scale, int scaleRoot, short[][] pcm, String[] names, int[] params /* per slot: lo hi root flags loopStart loopEnd(-1) */);
    static native short[] roundTrip(short[] pcm);
    static native int encodedBytes(int frames);
    static final int HEADER = 1152, SUPER = 640, SUPER_BYTES = 192, SLOTS = 32;
    static final int CAPACITY = 127 * 4096;   // data sectors of the 536576 byte user partition, minus the two commit sectors and the two sectors of the saved loops + settings
    static final String[] SCALES = {"OFF", "CHROM", "MIX", "MAJ", "MIN", "HMIN", "PHRYG", "MIN+"};
    static final String[] NOTES = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static final int[][] STEPS = {{}, {0,1,2,3,4,5,6,7,8,9,10,11}, {0,2,4,5,7,9,10}, {0,2,4,5,7,9,11}, {0,2,3,5,7,8,10}, {0,2,3,5,7,8,11}, {0,1,3,5,7,8,10}, {0,2,3,5,7,8,10,11}};
    static int bytesFor(int frames) { return (frames + SUPER - 1) / SUPER * SUPER_BYTES; }
}
