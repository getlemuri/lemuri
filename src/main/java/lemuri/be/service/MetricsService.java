package lemuri.be.service;

import java.util.List;
import java.util.function.Consumer;
import lemuri.be.dto.MetricsResult;
import lemuri.be.dto.Host;
import lemuri.be.dto.Updates;

/** CPU, memory and disks of the VPSs, read over SSH. */
public interface MetricsService {
    /** CPU samples kept for the summary chart. */
    int SAMPLES = 30;

    /**
     * Reads the resources in the background and gives the outcome to {@code outcome}, from a worker thread.
     * After an error there is no retry for a while, unless with {@code force}: many failed
     * logins trigger fail2ban.
     */
    void refresh(Host h, boolean force, Consumer<MetricsResult> outcome);

    /**
     * Pending updates and reboot, in the background. Counting packages takes the server a few seconds: the
     * answer is kept for a while and given again, unless with {@code force}.
     */
    default void updates(Host h, boolean force, Consumer<Updates> outcome) {
    }

    boolean running(String hostId);

    /** The last outcome, or null if the host has never been read. */
    MetricsResult last(String hostId);

    List<Double> cpuHistory(String hostId);

    /** The host was deleted: its connection is closed at once and what was known about it is forgotten. */
    void forget(String hostId);

    void stop();
}
