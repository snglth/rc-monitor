package com.dji.rcmonitor;

import android.content.Context;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.util.Log;

/**
 * Reads raw USB bulk data from a DJI RM510 remote controller and feeds it
 * to RcMonitor for DUML parsing.
 *
 * Handles the full CDC ACM handshake sequence:
 * 1. Finds the Protocol interface (with both bulk IN and OUT endpoints)
 * 2. Configures CDC ACM line coding (115200 baud, 8N1) and control lines (DTR+RTS)
 * 3. Sends the DUML enable command to start push data streaming
 * 4. Reads incoming DUML frames and feeds them to the parser
 * 5. Falls back to polling with channel requests if no push data arrives
 *
 * Usage:
 * <pre>
 *   UsbRcReader reader = new UsbRcReader(context);
 *   reader.start(new RcMonitor.SimpleListener() {
 *       public void onState(RcMonitor.RcState state) {
 *           Log.d("RC", "Shutter: " + state.shutter +
 *                       " RightH: " + state.stickRightH);
 *       }
 *   });
 *   // ...
 *   reader.stop();
 * </pre>
 */
public class UsbRcReader {
    private static final String TAG = "UsbRcReader";
    private static final int READ_TIMEOUT_MS = 100;
    private static final int BUFFER_SIZE = 1024;

    /* CDC ACM control request types and codes */
    private static final int USB_RT_ACM = 0x21; /* host-to-device, class, interface */
    private static final int SET_LINE_CODING = 0x20;
    private static final int SET_CONTROL_LINE_STATE = 0x22;

    /* Timeout for push data before falling back to polling (ms) */
    private static final int PUSH_TIMEOUT_MS = 2000;
    /* Interval between poll requests when in polling mode (ms) */
    private static final int POLL_INTERVAL_MS = 50;

    private final Context context;
    private final RcMonitor monitor;

    private volatile boolean running;
    private Thread readThread;

    public UsbRcReader(Context context) {
        this.context = context;
        this.monitor = new RcMonitor();
    }

    /**
     * Find the first connected DJI USB device.
     * Checks for both init-mode (PID 0x0040) and active-mode (PID 0x1020) PIDs.
     * @return UsbDevice or null if not found
     */
    public UsbDevice findDjiDevice() {
        UsbManager usbManager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (usbManager == null) return null;

        for (UsbDevice device : usbManager.getDeviceList().values()) {
            if (device.getVendorId() == RcMonitor.DJI_USB_VID) {
                int pid = device.getProductId();
                if (pid == RcMonitor.DJI_USB_PID_ACTIVE ||
                    pid == RcMonitor.DJI_USB_PID_INIT) {
                    return device;
                }
            }
        }
        return null;
    }

    /**
     * Find the CDC ACM data interface with both bulk IN and OUT endpoints.
     * This is the Protocol interface used for DUML communication.
     */
    private static UsbInterface findProtocolInterface(UsbDevice device) {
        for (int i = 0; i < device.getInterfaceCount(); i++) {
            UsbInterface candidate = device.getInterface(i);
            boolean hasBulkIn = false;
            boolean hasBulkOut = false;

            for (int j = 0; j < candidate.getEndpointCount(); j++) {
                UsbEndpoint ep = candidate.getEndpoint(j);
                if (ep.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK) {
                    if (ep.getDirection() == UsbConstants.USB_DIR_IN) hasBulkIn = true;
                    if (ep.getDirection() == UsbConstants.USB_DIR_OUT) hasBulkOut = true;
                }
            }

            if (hasBulkIn && hasBulkOut) {
                return candidate;
            }
        }
        return null;
    }

    /**
     * Get the bulk endpoint in the given direction from an interface.
     */
    private static UsbEndpoint findBulkEndpoint(UsbInterface iface, int direction) {
        for (int j = 0; j < iface.getEndpointCount(); j++) {
            UsbEndpoint ep = iface.getEndpoint(j);
            if (ep.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK &&
                ep.getDirection() == direction) {
                return ep;
            }
        }
        return null;
    }

    /**
     * Send CDC ACM SET_LINE_CODING control transfer.
     * Configures 115200 baud, 8 data bits, 1 stop bit, no parity.
     */
    private static boolean setLineCoding(UsbDeviceConnection conn, int interfaceNum) {
        /* Line coding structure (7 bytes):
         *   dwDTERate   (4 bytes LE) = 115200 = 0x0001C200
         *   bCharFormat (1 byte)     = 0 (1 stop bit)
         *   bParityType (1 byte)     = 0 (none)
         *   bDataBits   (1 byte)     = 8
         */
        byte[] lineCoding = {
            (byte) 0x00, (byte) 0xC2, (byte) 0x01, (byte) 0x00, /* 115200 LE */
            0x00,  /* 1 stop bit */
            0x00,  /* no parity */
            0x08   /* 8 data bits */
        };

        int ret = conn.controlTransfer(
            USB_RT_ACM, SET_LINE_CODING, 0, interfaceNum,
            lineCoding, lineCoding.length, 1000);
        return ret >= 0;
    }

