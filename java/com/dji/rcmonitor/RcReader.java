package com.dji.rcmonitor;

/**
 * Abstraction for RC data sources. Implementations read raw DUML frames
 * or decoded payloads from different transports (USB, DUSS, local socket,
 * input events) and deliver parsed RC state through the listener callback.
 */
public interface RcReader {
    /** Human-readable name for this reader (e.g. "USB", "DUSS"). */
    String getName();

    /**
     * Start reading on a background thread.
     * @param listener Callback for RC state updates (called on the read thread)
     * @return true if started successfully
     */
    boolean start(RcMonitor.RcStateListener listener);

    /** Stop reading and release resources. */
    void stop();

    /** Whether the read loop is currently active. */
    boolean isRunning();

    /**
     * Quick check whether this reader's data source is likely reachable.
     * May perform I/O (e.g. device enumeration, file stat) so avoid calling
     * on the main thread in tight loops.
     */
    boolean isAvailable();
}
