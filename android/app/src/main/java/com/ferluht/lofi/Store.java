package com.ferluht.lofi;

import android.content.Context;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

/** Packs live in files/packs/<id>/pack.json + slotN.pcm (16 kHz mono int16 little endian), so they stay editable. */
final class Store {
    private final File root;
    Store(Context c) { root = new File(c.getFilesDir(), "packs"); root.mkdirs(); }
    List<Pack> list() {
        List<Pack> out = new ArrayList<>();
        File[] dirs = root.listFiles();
        if (dirs != null) for (File d : dirs) { try { out.add(load(d.getName())); } catch (Exception e) { /* skip broken */ } }
        out.sort((a, b) -> a.name.compareToIgnoreCase(b.name));
        return out;
    }
    /** First launch: a ready-made demo pack (Tape Key, Shiny, Phonk bass, 9 TrapKit hits) so an upload can be tried without any files. */
    void ensureDemo(Context c) {
        android.content.SharedPreferences sp = c.getSharedPreferences("lofi", Context.MODE_PRIVATE);
        if (sp.getBoolean("demo2", false)) return;
        try {
            Pack p = new Pack(); p.id = "demo"; p.name = "DEMO"; p.scale = 4; p.scaleRoot = 0;                       // C minor
            for (int i = 1; i <= 9; i++) { Slot h = demoSlot(c, "HIT " + i, "hit" + i, 0, 0, 0, Slot.F_ONESHOT); h.setBankKeys(0, i - 1, i - 1); p.slots.add(h); }       // zone 1: the TrapKit hits
            Slot t = demoSlot(c, "TAPE KEY", "tape_key", 0, 26, 6, 0); t.setBankKeys(1, 0, 26); t.root = 6; t.voices = 4; p.slots.add(t);                              // zone 2: a melodic sample
            Slot sh = demoSlot(c, "SHINY", "shiny", 0, 26, 3, 0); sh.setBankKeys(2, 0, 26); sh.root = 3; sh.voices = 4; p.slots.add(sh);                                 // zone 3
            Slot ph = demoSlot(c, "PHONK BASS", "phonk", 0, 26, 10, 0); ph.setBankKeys(3, 0, 26); ph.root = 10; ph.voices = 1; ph.glide = 8; p.slots.add(ph);            // zone 4: a mono bass with a slide
            p.slots.get(2).choke = 1; p.slots.get(3).choke = 1;                                                       // hits 3 and 4 cut each other (a hat pair)
            save(p); sp.edit().putBoolean("demo2", true).apply();
        } catch (Exception e) { /* no demo, not fatal */ }
    }
    private static Slot demoSlot(Context c, String name, String file, int lo, int hi, int root, int flags) throws IOException {
        Slot s = new Slot(); s.name = name; s.lo = lo; s.hi = hi; s.root = root; s.flags = flags;
        try (InputStream in = c.getAssets().open("demo/" + file + ".raw")) {
            ByteArrayOutputStream o = new ByteArrayOutputStream(); byte[] b = new byte[65536]; int n; while ((n = in.read(b)) > 0) o.write(b, 0, n);
            byte[] raw = o.toByteArray(); s.pcm = new short[raw.length / 2]; ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(s.pcm);
        }
        return s;
    }
    Pack load(String id) throws Exception {
        File d = new File(root, id); Pack p = new Pack(); p.id = id;
        JSONObject j = new JSONObject(new String(readAll(new File(d, "pack.json")), "UTF-8")); p.name = j.getString("name"); p.scale = j.optInt("scale", 0); p.scaleRoot = j.optInt("sroot", 0); int model = j.optInt("model", 1);
        JSONArray a = j.getJSONArray("slots");
        for (int i = 0; i < a.length(); i++) {
            JSONObject o = a.getJSONObject(i); Slot s = new Slot();
            s.name = o.getString("name"); s.lo = o.getInt("lo"); s.hi = o.getInt("hi"); s.root = o.getInt("root"); s.flags = o.getInt("flags");
            s.loopStart = o.optInt("ls", 0); s.loopEnd = o.optInt("le", -1); s.voices = o.optInt("vo", 0); s.choke = o.optInt("ch", 0); s.glide = o.optInt("gl", 0);
            byte[] raw = readAll(new File(d, "slot" + i + ".pcm")); s.pcm = new short[raw.length / 2];
            ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(s.pcm);
            p.slots.add(s);
        }
        if (model < 2) { for (Slot s : p.slots) s.root %= 12; p.autoLayout(); try { save(p); } catch (Exception e) { /* will be converted again next time */ } }   // packs of the old key range model: one zone per 27 keys now
        return p;
    }
    void save(Pack p) throws Exception {
        File d = new File(root, p.id); d.mkdirs();
        JSONObject j = new JSONObject(); j.put("name", p.name); j.put("model", 2); j.put("scale", p.scale); j.put("sroot", p.scaleRoot); JSONArray a = new JSONArray();
        for (int i = 0; i < p.slots.size(); i++) {
            Slot s = p.slots.get(i); JSONObject o = new JSONObject();
            o.put("name", s.name); o.put("lo", s.lo); o.put("hi", s.hi); o.put("root", s.root); o.put("flags", s.flags); o.put("ls", s.loopStart); o.put("le", s.loopEnd); o.put("vo", s.voices); o.put("ch", s.choke); o.put("gl", s.glide);
            a.put(o);
            ByteBuffer b = ByteBuffer.allocate(s.pcm.length * 2).order(ByteOrder.LITTLE_ENDIAN); b.asShortBuffer().put(s.pcm);
            try (FileOutputStream f = new FileOutputStream(new File(d, "slot" + i + ".pcm"))) { f.write(b.array()); }
        }
        j.put("slots", a);
        for (int i = p.slots.size(); new File(d, "slot" + i + ".pcm").delete(); i++) { /* drop leftovers */ }
        try (FileOutputStream f = new FileOutputStream(new File(d, "pack.json"))) { f.write(j.toString().getBytes("UTF-8")); }
    }
    void delete(Pack p) {
        File d = new File(root, p.id); File[] fs = d.listFiles();
        if (fs != null) for (File f : fs) f.delete();
        d.delete();
    }
    static byte[] readAll(File f) throws IOException {
        try (FileInputStream in = new FileInputStream(f)) { ByteArrayOutputStream o = new ByteArrayOutputStream(); byte[] b = new byte[65536]; int n; while ((n = in.read(b)) > 0) o.write(b, 0, n); return o.toByteArray(); }
    }
}