    /**
     * Send CDC ACM SET_CONTROL_LINE_STATE to assert DTR and RTS.
     */
    private static boolean setControlLineState(UsbDeviceConnection conn, int interfaceNum) {
        /* wValue: bit 0 = DTR, bit 1 = RTS -> 0x0003 */
        int ret = conn.controlTransfer(
            USB_RT_ACM, SET_CONTROL_LINE_STATE, 0x0003, interfaceNum,
            null, 0, 1000);
        return ret >= 0;
    }

    /**
     * Start reading from the DJI USB device on a background thread.
     * Performs CDC ACM setup and sends the DUML enable command before reading.
     * @param listener Callback for RC state updates (called on the read thread)
     * @return true if started successfully
     */
    public boolean start(RcMonitor.RcStateListener listener) {
        if (running) return false;

        UsbDevice device = findDjiDevice();
        if (device == null) {
            Log.e(TAG, "No DJI USB device found");
            return false;
        }

        UsbManager usbManager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (usbManager == null || !usbManager.hasPermission(device)) {
            Log.e(TAG, "No USB permission for device");
            return false;
        }

        /* Find the Protocol interface with both bulk IN and OUT */
        UsbInterface iface = findProtocolInterface(device);
        if (iface == null) {
            Log.e(TAG, "No Protocol interface with bulk IN+OUT found");
            return false;
        }

        UsbEndpoint bulkIn = findBulkEndpoint(iface, UsbConstants.USB_DIR_IN);
        UsbEndpoint bulkOut = findBulkEndpoint(iface, UsbConstants.USB_DIR_OUT);
        if (bulkIn == null || bulkOut == null) {
            Log.e(TAG, "Missing bulk endpoint");
            return false;
        }

        UsbDeviceConnection conn = usbManager.openDevice(device);
        if (conn == null) {
            Log.e(TAG, "Failed to open USB device");
            return false;
        }

        if (!conn.claimInterface(iface, true)) {
            Log.e(TAG, "Failed to claim interface");
            conn.close();
            return false;
        }

        /* CDC ACM setup: line coding + control lines */
        int ifaceNum = iface.getId();
        if (!setLineCoding(conn, ifaceNum)) {
            Log.w(TAG, "SET_LINE_CODING failed (may be non-fatal)");
        }
        if (!setControlLineState(conn, ifaceNum)) {
            Log.w(TAG, "SET_CONTROL_LINE_STATE failed (may be non-fatal)");
        }

        if (!monitor.init(listener)) {
            Log.e(TAG, "Failed to init native parser");
            conn.releaseInterface(iface);
            conn.close();
            return false;
        }

        running = true;
        final UsbDeviceConnection fConn = conn;
        final UsbEndpoint fBulkIn = bulkIn;
        final UsbEndpoint fBulkOut = bulkOut;
        final UsbInterface fIface = iface;

        readThread = new Thread(() -> {
            byte[] buf = new byte[BUFFER_SIZE];
            int seq = 1;
            Log.d(TAG, "USB read loop started");

            /* Send enable command to start push data streaming */
            byte[] enableCmd = RcMonitor.buildEnableCommand(seq++);
            if (enableCmd != null) {
                int sent = fConn.bulkTransfer(fBulkOut, enableCmd, enableCmd.length, 1000);
                if (sent >= 0) {
                    Log.d(TAG, "Enable command sent (" + sent + " bytes)");
                } else {
                    Log.w(TAG, "Failed to send enable command");
                }
            }

            boolean pushMode = true;
            long lastDataTime = System.currentTimeMillis();
            long lastPollTime = 0;

            while (running) {
                int n = fConn.bulkTransfer(fBulkIn, buf, buf.length, READ_TIMEOUT_MS);
                if (n > 0) {
                    monitor.feed(buf, n);
                    lastDataTime = System.currentTimeMillis();
                    pushMode = true;
                } else {
                    /* No data received - check if we should switch to polling */
                    long now = System.currentTimeMillis();
                    if (now - lastDataTime > PUSH_TIMEOUT_MS) {
                        pushMode = false;
                    }

                    if (!pushMode && now - lastPollTime > POLL_INTERVAL_MS) {
                        /* Send channel request to poll for data */
                        byte[] pollCmd = RcMonitor.buildChannelRequest(seq++);
                        if (pollCmd != null) {
                            fConn.bulkTransfer(fBulkOut, pollCmd, pollCmd.length, 100);
                        }
                        lastPollTime = now;
                    }
                }
            }

            monitor.destroy();
            fConn.releaseInterface(fIface);
            fConn.close();
            Log.d(TAG, "USB read loop stopped");
        }, "rc-usb-reader");
        readThread.setDaemon(true);
        readThread.start();

        return true;
    }

    /**
     * Stop reading and release USB resources.
     */
    public void stop() {
        running = false;
        if (readThread != null) {
            try {
                readThread.join(2000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            readThread = null;
        }
    }

    public boolean isRunning() {
        return running;
    }
}
