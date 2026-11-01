package com.ferluht.lofi;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.media.midi.*;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/** USB or BLE MIDI connection to the FM-1 (android.media.midi) + the BLE scan for MIDI peripherals. */
final class MidiLink implements Uploader.Link {
    static final ParcelUuid MIDI_SERVICE = ParcelUuid.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700");
    interface Opened { void done(MidiLink link, String error); }
    interface Found { void on(BluetoothDevice d, String name); }

    private final MidiManager mm; private MidiDevice dev; private MidiInputPort in; private MidiOutputPort out;
    private final LinkedBlockingQueue<byte[]> replies = new LinkedBlockingQueue<>(); private final ByteArrayOutputStream sx = new ByteArrayOutputStream(); private boolean inSx;
    String name = "";
    MidiLink(Context c) { mm = (MidiManager) c.getSystemService(Context.MIDI_SERVICE); }

    static List<MidiDeviceInfo> usbDevices(Context c) {
        MidiManager m = (MidiManager) c.getSystemService(Context.MIDI_SERVICE); List<MidiDeviceInfo> l = new ArrayList<>();
        if (m != null) for (MidiDeviceInfo i : m.getDevices()) if (i.getType() == MidiDeviceInfo.TYPE_USB && i.getInputPortCount() > 0 && i.getOutputPortCount() > 0) l.add(i);
        return l;
    }
    static String label(MidiDeviceInfo i) { String s = i.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME); return s == null ? "USB MIDI" : s; }

    void openUsb(MidiDeviceInfo info, Opened cb) { name = label(info); mm.openDevice(info, d -> attach(d, cb), new Handler(Looper.getMainLooper())); }
    void openBle(BluetoothDevice bt, String n, Opened cb) { name = n; mm.openBluetoothDevice(bt, d -> attach(d, cb), new Handler(Looper.getMainLooper())); }

    private void attach(MidiDevice d, Opened cb) {
        if (d == null) { cb.done(null, "could not open the MIDI device (is it paired/connected?)"); return; }
        dev = d; MidiDeviceInfo info = d.getInfo();
        if (info.getInputPortCount() < 1 || info.getOutputPortCount() < 1) { cb.done(null, "device has no MIDI ports"); return; }
        in = d.openInputPort(0); out = d.openOutputPort(0);
        if (in == null || out == null) { cb.done(null, "MIDI ports are busy"); return; }
        out.connect(new MidiReceiver() {
            @Override public void onSend(byte[] data, int offset, int count, long timestamp) { feed(data, offset, count); }
        });
        cb.done(this, null);
    }
    private synchronized void feed(byte[] d, int off, int n) {
        for (int i = off; i < off + n; i++) {
            int b = d[i] & 0xff;
            if (b == 0xf0) { sx.reset(); inSx = true; sx.write(b); }
            else if (inSx && b == 0xf7) { sx.write(b); replies.offer(sx.toByteArray()); inSx = false; sx.reset(); }
            else if (inSx && b < 0x80) sx.write(b);
            else if (b >= 0x80 && b < 0xf8) inSx = false;      // another status byte ends a broken SysEx
        }
    }
    @Override public void send(byte[] msg) { try { in.send(msg, 0, msg.length); } catch (Exception e) { /* surfaces as a missing answer */ } }
    @Override public byte[] receive(long timeoutMs) throws InterruptedException { return replies.poll(timeoutMs, TimeUnit.MILLISECONDS); }
    @Override public void drain() { replies.clear(); }
    void close() { try { if (in != null) in.close(); if (out != null) out.close(); if (dev != null) dev.close(); } catch (Exception e) { /* ignore */ } in = null; out = null; dev = null; }

    /** Scan for BLE MIDI peripherals for a few seconds. Needs the Bluetooth/location permissions to be granted already. */
    static Runnable scan(Context c, Found cb) {
        BluetoothManager bm = (BluetoothManager) c.getSystemService(Context.BLUETOOTH_SERVICE);
        BluetoothAdapter ad = bm == null ? null : bm.getAdapter();
        BluetoothLeScanner sc = ad == null || !ad.isEnabled() ? null : ad.getBluetoothLeScanner();
        if (sc == null) return () -> {};
        List<ScanFilter> f = new ArrayList<>(); f.add(new ScanFilter.Builder().setServiceUuid(MIDI_SERVICE).build());
        ScanCallback scb = new ScanCallback() {
            @Override public void onScanResult(int type, ScanResult r) {
                String n = r.getScanRecord() != null && r.getScanRecord().getDeviceName() != null ? r.getScanRecord().getDeviceName() : r.getDevice().getName();
                new Handler(Looper.getMainLooper()).post(() -> cb.on(r.getDevice(), n == null ? r.getDevice().getAddress() : n));
            }
        };
        try { sc.startScan(f, new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scb); } catch (SecurityException e) { return () -> {}; }
        return () -> { try { sc.stopScan(scb); } catch (Exception e) { /* ignore */ } };
    }
}
