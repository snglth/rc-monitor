package space.yasha.rcmonitor;

import android.content.Context;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.util.Log;

/**
 * No-root reader targeting USB Interface 7 on the RM510B, which streams
 * DUML data freely at ~20KB/s when dji_link owns the CDC ACM interfaces.
 *
 * Unlike {@link UsbRcReader}, this reader opens a specific interface index
 * (rather than searching for bulk IN+OUT) and does not perform CDC ACM
 * handshake or send enable commands — Interface 7 streams without them.
 *
 * Raw bytes are fed through {@link RcMonitor#feed(byte[], int)} so the
 * DUML parser filters noise via CRC validation.
 */
public class DussStreamReader implements RcReader {
    private static final String TAG = "DussStreamReader";
    private static final int DUSS_INTERFACE_INDEX = 7;
    private static final int READ_TIMEOUT_MS = 100;
    private static final int BUFFER_SIZE = 1024;
    private static final int HEX_LOG_INTERVAL_MS = 5000;

    private final Context context;
    private final RcMonitor monitor;

    private volatile boolean running;
    private Thread readThread;

    public DussStreamReader(Context context) {
        this.context = context;
        this.monitor = new RcMonitor();
    }

    @Override
    public String getName() {
        return "DUSS";
    }

    @Override
    public boolean isAvailable() {
        UsbDevice device = findDjiDevice();
        if (device == null) return false;
        if (device.getInterfaceCount() <= DUSS_INTERFACE_INDEX) return false;

        UsbInterface iface = device.getInterface(DUSS_INTERFACE_INDEX);
        return findBulkIn(iface) != null;
    }

    @Override
    public boolean start(RcMonitor.RcStateListener listener) {
        if (running) return false;

        UsbDevice device = findDjiDevice();
        if (device == null) {
            Log.e(TAG, "No DJI USB device found");
            return false;
        }

        if (device.getInterfaceCount() <= DUSS_INTERFACE_INDEX) {
            Log.e(TAG, "Device has only " + device.getInterfaceCount() +
                       " interfaces, need index " + DUSS_INTERFACE_INDEX);
            return false;
        }

        UsbManager usbManager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (usbManager == null || !usbManager.hasPermission(device)) {
            Log.e(TAG, "No USB permission for device");
            return false;
        }

        UsbInterface iface = device.getInterface(DUSS_INTERFACE_INDEX);
        UsbEndpoint bulkIn = findBulkIn(iface);
        if (bulkIn == null) {
            Log.e(TAG, "No bulk IN endpoint on interface " + DUSS_INTERFACE_INDEX);
            return false;
        }

        UsbDeviceConnection conn = usbManager.openDevice(device);
        if (conn == null) {
            Log.e(TAG, "Failed to open USB device");
            return false;
        }

        if (!conn.claimInterface(iface, true)) {
            Log.e(TAG, "Failed to claim interface " + DUSS_INTERFACE_INDEX);
            conn.close();
            return false;
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
        final UsbInterface fIface = iface;

        readThread = new Thread(() -> {
            byte[] buf = new byte[BUFFER_SIZE];
            long lastHexLog = 0;
            Log.d(TAG, "DUSS read loop started on interface " + DUSS_INTERFACE_INDEX);

            while (running) {
                int n = fConn.bulkTransfer(fBulkIn, buf, buf.length, READ_TIMEOUT_MS);
                if (n > 0) {
                    monitor.feed(buf, n);

                    /* Periodic hex dump to logcat for diagnostics */
                    long now = System.currentTimeMillis();
                    if (now - lastHexLog > HEX_LOG_INTERVAL_MS) {
                        logHexSample(buf, n);
                        lastHexLog = now;
                    }
                }
            }

            monitor.destroy();
            fConn.releaseInterface(fIface);
            fConn.close();
            Log.d(TAG, "DUSS read loop stopped");
        }, "rc-duss-reader");
        readThread.setDaemon(true);
        readThread.start();

        return true;
    }

    @Override
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

    @Override
    public boolean isRunning() {
        return running;
    }

    private UsbDevice findDjiDevice() {
        UsbManager usbManager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (usbManager == null) return null;

        UsbDevice fallback = null;
        for (UsbDevice device : usbManager.getDeviceList().values()) {
            if (device.getVendorId() == RcMonitor.DJI_USB_VID) {
                int pid = device.getProductId();
                if (pid == RcMonitor.DJI_USB_PID_INTERNAL) {
                    return device; /* prefer internal pigeon device */
                }
                if (pid == RcMonitor.DJI_USB_PID_ACTIVE ||
                    pid == RcMonitor.DJI_USB_PID_INIT) {
                    fallback = device;
                }
            }
        }
        return fallback;
    }

    private static UsbEndpoint findBulkIn(UsbInterface iface) {
        for (int j = 0; j < iface.getEndpointCount(); j++) {
            UsbEndpoint ep = iface.getEndpoint(j);
            if (ep.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK &&
                ep.getDirection() == UsbConstants.USB_DIR_IN) {
                return ep;
            }
        }
        return null;
    }

    private static void logHexSample(byte[] buf, int len) {
        int limit = Math.min(len, 64);
        StringBuilder sb = new StringBuilder(limit * 3);
        for (int i = 0; i < limit; i++) {
            if (i > 0) sb.append(' ');
            sb.append(String.format("%02X", buf[i] & 0xFF));
        }
        if (len > limit) sb.append("...");
        Log.d(TAG, "Sample (" + len + "B): " + sb);
    }
}
