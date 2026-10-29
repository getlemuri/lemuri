package lemuri.fe.m;

import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;
import java.util.UUID;
import javafx.beans.property.ObjectProperty;
import javafx.beans.property.SimpleObjectProperty;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.collections.ObservableMap;
import lemuri.be.dto.MetricsResult;
import lemuri.be.dto.Host;
import lemuri.be.dto.SessionStatus;
import lemuri.be.util.Folders;

/**
 * Observable state of the interface: hosts, sessions, tasks and selected node.
 * Views read and observe it, controllers change it. Everything is touched from
 * the JavaFX thread.
 */
public final class AppState {
    /** Tree node and tab of the central menu (null = the first one of the node). */
    public record Position(Selection node, String item) {}

    public final ObservableList<Host> hosts = FXCollections.observableArrayList();
    /** Folders created by hand, even empty ones. Those containing hosts exist anyway: see {@link #folders()}. */
    public final ObservableList<String> createdFolders = FXCollections.observableArrayList();
    public final ObservableList<Session> sessions = FXCollections.observableArrayList();
    public final ObservableList<Task> tasks = FXCollections.observableArrayList();
    /** hostId -> session shown in the host console. */
    public final ObservableMap<String, Session> activeSession = FXCollections.observableHashMap();
    public final ObjectProperty<Position> position = new SimpleObjectProperty<>(new Position(new Selection.Datacenter(), null));
    /** hostId -> last reading of CPU, RAM and disks. Not in the hosts: frequent changes must not redraw everything. */
    public final ObservableMap<String, MetricsResult> resources = FXCollections.observableHashMap();

    public static String uid() {
        return UUID.randomUUID().toString().substring(0, 8);
    }

    // ── Navigation ───────────────────────────────────────────────────────────

    public void select(Selection s, String item) {
        position.set(new Position(s, item));
    }

    public Selection node() {
        return position.get().node();
    }

    // ── Readings ─────────────────────────────────────────────────────────────

    public Host host(String id) {
        return hosts.stream().filter(h -> h.id.equals(id)).findFirst().orElse(null);
    }

    public Session session(String id) {
        return id == null ? null : sessions.stream().filter(s -> s.id.equals(id)).findFirst().orElse(null);
    }

    public long connected(String hostId) {
        return sessions.stream().filter(s -> (hostId == null || s.host.id.equals(hostId)) && s.state.get() == SessionStatus.CONNECTED).count();
    }

    /** All folders, sorted: the created ones, those of the hosts and their ancestors. */
    public List<String> folders() {
        TreeSet<String> all = new TreeSet<>(String.CASE_INSENSITIVE_ORDER);
        List<String> base = new ArrayList<>(createdFolders);
        hosts.forEach(h -> base.add(h.group));
        for (String c : base) {
            for (String p = c; !p.isEmpty(); p = Folders.parent(p)) all.add(p);
        }
        return new ArrayList<>(all);
    }

    public boolean folderExists(String path) {
        return folders().stream().anyMatch(c -> c.equalsIgnoreCase(path));
    }

    // ── Tasks ────────────────────────────────────────────────────────────────

    /** A new line at the top of the log; 200 are kept. */
    public Task task(String destination, String description) {
        Task t = new Task(destination, description);
        tasks.add(0, t);
        if (tasks.size() > 200) tasks.remove(200, tasks.size());
        return t;
    }
}
