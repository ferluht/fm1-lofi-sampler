package com.ferluht.lofi;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Waveform with slice markers. Tap empty space = new marker, drag a marker to move it, long-press a marker to remove it,
 *  tap inside a segment (marker-free tap on the lower strip) selects it. */
final class WaveView extends View {
    interface Listener { void changed(); }
    static final int BG = 0xff0b0b0b, FG = 0xffefefef, DIM = 0xff444444, ACC = 0xffff8a00;
    short[] pcm = new short[0]; final List<Integer> marks = new ArrayList<>(); int selected;   // segment index
    float v0 = 0, v1 = 1;    // visible fraction
    Listener listener;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private int dragging = -1; private float downX, downY; private long downT; private boolean moved;
    private float[] peaksMin, peaksMax; private int peaksW; private float peaksV0, peaksV1;

    WaveView(Context c) { super(c); p.setStrokeWidth(1f); }
    void setPcm(short[] x) { pcm = x; marks.clear(); selected = 0; v0 = 0; v1 = 1; peaksW = 0; invalidate(); }
    int[] bounds() {      // segment edges: 0, marks..., length
        int[] b = new int[marks.size() + 2]; b[0] = 0; for (int i = 0; i < marks.size(); i++) b[i + 1] = marks.get(i); b[b.length - 1] = pcm.length; return b;
    }
    int segCount() { return marks.size() + 1; }
    short[] segment(int i) { int[] b = bounds(); i = Math.max(0, Math.min(i, b.length - 2)); return java.util.Arrays.copyOfRange(pcm, b[i], b[i + 1]); }
    void zoom(float f) { float c = (v0 + v1) / 2, w = Math.min(1f, Math.max(0.01f, (v1 - v0) * f)); v0 = Math.max(0, Math.min(1 - w, c - w / 2)); v1 = v0 + w; invalidate(); }
    void pan(float frac) { float w = v1 - v0; v0 = Math.max(0, Math.min(1 - w, frac * (1 - w))); v1 = v0 + w; invalidate(); }

    private void computePeaks(int w) {
        if (pcm.length == 0) return;
        peaksW = w; peaksV0 = v0; peaksV1 = v1; peaksMin = new float[w]; peaksMax = new float[w];
        double s0 = v0 * pcm.length, span = (v1 - v0) * pcm.length;
        for (int x = 0; x < w; x++) {
            int a = (int) (s0 + span * x / w), b = Math.max(a + 1, (int) (s0 + span * (x + 1) / w)); short mn = 32767, mx = -32768;
            int step = Math.max(1, (b - a) / 64);
            for (int i = a; i < b && i < pcm.length; i += step) { short v = pcm[i]; if (v < mn) mn = v; if (v > mx) mx = v; }
            peaksMin[x] = mn / 32768f; peaksMax[x] = mx / 32768f;
        }
    }
    private float xOf(int sample) { return (sample / (float) Math.max(1, pcm.length) - v0) / (v1 - v0) * getWidth(); }
    private int sampleAt(float x) { return (int) ((v0 + (v1 - v0) * x / getWidth()) * pcm.length); }

    @Override protected void onDraw(Canvas c) {
        int w = getWidth(), h = getHeight(); c.drawColor(BG);
        if (pcm.length == 0) return;
        if (peaksW != w || peaksV0 != v0 || peaksV1 != v1) computePeaks(w);
        int[] b = bounds();
        if (selected >= 0 && selected + 1 < b.length) { p.setColor(0xff1d1308); c.drawRect(Math.max(0, xOf(b[selected])), 0, Math.min(w, xOf(b[selected + 1])), h, p); }
        float mid = h / 2f; p.setColor(DIM); c.drawLine(0, mid, w, mid, p);
        for (int x = 0; x < w; x++) {
            int sample = sampleAt(x); boolean inSel = selected + 1 < b.length && sample >= b[selected] && sample < b[selected + 1];
            p.setColor(inSel ? ACC : FG); c.drawLine(x, mid - peaksMax[x] * mid * 0.95f, x, mid - peaksMin[x] * mid * 0.95f - 1, p);
        }
        p.setColor(ACC); p.setStrokeWidth(3f);
        for (int m : marks) { float x = xOf(m); if (x >= 0 && x <= w) c.drawLine(x, 0, x, h, p); }
        p.setStrokeWidth(1f);
    }
    private int nearMark(float x) {
        int best = -1; float bd = 36f * getResources().getDisplayMetrics().density / 2f;
        for (int i = 0; i < marks.size(); i++) { float d = Math.abs(xOf(marks.get(i)) - x); if (d < bd) { bd = d; best = i; } }
        return best;
    }
    @Override public boolean onTouchEvent(MotionEvent e) {
        if (pcm.length == 0) return true;
        switch (e.getAction()) {
            case MotionEvent.ACTION_DOWN: downX = e.getX(); downY = e.getY(); downT = System.currentTimeMillis(); moved = false; dragging = nearMark(downX); getParent().requestDisallowInterceptTouchEvent(true); return true;
            case MotionEvent.ACTION_MOVE:
                if (Math.abs(e.getX() - downX) > 12) moved = true;
                if (dragging >= 0 && moved) {
                    int s = Math.max(1, Math.min(pcm.length - 1, sampleAt(e.getX()))); marks.set(dragging, s); invalidate();
                }
                return true;
            case MotionEvent.ACTION_UP:
                if (dragging >= 0) {
                    if (!moved && System.currentTimeMillis() - downT > 500) marks.remove(dragging);
                    else if (moved) { int m = marks.get(dragging); Collections.sort(marks); dragging = marks.indexOf(m); }
                } else if (!moved) {
                    boolean strip = e.getY() > getHeight() * 0.8f;     // lower strip selects a segment, anywhere else adds a marker
                    int s = Math.max(1, Math.min(pcm.length - 1, sampleAt(e.getX())));
                    if (strip) { int[] b = bounds(); for (int i = 0; i + 1 < b.length; i++) if (s >= b[i] && s < b[i + 1]) selected = i; }
                    else { marks.add(s); Collections.sort(marks); int[] b = bounds(); for (int i = 0; i + 1 < b.length; i++) if (s >= b[i] && s < b[i + 1]) selected = i; }
                }
                dragging = -1; if (selected >= segCount()) selected = segCount() - 1;
                invalidate(); if (listener != null) listener.changed(); return true;
        }
        return true;
    }
}
