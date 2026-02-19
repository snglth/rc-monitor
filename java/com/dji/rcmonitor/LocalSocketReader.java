package com.dji.rcmonitor;

import android.content.Context;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.util.Log;

import java.io.File;
import java.io.InputStream;

/**
 * Root-required reader that connects to dji_link's Unix domain socket
 * and feeds raw DUML frames through {@link RcMonitor#feed(byte[], int)}.
 *
 * The socket path (e.g. {@code /dev/socket/dji_xxx}) must be discovered
 * on-device and passed to the constructor.
 */
public class LocalSocketReader implements RcReader {
    private static final String TAG = "LocalSocketReader";
    private static final int BUFFER_SIZE = 1024;
    private static final int CONNECT_TIMEOUT_MS = 2000;

    private final Context context;
    private final String socketPath;
    private final RcMonitor monitor;

    private volatile boolean running;
    private Thread readThread;

    /**
     * @param context   Android context
     * @param socketPath Absolute path to the Unix domain socket
     */
    public LocalSocketReader(Context context, String socketPath) {
        this.context = context;
        this.socketPath = socketPath;
        this.monitor = new RcMonitor();
    }

    @Override
    public String getName() {
        return "LocalSocket";
    }

    @Override
    public boolean isAvailable() {
        File sock = new File(socketPath);
        if (!sock.exists()) return false;

        /* Try a quick connect to verify accessibility */
        LocalSocket probe = new LocalSocket();
        try {
            probe.connect(new LocalSocketAddress(socketPath,
                    LocalSocketAddress.Namespace.FILESYSTEM));
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            try { probe.close(); } catch (Exception ignored) {}
        }
    }

    @Override
    public boolean start(RcMonitor.RcStateListener listener) {
        if (running) return false;

        if (!monitor.init(listener)) {
            Log.e(TAG, "Failed to init native parser");
            return false;
        }

        LocalSocket socket = new LocalSocket();
        try {
            socket.connect(new LocalSocketAddress(socketPath,
                    LocalSocketAddress.Namespace.FILESYSTEM));
            socket.setSoTimeout(CONNECT_TIMEOUT_MS);
        } catch (Exception e) {
            Log.e(TAG, "Failed to connect to " + socketPath + ": " + e.getMessage());
            monitor.destroy();
            try { socket.close(); } catch (Exception ignored) {}
            return false;
        }

        running = true;
        final LocalSocket fSocket = socket;

        readThread = new Thread(() -> {
            byte[] buf = new byte[BUFFER_SIZE];
            Log.d(TAG, "LocalSocket read loop started: " + socketPath);

            try {
                InputStream in = fSocket.getInputStream();
                while (running) {
                    int n = in.read(buf);
                    if (n > 0) {
                        monitor.feed(buf, n);
                    } else if (n < 0) {
                        Log.w(TAG, "Socket EOF");
                        break;
                    }
                }
            } catch (Exception e) {
                if (running) {
                    Log.e(TAG, "Read error: " + e.getMessage());
                }
            } finally {
                monitor.destroy();
                try { fSocket.close(); } catch (Exception ignored) {}
                running = false;
                Log.d(TAG, "LocalSocket read loop stopped");
            }
        }, "rc-localsocket-reader");
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
}
