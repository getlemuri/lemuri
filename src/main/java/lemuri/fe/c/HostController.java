package lemuri.fe.c;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.prefs.Preferences;
import java.util.stream.Collectors;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.be.service.HostRepository;
import lemuri.be.service.Vault;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.fe.m.AppState;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Task;

/** Hosts and folders: changes them in the state and saves them at once. From the JavaFX thread. */
public final class HostController {
    private final AppState state;
    private final HostRepository repository;
    private final Vault vault;
    /** Problem reading the saved hosts, to show at startup; null if none. */
    public final String loadingNotice;

    public HostController(AppState state, HostRepository repository, Vault vault) {
        this.state = state;
        this.repository = repository;
        this.vault = vault;
        // The passwords of hosts deleted while the vault was locked are erased as soon as it is unlocked.
        vault.onUnlock(this::forgetLeftovers);
        HostRepository.Data d = repository.load();
        loadingNotice = d.warning();
        for (Host h : d.hosts()) h.group = Folders.normalize(h.group);
        state.hosts.setAll(d.hosts());
        state.createdFolders.setAll(d.folders().stream().map(Folders::normalize).filter(c -> !c.isEmpty()).distinct().toList());
    }

    // ── Hosts ────────────────────────────────────────────────────────────────

    public void saveHost(Host h) {
        for (int i = 0; i < state.hosts.size(); i++) {
            if (state.hosts.get(i).id.equals(h.id)) {
                String old = state.hosts.get(i).name;
                if (old != null && !old.equals(h.name)) renameJumps(old, h.name);
                state.hosts.set(i, h);
                write();
                return;
            }
        }
        state.hosts.add(h);
        write();
    }

    /** A host used as a jump host by name was renamed: the hosts going through it follow the new name. */
    private void renameJumps(String old, String created) {
        for (int i = 0; i < state.hosts.size(); i++) {
            Host x = state.hosts.get(i);
            if (x.jump == null) continue;
            String j = Arrays.stream(x.jump.split(",")).map(String::strip)
                    .map(t -> t.equalsIgnoreCase(old) ? created : t).collect(Collectors.joining(","));
            if (!j.equals(x.jump.replace(" ", ""))) {
                Host c = x.copy();
                c.jump = j;
                state.hosts.set(i, c);
            }
        }
    }

    /** Adds several hosts at once (an import): one save and one line in the log. */
    public void importHosts(List<Host> l, String source) {
        if (l.isEmpty()) return;
        for (Host h : l) h.group = Folders.normalize(h.group);
        state.hosts.addAll(l);
        write();
        state.task(source, I18n.t("task.hostsImported", String.valueOf(l.size()))).finish(Task.Outcome.OK, null);
    }

    /** Hosts and folders replaced all at once (a backup imported), with one save. */
    public void replaceAll(List<String> folders, List<Host> hosts) {
        for (Host h : hosts) h.group = Folders.normalize(h.group);
        state.hosts.setAll(hosts);
        state.createdFolders.setAll(folders.stream().map(Folders::normalize).filter(c -> !c.isEmpty()).distinct().toList());
        write();
    }

    public void removeHost(Host h) {
        state.hosts.removeIf(x -> x.id.equals(h.id));
        write();
        // With the vault locked the password cannot be removed now: the id is noted (it is not a secret) and the
        // password is removed at the next unlock. A deleted host's password must not stay in the file forever.
        try {
            if (vault.isOpen()) vault.forget(h.id);
            else toForget(h.id, true);
        } catch (IOException e) {
            toForget(h.id, true);
        }
        if (state.node() instanceof Selection.HostItem d && d.hostId().equals(h.id)) state.select(new Selection.Datacenter(), null);
        state.task(h.name, I18n.t("task.hostRemoved")).finish(Task.Outcome.OK, null);
    }

    private static final Preferences PREF = Preferences.userRoot().node("lemuri/host");

    /** Adds or removes an id from the list of passwords to erase at the next vault unlock. */
    private static synchronized void toForget(String id, boolean add) {
        Set<String> l = new LinkedHashSet<>(Arrays.asList(PREF.get("passwordsToForget", "").split(",")));
        l.remove("");
        if (add) l.add(id);
        else l.remove(id);
        PREF.put("passwordsToForget", String.join(",", l));
    }

