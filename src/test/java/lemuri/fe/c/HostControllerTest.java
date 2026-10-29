package lemuri.fe.c;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static lemuri.fe.c.Fakes.host;

import java.util.List;
import java.util.Map;
import java.util.prefs.Preferences;
import javafx.collections.ListChangeListener;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.fe.m.AppState;
import lemuri.fe.m.Selection;

/** Hosts and folders: what changes in the state and what gets saved. Preferences are the fake ones of the pom. */
class HostControllerTest {
    private final AppState state = new AppState();
    private final Fakes.MemoryVault vault = new Fakes.MemoryVault();

    @AfterEach
    void clear() {
        Preferences.userRoot().node("lemuri/host").remove("passwordsToForget");
    }

    private Fakes.Repository repository(List<String> folders, Host... hosts) {
        return new Fakes.Repository(folders, List.of(hosts));
    }

    private static String group(AppState s, String id) {
        return s.host(id).group;
    }

    @Test
    void loadingNormalizesTheFolders() {
        new HostController(state, repository(List.of(" /Prod//EU/ ", ""), host("h1", "Staging/")), vault);
        assertEquals(List.of("Prod/EU"), state.createdFolders);
        assertEquals("Staging", group(state, "h1"));
    }

    @Test
    void movingAFolderTakesItsSubfoldersAndHostsAlong() {
        Fakes.Repository r = repository(List.of("Prod/EU/empty"), host("h1", "Prod/EU"), host("h2", "Prod/EU/web"), host("h3", "Other"));
        HostController c = new HostController(state, r, vault);
        state.select(new Selection.Folder("Prod/EU/web"), null);

        c.moveFolder("Prod/EU", "Archive/EU");

        assertEquals("Archive/EU", group(state, "h1"));
        assertEquals("Archive/EU/web", group(state, "h2"));
        assertEquals("Other", group(state, "h3"));
        assertTrue(state.createdFolders.contains("Archive/EU/empty"));
        assertFalse(state.folderExists("Prod/EU"));
        // The selected folder follows the move, and everything is saved.
        assertEquals(new Selection.Folder("Archive/EU/web"), state.node());
        assertEquals("Archive/EU/web", r.last().hosts().stream().filter(h -> h.id.equals("h2")).findFirst().orElseThrow().group);
    }

    @Test
    void aFolderIsNotMovedInsideItself() {
        Fakes.Repository r = repository(List.of(), host("h1", "Prod"));
        HostController c = new HostController(state, r, vault);
        c.moveFolder("Prod", "Prod/inner");
        assertEquals("Prod", group(state, "h1"));
        assertTrue(r.saved.isEmpty());
    }

    @Test
    void deletingAFolderMovesItsHostsUp() {
        HostController c = new HostController(state, repository(List.of("Prod/EU/old"), host("h1", "Prod/EU"), host("h2", "Prod/EU/web")), vault);
        state.select(new Selection.Folder("Prod/EU/web"), null);
        c.deleteFolder("Prod/EU");
        assertEquals("Prod", group(state, "h1"));
        assertEquals("Prod", group(state, "h2"));
        assertFalse(state.folderExists("Prod/EU"));
        assertEquals(new Selection.Folder("Prod"), state.node());
    }

    @Test
    void aRemovedHostLosesItsPasswordEvenWithTheVaultLocked() {
        HostController c = new HostController(state, repository(List.of(), host("h1", ""), host("h2", ""), host("h3", "")), vault);
        vault.secrets.putAll(Map.of("h1", "pw1", "h2", "pw2", "h3", "pw3"));

        c.removeHost(state.host("h1"));
        assertNull(vault.secrets.get("h1"));

        // Locked: the password stays until the next unlock, then goes, unless the host came back meanwhile.
        vault.open = false;
        Host h2 = state.host("h2"), h3 = state.host("h3");
        c.removeHost(h2);
        c.removeHost(h3);
        c.saveHost(h3);
        assertEquals("pw2", vault.secrets.get("h2"));
        vault.unlock();
        assertNull(vault.secrets.get("h2"));
        assertEquals("pw3", vault.secrets.get("h3"));
        assertEquals("", Preferences.userRoot().node("lemuri/host").get("passwordsToForget", ""));
    }

    @Test
    void onlyStatusesThatChangeAreTouched() {
        Host h1 = host("h1", ""), h2 = host("h2", "");
        h1.state = HostStatus.ONLINE;
        HostController c = new HostController(state, repository(List.of(), h1, h2), vault);
        int[] changes = {0};
        state.hosts.addListener((ListChangeListener<Host>) e -> changes[0]++);
        c.refreshStatuses(Map.of("h1", HostStatus.ONLINE));
        assertEquals(0, changes[0]);
        c.refreshStatuses(Map.of("h1", HostStatus.OFFLINE, "h2", HostStatus.ONLINE));
        assertEquals(1, changes[0]);
        assertEquals(HostStatus.OFFLINE, state.host("h1").state);
        assertEquals(HostStatus.ONLINE, state.host("h2").state);
    }

    @Test
    void aReceivedHostMovedToAFolderBecomesOneOfOurOwn() {
        Host h = host("h1", "");
        h.received = true;
        HostController c = new HostController(state, repository(List.of(), h), vault);
        c.moveHost(state.host("h1"), "");
        assertFalse(state.host("h1").received);
        assertEquals("", group(state, "h1"));
    }

    @Test
    void importedHostsAreAddedWithOneSave() {
        Fakes.Repository r = repository(List.of(), host("h1", ""));
        HostController c = new HostController(state, r, vault);
        c.importHosts(List.of(host("a", " Imported/ "), host("b", "Imported")), "~/.ssh/config");
        assertEquals(1, r.saved.size());
        assertEquals(3, r.last().hosts().size());
        assertEquals("Imported", group(state, "a"));
        assertTrue(state.folderExists("Imported"));
        assertEquals("~/.ssh/config", state.tasks.get(0).destination);
        c.importHosts(List.of(), "~/.ssh/config");
        assertEquals(1, r.saved.size());
    }

    @Test
    void renamingAJumpHostUpdatesTheHostsBehindIt() {
        Host bastion = host("b1", ""), db = host("db", "");
        bastion.name = "bastion";
        db.jump = "Bastion, admin@10.0.0.5";
        HostController c = new HostController(state, repository(List.of(), bastion, db), vault);
        Host renamed = state.host("b1").copy();
        renamed.name = "gateway";
        c.saveHost(renamed);
        assertEquals("gateway,admin@10.0.0.5", state.host("db").jump);
    }
}
