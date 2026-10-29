package lemuri.fe.c;

import java.util.List;
import java.util.Map;
import java.util.function.Consumer;
import java.util.function.Function;
import javafx.application.Platform;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.be.dto.SessionStatus;
import lemuri.be.service.Transport;
import lemuri.be.terminal.Jumps;
import lemuri.be.terminal.SshTransport;
import lemuri.be.util.I18n;
import lemuri.fe.m.AppState;
import lemuri.fe.m.Pane;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Session;
import lemuri.fe.m.Task;

/** Opens and closes the SSH sessions. From the JavaFX thread. */
public final class SessionController {
    private final AppState state;
    private final HostController hosts;
    private final Function<Host, Transport> ssh;
    /** Runs on the JavaFX thread: the transports call back from their own threads. */
    private final Consumer<Runnable> fx;

    /** {@code requests}: the questions of the SSH connection (fingerprint, password…), asked by the view with its dialogs. */
    public SessionController(AppState state, HostController hosts, Function<Host, SshTransport.UserPrompts> requests) {
        this(state, hosts, h -> new SshTransport(h, requests), Platform::runLater);
    }

    /** For tests: transports made elsewhere, and callbacks run where the test wants. */
    SessionController(AppState state, HostController hosts, Function<Host, Transport> ssh, Consumer<Runnable> fx) {
        this.state = state;
        this.hosts = hosts;
        this.ssh = ssh;
        this.fx = fx;
    }

    private Session createSession(Host h) {
        return createSession(h, null, null);
    }

    /**
     * {@code owner}: the tab the new session is a part of, null for a new tab; {@code place} puts it in the
     * tab's layout before the views hear of it.
     */
    private Session createSession(Host h, Session owner, Consumer<Session> place) {
        long n = state.sessions.stream().filter(s -> s.host.id.equals(h.id)).count() + 1;
        Task t = state.task(h.name, I18n.t("task.sshSession", h.destination()));
        // The transport gets the host with its jump hosts resolved from the list as it is now.
        Transport tr = ssh.apply(Jumps.resolve(h, state.hosts));
        if (tr instanceof SshTransport st) st.onOs(os -> fx.accept(() -> hosts.refreshOs(h.id, os)));
        Session s = new Session(AppState.uid(), h, h.name + " #" + n, tr);
        s.owner(owner);
        tr.onState((state, msg) -> fx.accept(() -> {
            s.state.set(state);
            if (state == SessionStatus.CONNECTED) {
                t.finish(Task.Outcome.OK, null);
                hosts.refreshStatuses(Map.of(h.id, HostStatus.ONLINE));
            }
            if (state == SessionStatus.ERROR || state == SessionStatus.CLOSED) t.finish(Task.Outcome.ERROR, msg != null ? msg : I18n.t("task.connectionClosed"));
            // A part of a split tab goes away when its shell ends ("exit"), as in the split terminals; a tab stays to show why.
            if (state == SessionStatus.CLOSED && s.tab().panes().size() > 1 && s.tab().panes().contains(s) && this.state.sessions.contains(s)) closePane(s);
        }));
        if (owner == null) {
            state.sessions.add(s);
            state.activeSession.put(h.id, s);
        } else {
            place.accept(s);
            // Right after the other parts of its tab: if the tab closes, one of them is already in its place in the row.
            int after = owner.panes().stream().filter(p -> p != s).mapToInt(state.sessions::indexOf).max().orElse(state.sessions.size() - 1);
            state.sessions.add(after + 1, s);
        }
        return s;
    }

    /** Opens a session and goes to the host console. */
    public Session openSession(Host h) {
        Session s = createSession(h);
        state.select(new Selection.HostItem(h.id), "console");
        return s;
    }

    /**
     * Goes to the console of the host: to its session if one is open and alive (the last one used),
     * otherwise opens a new one. A new session on a host that has one is asked with the "+" of the tabs.
     */
    public Session showOrOpen(Host h) {
        Session s = state.activeSession.get(h.id);
        if (s == null || !state.sessions.contains(s) || !s.state.get().alive()) {
            s = state.sessions.stream().filter(x -> x.owner() == null && x.host.id.equals(h.id) && x.state.get().alive())
                    .reduce((a, b) -> b).orElse(null);
        }
        if (s == null) return openSession(h);
        showSession(s);
        return s;
    }

