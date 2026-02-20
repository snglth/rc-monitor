package space.yasha.rcmonitor;

import android.util.Log;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Tries readers in priority order and activates the first one that starts.
 *
 * <pre>
 *   RcReaderChain chain = new RcReaderChain(
 *       new DussStreamReader(context),
 *       new LocalSocketReader(context, "/dev/socket/dji_xxx"),
 *       new InputEventReader(context),
 *       new UsbRcReader(context)
 *   );
 *   chain.start(listener);
 *   // ...
 *   chain.stop();
 * </pre>
 */
public class RcReaderChain {
    private static final String TAG = "RcReaderChain";

    private final List<RcReader> readers;
    private RcReader active;

    public RcReaderChain(RcReader... readers) {
        this.readers = new ArrayList<>(Arrays.asList(readers));
    }

    /**
     * Try each reader in order, return the first that starts successfully.
     * @return the active reader, or null if none could start
     */
    public RcReader start(RcMonitor.RcStateListener listener) {
        if (active != null) {
            active.stop();
            active = null;
        }
        for (RcReader reader : readers) {
            String name = reader.getName();
            if (!reader.isAvailable()) {
                Log.d(TAG, name + ": not available, skipping");
                continue;
            }
            Log.d(TAG, name + ": available, attempting start");
            if (reader.start(listener)) {
                Log.i(TAG, name + ": started successfully");
                active = reader;
                return reader;
            }
            Log.w(TAG, name + ": start failed");
        }
        Log.e(TAG, "No reader could start");
        return null;
    }

    /** Stop the active reader. */
    public void stop() {
        if (active != null) {
            active.stop();
            active = null;
        }
    }

    /** Get the currently active reader, or null. */
    public RcReader getActive() {
        return active;
    }

    /**
     * Get status of all readers.
     * Each entry contains: "name", "available", "active".
     */
    public List<Map<String, Object>> status() {
        List<Map<String, Object>> result = new ArrayList<>();
        for (RcReader reader : readers) {
            Map<String, Object> entry = new HashMap<>();
            entry.put("name", reader.getName());
            entry.put("available", reader.isAvailable());
            entry.put("active", reader == active && reader.isRunning());
            result.add(entry);
        }
        return result;
    }
}
