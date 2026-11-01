package com.ferluht.lofi;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;

/** The zones of a pack: one row per zone (bank) of 27 keys. A zone holds either a kit (one sample per key) or one melodic sample over its keys.
 *  Drag a sample to another key or zone, drag the ends of a melodic sample to resize it, tap an empty cell to move the selected sample there,
 *  long-press to edit. The firmware switches zones with OCT- / OCT+, a filled key lights its LED. */
final class BankView extends View {
    interface Listener { void selected(int idx); void edit(int idx); void changed(); }
    static final int[] COLORS = {0xffff8a00, 0xff00ffff, 0xff00ff00, 0xffff00ff, 0xffffff00, 0xff0080ff, 0xffff0000, 0xff9a60ff, 0xffa0ff30, 0xffff8080, 0xff00a0a0, 0xffdcdcdc};
    Pack pack; int sel = -1; Listener listener;
    private final float labelW, rowH; private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private int dragSlot = -1, mode, startBank, startLo, startHi, startCellK; private float downX, downY; private long downT; private boolean moved;

    BankView(Context c) { super(c); float d = c.getResources().getDisplayMetrics().density; labelW = 44 * d; rowH = 38 * d; p.setTextSize(11 * d); }
    static int color(int i) { return COLORS[i % COLORS.length]; }
    void setPack(Pack pk) { pack = pk; requestLayout(); invalidate(); }
    int rows() { return pack == null ? 1 : Math.min(Pack.MAX_BANKS, pack.banks() + 1); }
    @Override protected void onMeasure(int w, int h) { setMeasuredDimension(MeasureSpec.getSize(w), (int) (rowH * rows() + 6)); }
    private float cellW() { return (getWidth() - labelW) / Pack.KEYS; }
    private int keyAt(float x) { return Math.max(0, Math.min(Pack.KEYS - 1, (int) ((x - labelW) / cellW()))); }
    private int bankAt(float y) { return Math.max(0, Math.min(rows() - 1, (int) (y / rowH))); }
    @Override protected void onDraw(Canvas c) {
        c.drawColor(0xff0b0b0b); if (pack == null) return; float cw = cellW();
        for (int b = 0; b < rows(); b++) {
            float y = b * rowH; p.setColor(b % 2 == 0 ? 0xff101010 : 0xff0b0b0b); c.drawRect(0, y, getWidth(), y + rowH, p);
            int kind = pack.bankKind(b); p.setColor(0xff8a8a8a); c.drawText("Z" + (b + 1), 6, y + rowH / 2 - 2, p); p.setColor(0xff555555); c.drawText(kind == 0 ? "empty" : kind == 1 ? "kit" : "melodic", 6, y + rowH / 2 + 13, p);
            for (int k = 0; k < Pack.KEYS; k++) { float x = labelW + k * cw; p.setColor(0xff1a1a1a); c.drawRect(x + 1, y + 5, x + cw - 1, y + rowH - 5, p); }
        }
        for (int i = 0; i < pack.slots.size(); i++) {
            Slot s = pack.slots.get(i); int b = s.bank(); if (b >= rows()) continue; float y = b * rowH, x0 = labelW + s.k0() * cw, x1 = labelW + (s.k1() + 1) * cw; boolean isSel = i == sel;
            p.setColor((color(i) & 0x00ffffff) | (isSel ? 0xff000000 : 0xb0000000)); c.drawRect(x0 + 1, y + 5, x1 - 1, y + rowH - 5, p);
            if (!pack.fits(s, b, s.k0(), s.k1(), s)) { p.setColor(0xffff3b30); p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(3); c.drawRect(x0 + 1, y + 5, x1 - 1, y + rowH - 5, p); p.setStyle(Paint.Style.FILL); }
            else if (isSel) { p.setColor(0xffffffff); p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(3); c.drawRect(x0 + 1, y + 5, x1 - 1, y + rowH - 5, p); p.setStyle(Paint.Style.FILL); }
            p.setColor(0xff000000); String n = s.name; float maxw = x1 - x0 - 4; while (n.length() > 1 && p.measureText(n) > maxw) n = n.substring(0, n.length() - 1); if (maxw > 12) c.drawText(n, x0 + 3, y + rowH / 2 + 4, p);
        }
    }
    @Override public boolean onTouchEvent(MotionEvent e) {
        if (pack == null) return false;
        int b = bankAt(e.getY()), k = keyAt(e.getX());
        switch (e.getAction()) {
            case MotionEvent.ACTION_DOWN: {
                getParent().requestDisallowInterceptTouchEvent(true); downX = e.getX(); downY = e.getY(); downT = System.currentTimeMillis(); moved = false; dragSlot = pack.slotAt(b, k);
                if (dragSlot >= 0) {
                    Slot s = pack.slots.get(dragSlot); sel = dragSlot; startBank = s.bank(); startLo = s.k0(); startHi = s.k1(); startCellK = k;
                    mode = s.hi - s.lo >= 2 ? (k == s.k0() ? 1 : k == s.k1() ? 2 : 0) : 0;      // the end cells of a melodic sample are the resize handles
                    if (listener != null) listener.selected(dragSlot);
                }
                invalidate(); return true; }
            case MotionEvent.ACTION_MOVE: {
                if (dragSlot < 0) return true; if (Math.abs(e.getX() - downX) > 10 || Math.abs(e.getY() - downY) > 10) moved = true; if (!moved) return true;
                Slot s = pack.slots.get(dragSlot);
                if (mode == 0) { int w = startHi - startLo, nb = b, nk = Math.max(0, Math.min(Pack.KEYS - 1 - w, startLo + (k - startCellK)));
                    if (pack.fits(s, nb, nk, nk + w, s)) { s.setBankKeys(nb, nk, nk + w); } }
                else if (mode == 1) { int nk = Math.min(k, startHi - 1); if (pack.fits(s, startBank, nk, startHi, s)) s.setBankKeys(startBank, nk, startHi); }
                else { int nk = Math.max(k, startLo + 1); if (pack.fits(s, startBank, startLo, nk, s)) s.setBankKeys(startBank, startLo, nk); }
                requestLayout(); invalidate(); return true; }
            case MotionEvent.ACTION_UP:
                if (dragSlot >= 0) {
                    if (!moved && System.currentTimeMillis() - downT > 500 && listener != null) listener.edit(dragSlot);
                    else if (moved && listener != null) listener.changed();
                } else if (!moved && sel >= 0 && sel < pack.slots.size()) {            // tap on an empty cell: move the selected sample there
                    Slot s = pack.slots.get(sel); int w = s.hi - s.lo, nk = Math.min(k, Pack.KEYS - 1 - w);
                    if (w > 0) nk = 0;                                                // a melodic sample takes a whole empty zone from its first key
                    if (pack.fits(s, b, nk, nk + w, s)) { s.setBankKeys(b, nk, nk + w); requestLayout(); if (listener != null) listener.changed(); }
                }
                dragSlot = -1; invalidate(); return true;
            case MotionEvent.ACTION_CANCEL: dragSlot = -1; return true;
        }
        return true;
    }
}
