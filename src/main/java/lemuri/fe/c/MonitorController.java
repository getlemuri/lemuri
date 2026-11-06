package lemuri.fe.c;

import java.util.List;
import java.util.function.Consumer;
import java.util.prefs.Preferences;
import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.application.Platform;
import javafx.beans.Observable;
import javafx.util.Duration;
import lemuri.be.dto.Host;
import lemuri.be.dto.MetricsResult;
import lemuri.be.dto.Security;
import lemuri.be.dto.Updates;
import lemuri.be.service.MetricsService;
import lemuri.be.service.ProbeService;
import lemuri.be.service.Vault;
import lemuri.be.terminal.Jumps;
import lemuri.fe.m.AppState;

/**
 * Reachability and resources of the hosts: asks the services for the
 * readings and brings the outcomes into the state, on the JavaFX thread.
 */
public final class MonitorController {
    private static final Preferences PREF = Preferences.userRoot().node("lemuri/ui");
    private static final String PROBE_MINUTES = "probeMinutes";
    /** How often the disk bars of the tree are brought up to date. */
    private static final long READ_EVERY_MS = 5 * 60_000;
    /** A host that refused the last reading is left alone longer: failed logins trigger fail2ban. */
    private static final long RETRY_AFTER_ERROR_MS = 30 * 60_000;
    private final AppState state;
    private final HostController hosts;
    private final MetricsService metrics;
    private final ProbeService probe;
    private final Vault vault;
    /** The check is cheap and often, so that unlocking the vault shows up soon; the readings keep their own pace. */
    private final Timeline readings = new Timeline(new KeyFrame(Duration.seconds(30), e -> readDue()));
    private boolean vaultWasOpen;

    public MonitorController(AppState state, HostController hosts, MetricsService metrics, ProbeService probe, Vault vault) {
        this.vault = vault;
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
        state.hosts.addListener((Observable o) -> readDue());
        readings.setCycleCount(Timeline.INDEFINITE);
        readings.play();
        readDue();
    }

    /**
     * Reads the hosts with no reading or an old one, for the disk bars of the tree. Hosts received from
     * contacts are read only when their list is opened; with the probe turned off, nothing runs on its own.
     * Unlocking the vault gives the hosts that failed a new try at once: their passwords may have been missing.
     */
    private void readDue() {
        if (probeMinutes() == 0) return;
        boolean unlocked = vault.isOpen() && !vaultWasOpen;
        vaultWasOpen = vault.isOpen();
        long now = System.currentTimeMillis();
        for (Host h : state.hosts) {
            if (h.received) continue;
            MetricsResult e = state.resources.get(h.id);
            if (e == null || now - e.when() >= (e.error() == null ? READ_EVERY_MS : unlocked ? 0 : RETRY_AFTER_ERROR_MS)) refreshMetrics(h, false);
        }
    }

    /** The host was deleted: the silent metrics connection is closed now, not on exit. */
    public void forget(String hostId) {
        metrics.forget(hostId);
        state.resources.remove(hostId);
    }

    public void stop() {
        readings.stop();
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

    /** How safe the SSH access to a host is; the answer arrives on the JavaFX thread. */
    public void security(Host h, boolean force, Consumer<Security> outcome) {
        metrics.security(Jumps.resolve(h, state.hosts), force, s -> Platform.runLater(() -> outcome.accept(s)));
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