    /**
     * A new session on the same host, in the same tab as {@code target}: on its right ({@code sideBySide}) or
     * below it. The tab stays where it is.
     */
    public Session split(Session target, boolean sideBySide) {
        return split(target, sideBySide, target.host);
    }

    /** Like {@link #split(Session, boolean)}, but the new part connects to another host. */
    public Session split(Session target, boolean sideBySide, Host host) {
        Session tab = target.tab();
        if (tab.layout() == null) tab.layout(new Pane(tab));
        return createSession(host, tab, s -> tab.layout().split(target, s, sideBySide));
    }

    /**
     * Closes one part of a split tab, the others take its space. If it is the tab itself, the next part
     * takes its place in the row of tabs. With a single part it is the same as {@link #closeSession}.
     */
    public void closePane(Session s) {
        Session tab = s.tab();
        if (tab.panes().size() < 2) {
            closeSession(tab);
            return;
        }
        tab.layout().remove(s);
        Session remaining = tab;
        if (s != tab) {
            if (!tab.layout().divided()) tab.layout(null);
            state.sessions.remove(s);
        } else {
            Session heir = tab.layout().sessions().get(0);
            heir.owner(null);
            heir.synced.set(tab.synced.get());
            remaining = heir;
            heir.layout(tab.layout().divided() ? tab.layout() : null);
            for (Session p : heir.panes()) if (p != heir) p.owner(heir);
            tab.layout(null);
            // The heir may be on another host: the host of the closed tab keeps another tab of its own, if it has one.
            if (state.activeSession.get(tab.host.id) == tab) {
                Session other = heir.host.id.equals(tab.host.id) ? heir : state.sessions.stream()
                        .filter(x -> x.owner() == null && x != tab && x.host.id.equals(tab.host.id)).reduce((a, b) -> b).orElse(null);
                if (other != null) state.activeSession.put(tab.host.id, other);
                else state.activeSession.remove(tab.host.id);
            }
            state.activeSession.putIfAbsent(heir.host.id, heir);
            state.sessions.remove(tab);
        }
        // Back to one part, nothing left to sync: and a later split must not start synced by surprise.
        if (remaining.panes().size() < 2) remaining.synced.set(false);
        // Last: a transport may report the end right away, and by then the session is no longer in the tab.
        s.transport.close();
    }

    /** Brings the session tab to the front: the console shows it, the tree its host. */
    public void showSession(Session part) {
        Session s = part.tab();
        state.activeSession.put(s.host.id, s);
        state.select(new Selection.HostItem(s.host.id), "console");
    }

    /** Closes the tab of {@code s} with all its parts. */
    public void closeSession(Session part) {
        Session s = part.tab();
        List<Session> parts = s.panes();
        // If it is the visible tab, move to the one next to it afterwards (the previous one first), even of another host.
        // A part may be on another host, and the tab may be in view under that host.
        boolean inView = state.node() instanceof Selection.HostItem d && "console".equals(state.position.get().item())
                && parts.contains(state.activeSession.get(d.hostId()));
        List<Session> tabs = state.sessions.stream().filter(x -> x.owner() == null).toList();
        int i = tabs.indexOf(s);
        Session nearby = i > 0 ? tabs.get(i - 1) : i + 1 < tabs.size() ? tabs.get(i + 1) : null;
        s.layout(null);
        for (Session p : parts) {
            if (p != s) p.owner(null);
            p.transport.close();
            state.sessions.remove(p);
        }
        for (Session p : parts) {
            if (state.activeSession.get(p.host.id) != p) continue;
            Session other = state.sessions.stream().filter(x -> x.owner() == null && x.host.id.equals(p.host.id)).reduce((a, b) -> b).orElse(null);
            if (other != null) state.activeSession.put(p.host.id, other);
            else state.activeSession.remove(p.host.id);
        }
        if (inView && nearby != null) showSession(nearby);
    }

    public void disconnectHost(String hostId) {
        state.sessions.stream().filter(s -> s.host.id.equals(hostId)).forEach(s -> s.transport.close());
    }

    /** When the app closes. */
    public void closeAll() {
        state.sessions.forEach(s -> s.transport.close());
    }
}
