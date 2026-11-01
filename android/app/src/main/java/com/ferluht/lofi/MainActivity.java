package com.ferluht.lofi;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothDevice;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.midi.MidiDeviceInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.*;
import java.io.OutputStream;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
    static final int BG = 0xff0b0b0b, FG = 0xffefefef, DIM = 0xff8a8a8a, ACC = 0xffff8a00, LINE = 0xff262626;
    static final int REQ_PICK = 1, REQ_EXPORT = 2, REQ_PERM = 3;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private Store store; private FrameLayout host; private final Deque<Runnable> back = new ArrayDeque<>();
    private Pack cur; private AudioTrack track; private Runnable afterPick, afterEdit;
    private float density;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b); density = getResources().getDisplayMetrics().density; store = new Store(this);
        host = new FrameLayout(this); host.setBackgroundColor(BG); setContentView(host);
        getWindow().setStatusBarColor(BG); getWindow().setNavigationBarColor(BG);
        if (!Lofi.loaded) { setScreen(column(title("FM1 LOFI"), note("The native codec library did not load. Reinstall the app."))); return; }
        store.ensureDemo(this);
        showHome();
    }
    @Override public void onBackPressed() { if (!back.isEmpty()) back.pop().run(); else super.onBackPressed(); }
    @Override protected void onDestroy() { super.onDestroy(); stopPlay(); io.shutdownNow(); }

    // ---------------------------------------------------------------- ui helpers
    int dp(float v) { return Math.round(v * density); }
    TextView tv(String s, float sp, int color, boolean bold) { TextView t = new TextView(this); t.setText(s); t.setTextSize(sp); t.setTextColor(color); if (bold) t.setTypeface(Typeface.DEFAULT_BOLD); return t; }
    TextView title(String s) { TextView t = tv(s, 28, ACC, true); t.setPadding(0, dp(24), 0, dp(12)); return t; }
    TextView note(String s) { TextView t = tv(s, 14, DIM, false); t.setPadding(0, dp(4), 0, dp(8)); return t; }
    TextView button(String s, View.OnClickListener l) {
        TextView t = tv(s, 15, ACC, true); t.setGravity(Gravity.CENTER); t.setPadding(dp(14), dp(12), dp(14), dp(12)); t.setOnClickListener(l);
        t.setBackgroundColor(0xff17110a); return t;
    }
    LinearLayout column(View... vs) { LinearLayout l = new LinearLayout(this); l.setOrientation(LinearLayout.VERTICAL); l.setPadding(dp(16), dp(8), dp(16), dp(24)); for (View v : vs) l.addView(v); return l; }
    LinearLayout rowOf(View... vs) {
        LinearLayout l = new LinearLayout(this); l.setOrientation(LinearLayout.HORIZONTAL); l.setGravity(Gravity.CENTER_VERTICAL);
        for (View v : vs) { LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1); p.setMargins(0, dp(4), dp(8), dp(4)); l.addView(v, p); }
        return l;
    }
    View line() { View v = new View(this); v.setBackgroundColor(LINE); v.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1))); return v; }
    View bar(double frac, int color) {
        FrameLayout f = new FrameLayout(this); f.setBackgroundColor(LINE); f.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(6)));
        View v = new View(this); v.setBackgroundColor(color); f.addView(v, new FrameLayout.LayoutParams(0, dp(6))); f.post(() -> { ViewGroup.LayoutParams p = v.getLayoutParams(); p.width = (int) (f.getWidth() * Math.max(0, Math.min(1, frac))); v.setLayoutParams(p); });
        return f;
    }
    void setScreen(View content) {
        ScrollView sv = new ScrollView(this); sv.setBackgroundColor(BG); sv.addView(content); host.removeAllViews(); host.addView(sv);
    }
    void toast(String s) { Toast.makeText(this, s, Toast.LENGTH_SHORT).show(); }
    static String zoneText(Slot s) { return "Z" + (s.bank() + 1) + " · " + (s.isKit() ? "key " + (s.k0() + 1) : "keys " + (s.k0() + 1) + "–" + (s.k1() + 1) + " · pitch " + Lofi.NOTES[s.root % 12]); }
    static String noteName(int n) { String[] a = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; return a[n % 12] + (n / 12 - 1); }
    static String kb(int bytes) { return String.format(java.util.Locale.US, "%.1f KB", bytes / 1024.0); }
    EditText edit(String text, boolean number) { EditText e = new EditText(this); e.setText(text); e.setTextColor(FG); e.setHintTextColor(DIM); e.setSingleLine(); if (number) e.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_SIGNED); return e; }
    int num(EditText e, int def) { try { return Integer.parseInt(e.getText().toString().trim()); } catch (Exception x) { return def; } }
    AlertDialog.Builder dialog(String title) { AlertDialog.Builder b = new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert); b.setTitle(title); return b; }
    void save() { try { store.save(cur); } catch (Exception e) { toast("could not save: " + e.getMessage()); } }

    // ---------------------------------------------------------------- home
    void showHome() {
        back.clear(); cur = null;
        List<Pack> packs = store.list(); LinearLayout c = column(title("FM1 LOFI"), note("Sample packs for the FM-1 lo-fi sampler: 16 kHz, 2 bit. Build a pack here, send it over Bluetooth."));
        c.addView(button("NEW PACK", v -> { Pack p = new Pack(); cur = p; save(); showPack(p); }));
        c.addView(line());
        if (packs.isEmpty()) c.addView(note("No packs yet."));
        for (Pack p : packs) {
            LinearLayout r = column(tv(p.name, 18, FG, true), tv(p.slots.size() + " samples · " + String.format(java.util.Locale.US, "%.1f s", p.seconds()) + " · " + kb(p.imageBytes()) + " of " + kb(Lofi.CAPACITY), 13, DIM, false), bar(p.imageBytes() / (double) Lofi.CAPACITY, p.freeBytes() < 0 ? 0xffff3b30 : ACC));
            r.setPadding(0, dp(12), 0, dp(12)); r.setOnClickListener(v -> { cur = p; showPack(p); }); c.addView(r); c.addView(line());
        }
        setScreen(c);
    }

    // ---------------------------------------------------------------- pack editor
    void showPack(Pack p) {
        back.clear(); back.push(this::showHome);
        LinearLayout c = column();
        TextView name = tv(p.name, 26, ACC, true); name.setPadding(0, dp(24), 0, dp(4));
        name.setOnClickListener(v -> { EditText e = edit(p.name, false); dialog("Pack name").setView(e).setPositiveButton("OK", (d, w) -> { String s = e.getText().toString().trim(); if (!s.isEmpty()) { p.name = s.length() > 15 ? s.substring(0, 15) : s; save(); showPack(p); } }).setNegativeButton("Cancel", null).show(); });
        c.addView(name);
        c.addView(note(p.slots.size() + " of " + Lofi.SLOTS + " slots · " + String.format(java.util.Locale.US, "%.1f s", p.seconds()) + " · " + kb(p.imageBytes()) + " of " + kb(Lofi.CAPACITY) + (p.freeBytes() < 0 ? "  (TOO BIG)" : "")));
        c.addView(bar(p.imageBytes() / (double) Lofi.CAPACITY, p.freeBytes() < 0 ? 0xffff3b30 : ACC));
        c.addView(rowOf(button("ADD SAMPLE", v -> pickAudio(p)), button("ZONES", v -> showZones(p)), button("UPLOAD", v -> showUpload(p))));
        c.addView(line());
        for (int i = 0; i < p.slots.size(); i++) {
            final int idx = i; Slot s = p.slots.get(i);
            LinearLayout r = new LinearLayout(this); r.setOrientation(LinearLayout.HORIZONTAL); r.setGravity(Gravity.CENTER_VERTICAL); r.setPadding(0, dp(10), 0, dp(10));
            LinearLayout t = new LinearLayout(this); t.setOrientation(LinearLayout.VERTICAL);
            t.addView(tv((i + 1) + "  " + s.name, 17, FG, true));
            String keys = zoneText(s);
            t.addView(tv(keys + " · " + String.format(java.util.Locale.US, "%.2f s", s.seconds()) + " · " + kb(s.bytes()) + ((s.flags & Slot.F_LOOP) != 0 ? " · loop" : ""), 13, DIM, false));
            r.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            TextView play = button("▶", v -> preview(s.pcm, 0)); r.addView(play);
            r.setOnClickListener(v -> editSlot(p, idx)); c.addView(r); c.addView(line());
        }
        c.addView(rowOf(button("EXPORT .LFP", v -> exportPack(p)), button("DELETE PACK", v -> dialog("Delete \"" + p.name + "\"?").setPositiveButton("Delete", (d, w) -> { store.delete(p); showHome(); }).setNegativeButton("Cancel", null).show())));
        c.addView(note("Keys on the FM-1 start at F3 (MIDI 53); OCT -/+ moves them by an octave."));
        setScreen(c);
    }

    // ---------------------------------------------------------------- zones
    void showZones(Pack p) {
        back.push(() -> showPack(p));
        BankView zv = new BankView(this); zv.setPack(p); if (!p.slots.isEmpty()) zv.sel = 0;
        TextView info = note(""); LinearLayout scaleRow = new LinearLayout(this), rootRow = new LinearLayout(this); scaleRow.setOrientation(LinearLayout.HORIZONTAL); rootRow.setOrientation(LinearLayout.HORIZONTAL);
        Runnable[] refresh = new Runnable[1];
        refresh[0] = () -> {
            for (int i = 0; i < scaleRow.getChildCount(); i++) { TextView t = (TextView) scaleRow.getChildAt(i); t.setBackgroundColor(i == p.scale ? ACC : 0xff17110a); t.setTextColor(i == p.scale ? 0xff000000 : ACC); }
            for (int i = 0; i < rootRow.getChildCount(); i++) { TextView t = (TextView) rootRow.getChildAt(i); t.setBackgroundColor(i == p.scaleRoot ? ACC : 0xff17110a); t.setTextColor(i == p.scaleRoot ? 0xff000000 : ACC); }
            rootRow.setVisibility(p.scale == 0 ? View.GONE : View.VISIBLE);
            Slot s = zv.sel >= 0 && zv.sel < p.slots.size() ? p.slots.get(zv.sel) : null;
            info.setText(s == null ? "Tap a sample to pick it." : s.name + "  " + zoneText(s)
                + (s.isKit() ? "  · a kit key plays at its own pitch" : "  · keys start at F, follow " + (p.scale > 0 ? Lofi.SCALES[p.scale] + " " + Lofi.NOTES[p.scaleRoot] : "chromatic"))
                + (s.voices > 0 ? "  · " + (s.voices == 1 ? "mono" : s.voices + " voices") : "") + (s.choke > 0 ? "  · choke " + s.choke : "") + (s.glide > 0 ? "  · glide " + s.glide * 10 + " ms" : ""));
            zv.invalidate();
        };
        for (int i = 0; i < Lofi.SCALES.length; i++) { final int id = i; TextView t = button(Lofi.SCALES[i], v -> { p.scale = id; save(); refresh[0].run(); }); t.setPadding(dp(10), dp(10), dp(10), dp(10)); LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT); lp.setMargins(0, 0, dp(6), 0); scaleRow.addView(t, lp); }
        for (int i = 0; i < 12; i++) { final int id = i; TextView t = button(Lofi.NOTES[i], v -> { p.scaleRoot = id; save(); refresh[0].run(); }); t.setPadding(dp(10), dp(8), dp(10), dp(8)); LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT); lp.setMargins(0, 0, dp(6), 0); rootRow.addView(t, lp); }
        zv.listener = new BankView.Listener() {
            public void selected(int i) { refresh[0].run(); }
            public void edit(int i) { afterEdit = () -> { zv.setPack(p); refresh[0].run(); }; editSlot(p, i); }
            public void changed() { save(); refresh[0].run(); }
        };
        HorizontalScrollView sh = new HorizontalScrollView(this); sh.addView(scaleRow); HorizontalScrollView rh = new HorizontalScrollView(this); rh.addView(rootRow);
        LinearLayout c = column(title(p.name + "  ZONES"), note("A zone is 27 keys; OCT- / OCT+ on the FM-1 switch between zones. A zone is a kit (one sample per key) or one melodic sample over its keys. Drag a sample to another key or zone, drag the ends of a melodic sample to resize it, tap an empty cell to move the selected sample there, long-press to edit."),
                zv, info,
                rowOf(button("◀ KEY", v -> shift(p, zv, -1, refresh[0])), button("KEY ▶", v -> shift(p, zv, 1, refresh[0])), button("ZONE ▲", v -> moveBank(p, zv, -1, refresh[0])), button("ZONE ▼", v -> moveBank(p, zv, 1, refresh[0]))),
                rowOf(button("PLAY", v -> { if (zv.sel >= 0) preview(p.slots.get(zv.sel).pcm, 0); }), button("EDIT", v -> { if (zv.sel >= 0) { afterEdit = () -> { zv.setPack(p); refresh[0].run(); }; editSlot(p, zv.sel); } }), button("AUTO LAYOUT", v -> { p.autoLayout(); save(); zv.setPack(p); refresh[0].run(); })),
                tv("scale of the melodic samples (kit keys ignore it)", 12, DIM, false), sh, rh);
        c.setPadding(dp(16), dp(8), dp(16), dp(24)); refresh[0].run(); setScreen(c);
    }
    private void shift(Pack p, BankView zv, int d, Runnable r) {
        if (zv.sel < 0 || zv.sel >= p.slots.size()) return; Slot s = p.slots.get(zv.sel); if (!s.isKit()) { toast("a melodic sample takes the whole zone"); return; }
        int nk = s.k0() + d; if (p.fits(s, s.bank(), nk, nk, s)) { s.setBankKeys(s.bank(), nk, nk); save(); zv.requestLayout(); zv.invalidate(); r.run(); } else toast("that key is taken");
    }
    private void moveBank(Pack p, BankView zv, int d, Runnable r) {
        if (zv.sel < 0 || zv.sel >= p.slots.size()) return; Slot s = p.slots.get(zv.sel); int nb = s.bank() + d;
        if (p.fits(s, nb, s.k0(), s.k1(), s)) { s.setBankKeys(nb, s.k0(), s.k1()); save(); zv.requestLayout(); zv.invalidate(); r.run(); } else toast("that zone cannot take it (a zone is a kit or one melodic sample)");
    }

    void editSlot(Pack p, int idx) {
        Slot s = p.slots.get(idx);
        EditText name = edit(s.name, false), zb = edit(String.valueOf(s.bank() + 1), true), k0 = edit(String.valueOf(s.k0() + 1), true), k1 = edit(String.valueOf(s.k1() + 1), true);
        Spinner pitch = new Spinner(this); pitch.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, Lofi.NOTES)); pitch.setSelection(s.isKit() ? 0 : s.root % 12);
        EditText ls = edit(String.valueOf(s.loopStart * 1000 / 16000), true), le = edit(s.loopEnd < 0 ? "" : String.valueOf(s.loopEnd * 1000 / 16000), true);
        CheckBox loop = new CheckBox(this); loop.setText("loop between start and end (ms)"); loop.setTextColor(FG); loop.setChecked((s.flags & Slot.F_LOOP) != 0);
        EditText vo = edit(String.valueOf(s.voices), true), ch = edit(String.valueOf(s.choke), true), gl = edit(String.valueOf(s.glide * 10), true);
        CheckBox one = new CheckBox(this); one.setText("one-shot (ignore key release)"); one.setTextColor(FG); one.setChecked((s.flags & Slot.F_ONESHOT) != 0);
        SeekBar spd = new SeekBar(this); spd.setMax(5); spd.setProgress(4); TextView spdT = tv("preview speed: 0 oct", 13, DIM, false);
        spd.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar b, int v, boolean u) { spdT.setText("preview speed: " + (v - 4) + " oct"); }
            public void onStartTrackingTouch(SeekBar b) {} public void onStopTrackingTouch(SeekBar b) {} });
        LinearLayout c = column(tv("name", 12, DIM, false), name, tv("zone 1-8 / first key / last key (1-27). One key = a kit hit, several = a melodic sample (then the zone holds nothing else)", 12, DIM, false), rowOf(zb, k0, k1), tv("pitch of the sample (melodic: the keys start at F, the zone begins one octave below the sample)", 12, DIM, false), pitch,
                loop, rowOf(ls, le), one, tv("voices (0 = unlimited, 1 = mono) / choke group (0 = none) / glide ms (0 = off)", 12, DIM, false), rowOf(vo, ch, gl), spdT, spd, button("PLAY", v -> preview(s.pcm, spd.getProgress() - 4)));
        c.setPadding(dp(16), dp(8), dp(16), dp(8));
        ScrollView sv = new ScrollView(this); sv.addView(c);
        dialog("Sample " + (idx + 1)).setView(sv).setPositiveButton("Save", (d, w) -> {
            s.name = name.getText().toString().trim().isEmpty() ? s.name : name.getText().toString().trim();
            int nb = Math.max(1, Math.min(Pack.MAX_BANKS, num(zb, s.bank() + 1))) - 1, a0 = Math.max(1, Math.min(27, num(k0, s.k0() + 1))) - 1, a1 = Math.max(a0 + 1, Math.min(27, num(k1, s.k1() + 1))) - 1;
            if (p.fits(s, nb, a0, a1, s)) { s.setBankKeys(nb, a0, a1); if (!s.isKit()) s.root = pitch.getSelectedItemPosition(); } else toast("that place is taken: the zone keeps its old keys");
            int a = Math.max(0, num(ls, 0)) * 16, e = le.getText().toString().trim().isEmpty() ? -1 : num(le, -1) * 16;
            if (e > s.pcm.length) e = s.pcm.length;
            s.flags = (loop.isChecked() && e > a ? Slot.F_LOOP : 0) | (one.isChecked() ? Slot.F_ONESHOT : 0); s.loopStart = a; s.loopEnd = loop.isChecked() && e > a ? e : -1;
            s.voices = Math.max(0, Math.min(8, num(vo, 0))); s.choke = Math.max(0, Math.min(15, num(ch, 0))); s.glide = Math.max(0, Math.min(255, num(gl, 0) / 10));
            save(); if (afterEdit != null) { Runnable r = afterEdit; afterEdit = null; r.run(); } else showPack(p);
        }).setNeutralButton("Delete", (d, w) -> { p.slots.remove(idx); save(); if (afterEdit != null) { Runnable r = afterEdit; afterEdit = null; r.run(); } else showPack(p); }).setNegativeButton("Cancel", (d, w) -> { stopPlay(); afterEdit = null; }).show();
    }

    void exportPack(Pack p) {
        if (p.slots.isEmpty()) { toast("pack is empty"); return; }
        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/octet-stream").putExtra(Intent.EXTRA_TITLE, p.name.replace(' ', '_') + ".lfp");
        startActivityForResult(i, REQ_EXPORT);
    }

    // ---------------------------------------------------------------- import + slicer
    void pickAudio(Pack p) { Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("audio/*"); startActivityForResult(i, REQ_PICK); }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null || cur == null) return;
        Uri uri = data.getData();
        if (req == REQ_PICK) {
            final String base = baseName(uri); final ProgressDialog_ pd = new ProgressDialog_("Decoding " + base + " ...");
            io.execute(() -> {
                try { short[] pcm = AudioImport.load(this, uri); ui.post(() -> { pd.close(); if (pcm.length < 160) toast("file is empty"); else showSlicer(cur, pcm, base); }); }
                catch (Throwable t) { ui.post(() -> { pd.close(); toast("could not read the file: " + t.getMessage()); }); }
            });
        } else if (req == REQ_EXPORT) {
            try (OutputStream o = getContentResolver().openOutputStream(uri)) { o.write(cur.build()); toast("exported"); } catch (Exception e) { toast("export failed: " + e.getMessage()); }
        }
    }
    String baseName(Uri u) {
        String s = u.getLastPathSegment(); if (s == null) return "SAMPLE"; int k = Math.max(s.lastIndexOf('/'), s.lastIndexOf(':')); s = s.substring(k + 1); int d = s.lastIndexOf('.'); if (d > 0) s = s.substring(0, d);
        return s.length() > 9 ? s.substring(0, 9) : s;
    }
    final class ProgressDialog_ {
        final AlertDialog d;
        ProgressDialog_(String msg) { ProgressBar pb = new ProgressBar(MainActivity.this); LinearLayout l = column(tv(msg, 15, FG, false), pb); d = dialog("Please wait").setView(l).setCancelable(false).show(); }
        void close() { try { d.dismiss(); } catch (Exception e) { /* gone */ } }
    }

    static short[] trimTail(short[] x) {
        int last = 0; for (int i = x.length - 1; i >= 0; i--) if (Math.abs(x[i]) > 60) { last = i; break; }
        int end = Math.min(x.length, last + 16 * 12); return end < x.length ? java.util.Arrays.copyOf(x, end) : x;
    }

    void showSlicer(Pack p, short[] pcm, String base) {
        back.push(() -> showPack(p));
        WaveView wv = new WaveView(this); wv.setPcm(pcm); wv.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(220)));
        TextView info = note("");
        Runnable refresh = () -> { short[] seg = wv.segment(wv.selected); info.setText(String.format(java.util.Locale.US, "%d slices · selected %d: %.2f s, %s  ·  pack free %s", wv.segCount(), wv.selected + 1, seg.length / 16000.0, kb(Lofi.bytesFor(seg.length)), kb(p.freeBytes()))); };
        wv.listener = refresh::run; refresh.run();
        SeekBar pan = new SeekBar(this); pan.setMax(1000); pan.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar b, int v, boolean u) { if (u) wv.pan(v / 1000f); } public void onStartTrackingTouch(SeekBar b) {} public void onStopTrackingTouch(SeekBar b) {} });
        SeekBar sens = new SeekBar(this); sens.setMax(100); sens.setProgress(50);
        LinearLayout c = column(title("SLICE  " + base), note("Tap the waveform to add a slice point, drag a point to move it, long-press to remove. Tap the bottom strip to pick a slice."), wv, pan,
                rowOf(button("ZOOM +", v -> wv.zoom(0.5f)), button("ZOOM −", v -> wv.zoom(2f)), button("PLAY", v -> preview(wv.segment(wv.selected), 0))),
                tv("auto-slice sensitivity", 12, DIM, false), sens,
                rowOf(button("AUTO", v -> { wv.marks.clear(); for (int m : AudioImport.autoSlice(pcm, sens.getProgress() / 100.0, 90)) wv.marks.add(m); wv.selected = 0; wv.invalidate(); refresh.run(); }),
                      button("CLEAR", v -> { wv.marks.clear(); wv.selected = 0; wv.invalidate(); refresh.run(); })), info,
                rowOf(button("ALL → KIT", v -> addKit(p, wv, base)), button("SELECTED → HIT", v -> addHit(p, wv, base))), button("SELECTED → KEYS (pitched, 12 keys)", v -> addKeys(p, wv, base)));
        setScreen(c);
    }
    private boolean room(Pack p, int bytes) { if (p.slots.size() >= Lofi.SLOTS) { toast("a pack holds " + Lofi.SLOTS + " samples"); return false; } if (p.imageBytes() + bytes > Lofi.CAPACITY) { toast("not enough room: " + kb(Lofi.CAPACITY - p.imageBytes()) + " free"); return false; } return true; }
    private void addKit(Pack p, WaveView wv, String base) {
        int n = wv.segCount(), added = 0;
        for (int i = 0; i < n; i++) {
            short[] s = trimTail(wv.segment(i)); if (s.length < 160) continue; if (!room(p, Lofi.bytesFor(s.length))) break;
            Slot sl = new Slot(); sl.pcm = s; sl.name = (base + (n > 1 ? (i + 1) : "")); if (sl.name.length() > 11) sl.name = sl.name.substring(0, 11);
            sl.flags = Slot.F_ONESHOT; if (!p.placeKit(sl)) { toast("no free kit key left (8 zones)"); break; } p.slots.add(sl); added++;
        }
        if (added > 0) { save(); toast(added + " samples added"); back.pop(); showPack(p); }
    }
    private void addHit(Pack p, WaveView wv, String base) {
        short[] s = trimTail(wv.segment(wv.selected)); if (s.length < 160 || !room(p, Lofi.bytesFor(s.length))) return;
        Slot sl = new Slot(); sl.pcm = s; sl.name = base.length() > 11 ? base.substring(0, 11) : base; sl.flags = Slot.F_ONESHOT; if (!p.placeKit(sl)) { toast("no free kit key left (8 zones)"); return; } p.slots.add(sl); save(); back.pop(); showPack(p);
    }
    private void addKeys(Pack p, WaveView wv, String base) {
        short[] s = trimTail(wv.segment(wv.selected)); if (s.length < 160 || !room(p, Lofi.bytesFor(s.length))) return;
        Slot sl = new Slot(); sl.pcm = s; sl.name = base.length() > 11 ? base.substring(0, 11) : base; if (!p.placeMelodic(sl)) { toast("no empty zone left (8 zones)"); return; }
        p.slots.add(sl); save(); back.pop(); showPack(p);
    }

    // ---------------------------------------------------------------- preview
    void stopPlay() { try { if (track != null) { track.stop(); track.release(); } } catch (Exception e) { /* ignore */ } track = null; }
    void preview(short[] pcm, int octaves) {
        stopPlay(); if (pcm.length < 2) return;
        io.execute(() -> {
            short[] heard = Lofi.roundTrip(pcm);       // exactly what the FM-1 decoder produces
            ui.post(() -> {
                try {
                    int rate = (int) Math.max(1000, Math.min(32000, 16000 * Math.pow(2, octaves)));
                    track = new AudioTrack.Builder().setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(16000).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                            .setBufferSizeInBytes(heard.length * 2).setTransferMode(AudioTrack.MODE_STATIC).build();
                    track.write(heard, 0, heard.length); track.setPlaybackRate(rate); track.play();
                } catch (Exception e) { toast("playback failed: " + e.getMessage()); }
            });
        });
    }

    // ---------------------------------------------------------------- upload
    void showUpload(Pack p) {
        if (p.slots.isEmpty()) { toast("the pack is empty"); return; }
        if (p.freeBytes() < 0) { toast("the pack does not fit: remove some samples"); return; }
        if (Build.VERSION.SDK_INT >= 23) requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION, "android.permission.BLUETOOTH_SCAN", "android.permission.BLUETOOTH_CONNECT"}, REQ_PERM);
        LinearLayout list = column(); list.setPadding(dp(16), dp(8), dp(16), dp(8));
        TextView status = note("Switch the FM-1 on (Bluetooth MIDI starts by itself) or plug it in with a USB cable. It appears below.");
        list.addView(status);
        final AlertDialog[] dlg = new AlertDialog[1]; final Runnable[] stop = {() -> {}};
        final List<String> seen = new ArrayList<>();
        for (MidiDeviceInfo i : MidiLink.usbDevices(this)) list.addView(button("USB · " + MidiLink.label(i), v -> { stop[0].run(); dlg[0].dismiss(); connect(p, i, null, null); }));
        stop[0] = MidiLink.scan(this, (BluetoothDevice d, String name) -> {
            if (seen.contains(d.getAddress())) return; seen.add(d.getAddress());
            list.addView(button("Bluetooth · " + name, v -> { stop[0].run(); dlg[0].dismiss(); connect(p, null, d, name); }));
        });
        ScrollView sv = new ScrollView(this); sv.addView(list);
        dlg[0] = dialog("Send \"" + p.name + "\" to the FM-1").setView(sv).setNegativeButton("Cancel", (d, w) -> stop[0].run()).setOnCancelListener(d -> stop[0].run()).show();
    }

    void connect(Pack p, MidiDeviceInfo usb, BluetoothDevice ble, String name) {
        TextView msg = tv("Connecting ...", 15, FG, false); ProgressBar pb = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal); pb.setMax(100);
        LinearLayout l = column(msg, pb); AlertDialog d = dialog("Upload").setView(l).setCancelable(false).show();
        MidiLink link = new MidiLink(this);
        MidiLink.Opened opened = (lk, err) -> {
            if (err != null) { d.dismiss(); dialog("Upload").setMessage(err).setPositiveButton("OK", null).show(); return; }
            msg.setText("Building the pack ...");
            io.execute(() -> {
                String result;
                try {
                    byte[] img = p.build(); if (img == null) throw new Uploader.Failure("could not build the pack");
                    Uploader up = new Uploader(lk);
                    up.send(img, (s, t, stage) -> ui.post(() -> { msg.setText(stage + " (" + s + "/" + t + ")"); pb.setProgress(t == 0 ? 0 : s * 100 / t); }));
                    result = "Done. " + img.length / 1024 + " KB sent" + (up.blocksResent > 0 ? ", " + up.blocksResent + " blocks re-sent" : "") + ".";
                } catch (Uploader.Failure f) { result = "Failed: " + f.getMessage(); }
                catch (InterruptedException e) { result = "Cancelled"; }
                catch (Throwable t) { result = "Failed: " + t; }
                final String r = result; lk.close();
                ui.post(() -> { d.dismiss(); dialog("Upload").setMessage(r).setPositiveButton("OK", null).show(); });
            });
        };
        if (usb != null) link.openUsb(usb, opened); else link.openBle(ble, name, opened);
    }
}
