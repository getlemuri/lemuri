package lemuri.fe.c;

import java.util.function.BiFunction;
import java.util.function.Function;
import javafx.application.Platform;
import lemuri.be.dto.Host;
import lemuri.be.service.HostRepository;
import lemuri.be.service.MetricsService;
import lemuri.be.service.ProbeService;
import lemuri.be.service.SharingService;
import lemuri.be.service.Transport;
import lemuri.be.service.Vault;
import lemuri.be.terminal.SshTransport;
import lemuri.fe.m.AppState;

/**
 * What the views receive: the state to read and observe, the controllers to
 * change it and the vault. The backend services come from outside
 * (see {@link lemuri.App}), so this depends only on the interfaces.
 */
public final class AppContext {
    public final AppState state = new AppState();
    public final Vault vault;
    public final HostController hosts;
    public final SessionController sessions;
    public final MonitorController monitor;
    public final SharingController sharing;

    public AppContext(HostRepository repository, Vault vault, MetricsService metrics,
                      ProbeService probe, SharingService sharing, Function<Host, SshTransport.UserPrompts> requests) {
        this(repository, vault, metrics, probe, sharing, (state, hosts) -> new SessionController(state, hosts, requests));
    }

    private AppContext(HostRepository repository, Vault vault, MetricsService metrics, ProbeService probe, SharingService sharing,
                       BiFunction<AppState, HostController, SessionController> sessions) {
        this.vault = vault;
        this.hosts = new HostController(state, repository, vault);
        this.sessions = sessions.apply(state, hosts);
        this.monitor = new MonitorController(state, hosts, metrics, probe);
        this.sharing = new SharingController(state, sharing);
    }

    /** For the interface tests: sessions get the transports made by {@code transports}, never a real SSH connection. */
    public static AppContext withTransports(HostRepository repository, Vault vault, MetricsService metrics, ProbeService probe,
                                            SharingService sharing, Function<Host, Transport> transports) {
        return new AppContext(repository, vault, metrics, probe, sharing,
                (state, hosts) -> new SessionController(state, hosts, transports, Platform::runLater));
    }

    /** On close: stops the services and closes the sessions. */
    public void stop() {
        monitor.stop();
        sharing.stop();
        sessions.closeAll();
    }
}