    private void forgetLeftovers() {
        for (String id : PREF.get("passwordsToForget", "").split(",")) {
            if (id.isBlank()) continue;
            // If in the meantime the host came back (received again with the same id), its password stays.
            if (state.hosts.stream().noneMatch(x -> x.id.equals(id))) {
                try {
                    vault.forget(id);
                } catch (IOException e) {
                    continue; // retry at the next unlock
                }
            }
            toForget(id, false);
        }
    }

    public void moveHost(Host h, String folder) {
        Host c = h.copy();
        c.group = Folders.normalize(folder);
        // Put in a folder (or in the datacenter), a received host becomes one of your own.
        if (c.group.equals(h.group) && !h.received) return;
        c.received = false;
        saveHost(c);
        state.task(h.name, I18n.t("task.movedTo", Folders.label(c.group))).finish(Task.Outcome.OK, null);
    }

    /**
     * Outcome of the reachability probe. Only hosts whose status changes are
     * touched, with a single notification: the views are rebuilt only once.
     */
    public void refreshStatuses(Map<String, HostStatus> statuses) {
        boolean modified = false;
        for (Host h : state.hosts) {
            HostStatus s = statuses.get(h.id);
            if (s != null && s != h.state) {
                h.state = s;
                modified = true;
            }
        }
        if (modified) state.hosts.setAll(new ArrayList<>(state.hosts));
    }

    /** The operating system read from the VPS (PRETTY_NAME of /etc/os-release) replaces the one typed by hand. */
    public void refreshOs(String hostId, String os) {
        Host h = state.host(hostId);
        if (h == null || os == null || os.equals(h.os)) return;
        Host c = h.copy();
        c.os = os;
        saveHost(c);
    }

    // ── Folders ──────────────────────────────────────────────────────────────

    public void createFolder(String path) {
        String p = Folders.normalize(path);
        if (p.isEmpty() || state.folderExists(p)) return;
        state.createdFolders.add(p);
        write();
        state.task(p, I18n.t("task.folderCreated")).finish(Task.Outcome.OK, null);
    }

    /**
     * Renames or moves a folder: it, its subfolders and the hosts inside change
     * path together. A folder is not moved inside itself.
     */
    public void moveFolder(String old, String created) {
        String n = Folders.normalize(created);
        if (n.isEmpty() || n.equals(old) || Folders.inside(n, old)) return;
        List<String> create = new ArrayList<>();
        for (String c : state.createdFolders) create.add(Folders.rename(c, old, n));
        // A folder that existed only thanks to its hosts stays too, if it is emptied later.
        create.add(n);
        state.createdFolders.setAll(create.stream().distinct().toList());
        List<Host> fresh = new ArrayList<>();
        for (Host h : state.hosts) {
            String g = Folders.rename(h.group, old, n);
            if (g.equals(h.group)) fresh.add(h);
            else {
                Host c = h.copy();
                c.group = g;
                fresh.add(c);
            }
        }
        state.hosts.setAll(fresh);
        write();
        boolean rename = Folders.parent(old).equals(Folders.parent(n));
        state.task(old, rename ? I18n.t("task.renamedTo", Folders.name(n)) : I18n.t("task.movedTo", Folders.label(Folders.parent(n)))).finish(Task.Outcome.OK, null);
        if (state.node() instanceof Selection.Folder c && Folders.inside(c.path(), old))
            state.select(new Selection.Folder(Folders.rename(c.path(), old, n)), state.position.get().item());
    }

    /** Deletes the folder and its subfolders; the hosts they contained move up to the parent folder. */
    public void deleteFolder(String path) {
        String on = Folders.parent(path);
        state.createdFolders.removeIf(c -> Folders.inside(c, path));
        if (!on.isEmpty() && !state.createdFolders.contains(on)) state.createdFolders.add(on);
        List<Host> fresh = new ArrayList<>();
        for (Host h : state.hosts) {
            if (!Folders.inside(h.group, path)) fresh.add(h);
            else {
                Host c = h.copy();
                c.group = on;
                fresh.add(c);
            }
        }
        state.hosts.setAll(fresh);
        write();
        state.task(path, I18n.t("task.folderDeleted")).finish(Task.Outcome.OK, null);
        if (state.node() instanceof Selection.Folder c && Folders.inside(c.path(), path))
            state.select(on.isEmpty() ? new Selection.Datacenter() : new Selection.Folder(on), null);
    }

    private void write() {
        repository.save(new HostRepository.Data(new ArrayList<>(state.createdFolders), new ArrayList<>(state.hosts)));
    }
}
