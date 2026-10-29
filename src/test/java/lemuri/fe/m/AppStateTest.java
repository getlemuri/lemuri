package lemuri.fe.m;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;

/** What the views read from the state: folders, hosts and the task log. */
class AppStateTest {
    private static Host host(String id, String group) {
        Host h = new Host();
        h.id = id;
        h.name = id;
        h.group = group;
        return h;
    }

    @Test
    void foldersComeFromTheCreatedOnesAndFromTheHostsWithTheirAncestors() {
        AppState s = new AppState();
        s.createdFolders.add("Empty");
        s.hosts.addAll(List.of(host("h1", "Prod/EU/web"), host("h2", ""), host("h3", "prod/US")));
        // "prod" and "Prod" are the same folder: the first one written wins.
        assertEquals(List.of("Empty", "Prod", "Prod/EU", "Prod/EU/web", "prod/US"), s.folders());
        assertTrue(s.folderExists("PROD/eu"));
        assertFalse(s.folderExists("Staging"));
        assertEquals("h3", s.host("h3").id);
    }

    @Test
    void theTaskLogKeepsTheNewestTwoHundred() {
        AppState s = new AppState();
        for (int i = 0; i < 205; i++) s.task("host", "task " + i);
        assertEquals(200, s.tasks.size());
        assertEquals("task 204", s.tasks.get(0).description);
        assertEquals("task 5", s.tasks.get(199).description);
    }
}
