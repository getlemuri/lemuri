package lemuri.fe.c;

import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Set;
import java.util.Map;
import java.util.function.Consumer;
import java.util.function.Function;
import javafx.application.Platform;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.be.dto.SessionStatus;
import lemuri.be.service.Transport;
import lemuri.be.terminal.Jumps;
import lemuri.be.terminal.LocalTransport;
import lemuri.be.terminal.SshTransport;
import lemuri.be.util.I18n;
import lemuri.fe.m.AppState;
import lemuri.fe.m.LocalHost;
import lemuri.fe.m.Pane;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Session;
import lemuri.fe.m.Task;

/** Opens and closes the SSH sessions and the local shell tabs. From the JavaFX thread. */
public final class SessionController {
    private final AppState state;
    private final HostController hosts;
    private final Function<Host, Transport> ssh;
    /** Runs on the JavaFX thread: the transports call back from their own threads. */
    private final Consumer<Runnable> fx;
    /** Sessions closed by a command (disconnect, quit): their end is not an "exit" typed in the shell. */
    private final Set<Session> stopped = Collections.newSetFromMap(new IdentityHashMap<>());

    /** {@code requests}: the questions of the SSH connection (fingerprint, password…), asked by the view with its dialogs. */
    public SessionController(AppState state, HostController hosts, Function<Host, SshTransport.UserPrompts> requests) {
        this(state, hosts, h -> LocalHost.is(h) ? new LocalTransport() : new SshTransport(h, requests), Platform::runLater);
    }

    /** For tests: transports made elsewhere (for the local shell too), and callbacks run where the test wants. */
    SessionController(AppState state, HostController hosts, Function<Host, Transport> ssh, Consumer<Runnable> fx) {
        this.state = state;
        this.hosts = hosts;
        this.ssh = ssh;
        this.fx = fx;
    }

    private Session createSession(Host h) {
        return createSession(h, null, null, -1);
    }

    /**
     * {@code owner}: the tab the new session is a part of, null for a new tab; {@code place} puts it in the
     * tab's layout before the views hear of it. {@code at}: where a new tab goes in the row, -1 at the end.
     */
    private Session createSession(Host h, Session owner, Consumer<Session> place, int at) {
        boolean local = LocalHost.is(h);
        long n = state.sessions.stream().filter(s -> s.host.id.equals(h.id)).count() + 1;
        // The local shell starts at once: no line in the log for it.
        Task t = local ? null : state.task(h.name, I18n.t("task.sshSession", h.destination()));
        // The transport gets the host with its jump hosts resolved from the list as it is now.
        Transport tr = ssh.apply(local ? h : Jumps.resolve(h, state.hosts));
        if (tr instanceof SshTransport st) st.onOs(os -> fx.accept(() -> hosts.refreshOs(h.id, os)));
        Session s = new Session(AppState.uid(), h, h.name + " #" + n, tr);
        s.owner(owner);
        tr.onState((state, msg) -> fx.accept(() -> {
            SessionStatus before = s.state.get();
            s.state.set(state);
            if (t != null && state == SessionStatus.CONNECTED) {
                t.finish(Task.Outcome.OK, null);
                hosts.refreshStatuses(Map.of(h.id, HostStatus.ONLINE));
            }
            if (t != null && (state == SessionStatus.ERROR || state == SessionStatus.CLOSED)) t.finish(Task.Outcome.ERROR, msg != null ? msg : I18n.t("task.connectionClosed"));
            // Only the first report of the end: a transport closed again reports it once more.
            if (state != SessionStatus.CLOSED || before == SessionStatus.CLOSED) return;
            // Closed from the app (its tab or part closed, disconnect, quit): nothing else to do.
            boolean byApp = stopped.remove(s);
            if (byApp || !this.state.sessions.contains(s) || !s.tab().panes().contains(s)) return;
            // "exit" in an SSH shell that was running: a local shell takes its place, as when ssh ends in a terminal.
            if (!local && before == SessionStatus.CONNECTED) replaceWithLocal(s);
            // A part of a split tab goes away when its shell ends, as in the split terminals; so does a local tab.
            // An SSH tab that never connected stays, to show why.
            else if (s.tab().panes().size() > 1) closePane(s);
            else if (local) closeSession(s);
        }));
        if (owner == null) {
            state.sessions.add(at < 0 ? state.sessions.size() : at, s);
            state.activeSession.put(h.id, s);
        } else {
            place.accept(s);
            // Right after the other parts of its tab: if the tab closes, one of them is already in its place in the row.
            int after = owner.panes().stream().filter(p -> p != s).mapToInt(state.sessions::indexOf).max().orElse(state.sessions.size() - 1);
            state.sessions.add(after + 1, s);
        }
        return s;
    }

    /** Opens a local shell in a new tab and shows it. */
    public Session openLocal() {
        return openSession(LocalHost.INSTANCE);
    }

    /** Like {@link #split(Session, boolean)}, with a local shell in the new part. */
    public Session splitLocal(Session target, boolean sideBySide) {
        return split(target, sideBySide, LocalHost.INSTANCE);
    }

    /**
     * An SSH shell has ended: a local shell goes in its place, in the same tab or part of a tab, and gets the
     * view if the ended one had it.
     */
    private void replaceWithLocal(Session ended) {
        Session tab = ended.tab();
        boolean inView = state.node() instanceof Selection.HostItem d && "console".equals(state.position.get().item())
                && tab.panes().contains(state.activeSession.get(d.hostId()));
        Session fresh;
        if (tab.panes().size() > 1) {
            fresh = split(ended, true, LocalHost.INSTANCE);
            closePane(ended);
        } else {
            // In the row of tabs, where the ended one was.
            fresh = createSession(LocalHost.INSTANCE, null, null, state.sessions.indexOf(ended) + 1);
            closeSession(ended);
        }
        if (inView) showSession(fresh);
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
        return createSession(host, tab, s -> tab.layout().split(target, s, sideBySide), -1);
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
        stop(s);
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
            stop(p);
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
        state.sessions.stream().filter(s -> s.host.id.equals(hostId)).forEach(this::stop);
    }

    /** When the app closes. */
    public void closeAll() {
        state.sessions.forEach(this::stop);
    }

    /** Closes the transport from the app: its end does not count as an "exit" typed in the shell. */
    private void stop(Session s) {
        // A transport already closed reports nothing more: it is not marked, or it would stay in the set.
        if (s.state.get().alive()) stopped.add(s);
        s.transport.close();
    }
}
