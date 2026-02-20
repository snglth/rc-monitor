package com.dji.rcmonitor;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Reads stick data from {@code /dev/input/event*} and synthesizes 17-byte
 * RC payloads fed through {@link RcMonitor#feedDirect(byte[], int)}.
 *
 * This reader provides partial data — analog sticks only, no proprietary
 * buttons (those travel over DUML, not evdev). Requires read access to the
 * input device, which typically means root.
 *
 * <h3>Input event format (arm64)</h3>
 * Each {@code struct input_event} is 24 bytes:
 * <pre>
 *   uint64_t  tv_sec    (8 bytes)
 *   uint64_t  tv_usec   (8 bytes)
 *   uint16_t  type
 *   uint16_t  code
 *   int32_t   value
 * </pre>
 *
 * <h3>Stick mapping</h3>
 * EV_ABS axis codes are mapped to the four sticks. The raw input values
 * are scaled to rc-monitor's centered-at-0 range (~-660..+660) using a
 * configurable scale factor.
 */
public class InputEventReader implements RcReader {
    private static final String TAG = "InputEventReader";
    private static final String DEFAULT_DEVICE = "/dev/input/event8";
    private static final int INPUT_EVENT_SIZE = 24; /* arm64 struct input_event */

    /* linux/input-event-codes.h */
    private static final int EV_SYN = 0x00;
    private static final int EV_ABS = 0x03;

    /* Common ABS axis codes — may need adjustment per device */
    private static final int ABS_X  = 0x00; /* right stick horizontal */
    private static final int ABS_Y  = 0x01; /* right stick vertical */
    private static final int ABS_Z  = 0x02; /* left stick vertical */
    private static final int ABS_RZ = 0x05; /* left stick horizontal */

    /**
     * Default scale factor: maps raw input range center (assumed ±32768)
     * to rc-monitor range (±660). Adjust via {@link #setScaleFactor(double)}
     * if the device reports a different axis range.
     */
    private static final double DEFAULT_SCALE = 660.0 / 32768.0;

    private final Context context;
    private final String devicePath;
    private final RcMonitor monitor;
    private double scaleFactor = DEFAULT_SCALE;

    private volatile boolean running;
    private Thread readThread;
    private volatile FileInputStream activeStream;

    /* Running stick state, updated on each EV_ABS event */
    private int stickRightH;
    private int stickRightV;
    private int stickLeftV;
    private int stickLeftH;

    public InputEventReader(Context context) {
        this(context, DEFAULT_DEVICE);
    }

    public InputEventReader(Context context, String devicePath) {
        this.context = context;
        this.devicePath = devicePath;
        this.monitor = new RcMonitor();
    }

    /**
     * Set the scale factor mapping raw input values to rc-monitor's ±660 range.
     * Default assumes a ±32768 raw range. For example, if the device reports
     * 0..1024 centered at 512, use {@code 660.0 / 512.0}.
     */
    public void setScaleFactor(double factor) {
        this.scaleFactor = factor;
    }

    @Override
    public String getName() {
        return "InputEvent";
    }

    @Override
    public boolean isAvailable() {
        File dev = new File(devicePath);
        return dev.exists() && dev.canRead();
    }

    @Override
    public boolean start(RcMonitor.RcStateListener listener) {
        if (running) return false;

        File dev = new File(devicePath);
        if (!dev.exists()) {
            Log.e(TAG, "Device not found: " + devicePath);
            return false;
        }

        if (!monitor.init(listener)) {
            Log.e(TAG, "Failed to init native parser");
            return false;
        }

        running = true;
        stickRightH = 0;
        stickRightV = 0;
        stickLeftV = 0;
        stickLeftH = 0;

        readThread = new Thread(() -> {
            Log.d(TAG, "Input event read loop started: " + devicePath);
            try (FileInputStream fis = new FileInputStream(devicePath)) {
                activeStream = fis;
                byte[] eventBuf = new byte[INPUT_EVENT_SIZE];
                ByteBuffer bb = ByteBuffer.wrap(eventBuf).order(ByteOrder.LITTLE_ENDIAN);

                while (running) {
                    int total = 0;
                    while (total < INPUT_EVENT_SIZE) {
                        int n = fis.read(eventBuf, total, INPUT_EVENT_SIZE - total);
                        if (n < 0) {
                            Log.w(TAG, "Device EOF");
                            running = false;
                            break;
                        }
                        total += n;
                    }
                    if (!running) break;

                    bb.rewind();
                    bb.getLong(); /* tv_sec */
                    bb.getLong(); /* tv_usec */
                    int type = bb.getShort() & 0xFFFF;
                    int code = bb.getShort() & 0xFFFF;
                    int value = bb.getInt();

                    if (type == EV_ABS) {
                        int scaled = (int) (value * scaleFactor);
                        switch (code) {
                            case ABS_X:  stickRightH = scaled; break;
                            case ABS_Y:  stickRightV = scaled; break;
                            case ABS_Z:  stickLeftV  = scaled; break;
                            case ABS_RZ: stickLeftH  = scaled; break;
                        }
                    } else if (type == EV_SYN) {
                        byte[] payload = buildPayload();
                        monitor.feedDirect(payload, payload.length);
                    }
                }
            } catch (Exception e) {
                if (running) {
                    Log.e(TAG, "Read error: " + e.getMessage());
                }
            } finally {
                activeStream = null;
                monitor.destroy();
                running = false;
                Log.d(TAG, "Input event read loop stopped");
            }
        }, "rc-input-reader");
        readThread.setDaemon(true);
        readThread.start();

        return true;
    }

    @Override
    public void stop() {
        running = false;
        /* Close stream to unblock any pending read */
        FileInputStream stream = activeStream;
        if (stream != null) {
            try { stream.close(); } catch (Exception ignored) {}
        }
        if (readThread != null) {
            try {
                readThread.join(2000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            readThread = null;
        }
    }

    @Override
    public boolean isRunning() {
        return running;
    }

    /**
     * Build a 17-byte RC push payload from the current stick state.
     * Bytes 0–4 are zeroed (no button data from evdev).
     * Bytes 5–16 are six uint16 LE values with 0x400 (1024) center offset.
     *
     * Layout matches rcm_parse_payload expectations:
     *   [5..6]   right stick H   (uint16 LE, centered at 0x400)
     *   [7..8]   right stick V   (uint16 LE, centered at 0x400)
     *   [9..10]  left stick V    (uint16 LE, centered at 0x400)
     *   [11..12] left stick H    (uint16 LE, centered at 0x400)
     *   [13..14] left wheel      (uint16 LE, centered at 0x400)
     *   [15..16] right wheel     (uint16 LE, centered at 0x400)
     */
    private byte[] buildPayload() {
        byte[] p = new byte[17];
        int center = 0x400;

        putLE16(p, 5,  center + stickRightH);
        putLE16(p, 7,  center + stickRightV);
        putLE16(p, 9,  center + stickLeftV);
        putLE16(p, 11, center + stickLeftH);
        putLE16(p, 13, center); /* left wheel — no evdev source */
        putLE16(p, 15, center); /* right wheel — no evdev source */

        return p;
    }

    private static void putLE16(byte[] buf, int offset, int value) {
        buf[offset]     = (byte) (value & 0xFF);
        buf[offset + 1] = (byte) ((value >> 8) & 0xFF);
    }
}
