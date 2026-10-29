package lemuri.fe.c;

import java.util.List;
import java.util.function.Consumer;
import java.util.prefs.Preferences;
import javafx.application.Platform;
import javafx.beans.Observable;
import lemuri.be.dto.Host;
import lemuri.be.dto.Updates;
import lemuri.be.service.MetricsService;
import lemuri.be.service.ProbeService;
import lemuri.be.terminal.Jumps;
import lemuri.fe.m.AppState;

/**
 * Reachability and resources of the hosts: asks the services for the
 * readings and brings the outcomes into the state, on the JavaFX thread.
 */
public final class MonitorController {
    private static final Preferences PREF = Preferences.userRoot().node("lemuri/ui");
    private static final String PROBE_MINUTES = "probeMinutes";
    private final AppState state;
    private final HostController hosts;
    private final MetricsService metrics;
    private final ProbeService probe;

    public MonitorController(AppState state, HostController hosts, MetricsService metrics, ProbeService probe) {
        this.state = state;
        this.hosts = hosts;
        this.metrics = metrics;
        this.probe = probe;
    }

    /** Starts the probe: right away and at every change of the host list, unless the user turned it off. */
    public void launch() {
        probe.every(probeMinutes());
        probe.launch(statuses -> Platform.runLater(() -> hosts.refreshStatuses(statuses)));
        state.hosts.addListener((Observable o) -> probe.list(withRoutes()));
        probe.list(withRoutes());
    }

    /** The host was deleted: the silent metrics connection is closed now, not on exit. */
    public void forget(String hostId) {
        metrics.forget(hostId);
        state.resources.remove(hostId);
    }

    public void stop() {
        probe.stop();
        metrics.stop();
    }

    /** Minutes between two reachability checks, as the user chose; 0: never on its own. */
    public static int probeMinutes() {
        return PREF.getInt(PROBE_MINUTES, ProbeService.DEFAULT_MINUTES);
    }

    public void probeMinutes(int minutes) {
        PREF.putInt(PROBE_MINUTES, minutes);
        probe.every(minutes);
    }

    /** Tries the reachability of all hosts again right away. */
    public void tryAll() {
        probe.all();
    }

    /** With {@code force} a host that has just failed is tried again too. */
    public void refreshMetrics(Host h, boolean force) {
        metrics.refresh(Jumps.resolve(h, state.hosts), force, e -> Platform.runLater(() -> state.resources.put(h.id, e)));
    }

    /** Pending updates and reboot of a host; the answer arrives on the JavaFX thread. */
    public void updates(Host h, boolean force, Consumer<Updates> outcome) {
        metrics.updates(Jumps.resolve(h, state.hosts), force, u -> Platform.runLater(() -> outcome.accept(u)));
    }

    public boolean metricsRunning(String hostId) {
        return metrics.running(hostId);
    }

    public List<Double> cpuHistory(String hostId) {
        return metrics.cpuHistory(hostId);
    }

    /** The hosts with their jump hosts resolved: the probe tries the first hop of a host behind a bastion. */
    private List<Host> withRoutes() {
        return state.hosts.stream().map(h -> Jumps.resolve(h, state.hosts)).toList();
    }
}
