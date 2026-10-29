package lemuri.fe.c;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static lemuri.fe.c.Fakes.host;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.HostStatus;
import lemuri.be.dto.SessionStatus;
import lemuri.fe.m.AppState;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Session;
import lemuri.fe.m.Task;

/** Sessions, with fake transports and callbacks run right away instead of on the JavaFX thread. */
class SessionControllerTest {
    private final AppState state = new AppState();
    private final List<Fakes.FakeTransport> transports = new ArrayList<>();
    private final SessionController c;

    SessionControllerTest() {
        HostController hosts = new HostController(state, new Fakes.Repository(List.of(), List.of(host("web", ""), host("db", ""))), new Fakes.MemoryVault());
        c = new SessionController(state, hosts, h -> fake(), Runnable::run);
    }

    private Fakes.FakeTransport fake() {
        Fakes.FakeTransport t = new Fakes.FakeTransport();
        transports.add(t);
        return t;
    }

    private static Fakes.FakeTransport transport(Session s) {
        return (Fakes.FakeTransport) s.transport;
    }

    @Test
    void connectingMarksTheHostOnlineAndFinishesTheTask() {
        Session s = c.openSession(state.host("web"));
        assertEquals(new Selection.HostItem("web"), state.node());
        Task t = state.tasks.get(0);
        assertEquals(Task.Outcome.RUNNING, t.outcome.get());
        transport(s).report(SessionStatus.CONNECTED);
        assertEquals(SessionStatus.CONNECTED, s.state.get());
        assertEquals(Task.Outcome.OK, t.outcome.get());
        assertEquals(HostStatus.ONLINE, state.host("web").state);
    }

    @Test
    void closingTheVisibleTabMovesToTheOneBeforeIt() {
        Session first = c.openSession(state.host("web"));
        Session second = c.openSession(state.host("db"));
        Session third = c.openSession(state.host("db"));
        c.showSession(third);

        c.closeSession(third);

        assertTrue(transport(third).closed);
        assertFalse(state.sessions.contains(third));
        assertSame(second, state.activeSession.get("db"));
        assertEquals(new Selection.HostItem("db"), state.node());
        c.closeSession(second);
        assertNull(state.activeSession.get("db"));
        assertSame(first, state.activeSession.get("web"));
    }

    @Test
    void consoleGoesToTheOpenSessionInsteadOfOpeningAnother() {
        Session web = c.openSession(state.host("web"));
        transport(web).report(SessionStatus.CONNECTED);
        c.openSession(state.host("db"));

        assertSame(web, c.showOrOpen(state.host("web")));
        assertEquals(2, state.sessions.size());
        assertEquals(new Selection.HostItem("web"), state.node());
        assertEquals("console", state.position.get().item());

        // Its shell ended: the console opens a new one.
        transport(web).report(SessionStatus.CLOSED);
        Session again = c.showOrOpen(state.host("web"));
        assertFalse(again == web);
        assertEquals(3, state.sessions.size());
    }

    @Test
    void disconnectingAHostClosesOnlyItsSessions() {
        Session web = c.openSession(state.host("web"));
        Session db1 = c.openSession(state.host("db")), db2 = c.openSession(state.host("db"));
        c.disconnectHost("db");
        assertTrue(transport(db1).closed);
        assertTrue(transport(db2).closed);
        assertFalse(transport(web).closed);
    }

    @Test
    void aSplitCanConnectToAnotherHost() {
        Session tab = c.openSession(state.host("web"));
        Session db = c.split(tab, true, state.host("db"));
        assertSame(tab, db.owner());
        assertEquals("db", db.host.id);
        assertEquals(List.of(tab, db), tab.panes());
        // A part is not a tab: the other host has no tab of its own yet.
        assertNull(state.activeSession.get("db"));

        // Closing the first part: the part on the other host becomes the tab, and the closed host is not left pointing at it.
        c.closePane(tab);
        assertNull(db.owner());
        assertEquals(List.of(db), state.sessions);
        assertNull(state.activeSession.get("web"));
        assertSame(db, state.activeSession.get("db"));
    }

    @Test
    void closingATabWithAPartOnAnotherHostClosesThemAll() {
        Session dbTab = c.openSession(state.host("db"));
        Session tab = c.openSession(state.host("web"));
        Session db = c.split(tab, true, state.host("db"));
        c.closeSession(tab);
        assertTrue(transport(tab).closed);
        assertTrue(transport(db).closed);
        assertFalse(transport(dbTab).closed);
        assertEquals(List.of(dbTab), state.sessions);
        assertNull(state.activeSession.get("web"));
        assertSame(dbTab, state.activeSession.get("db"));
    }

    @Test
    void aSplitIsANewSessionInsideTheSameTab() {
        Session tab = c.openSession(state.host("web"));
        Session other = c.openSession(state.host("db"));
        Session right = c.split(tab, true);
        Session below = c.split(right, false);

        assertSame(tab, right.owner());
        assertSame(tab, below.owner());
        assertEquals(List.of(tab, right, below), tab.panes());
        // Right after their tab, and the tab stays the active one.
        assertEquals(List.of(tab, right, below, other), state.sessions);
        assertSame(tab, state.activeSession.get("web"));
        c.showSession(below);
        assertSame(tab, state.activeSession.get("web"));
    }

    @Test
    void closingAPartLeavesTheOthers() {
        Session tab = c.openSession(state.host("web"));
        Session right = c.split(tab, true);
        c.closePane(right);
        assertTrue(transport(right).closed);
        assertFalse(transport(tab).closed);
        assertEquals(List.of(tab), state.sessions);
        assertNull(tab.layout());
    }

    @Test
    void closingTheTabPartHandsTheTabToTheNextPart() {
        Session first = c.openSession(state.host("web"));
        Session tab = c.openSession(state.host("web"));
        Session right = c.split(tab, true), below = c.split(right, false);
        c.closePane(tab);
        assertTrue(transport(tab).closed);
        assertEquals(List.of(first, right, below), state.sessions);
        assertNull(right.owner());
        assertSame(right, below.owner());
        assertEquals(List.of(right, below), right.panes());
        assertSame(right, state.activeSession.get("web"));
    }

    /** Synced typing belongs to the tab: it moves with the tab, and ends when only one part is left. */
    @Test
    void syncFollowsTheTabAndEndsWithOnePart() {
        Session tab = c.openSession(state.host("web"));
        Session right = c.split(tab, true), below = c.split(right, false);
        tab.synced.set(true);
        c.closePane(tab);
        assertTrue(right.synced.get(), "the next part becomes the tab, still synced");
        c.closePane(below);
        assertFalse(right.synced.get(), "one part left: nothing to sync");
        Session again = c.split(right, true);
        assertFalse(again.tab().synced.get(), "a new split does not start synced by surprise");
    }

    @Test
    void closingTheTabClosesEveryPart() {
        Session tab = c.openSession(state.host("web"));
        Session right = c.split(tab, true);
        c.closeSession(right);
        assertTrue(transport(tab).closed);
        assertTrue(transport(right).closed);
        assertTrue(state.sessions.isEmpty());
    }

    @Test
    void aPartWhoseShellEndsGoesAway() {
        Session tab = c.openSession(state.host("web"));
        Session right = c.split(tab, true);
        transport(right).report(SessionStatus.CLOSED);
        assertEquals(List.of(tab), state.sessions);
        // The tab itself stays, to show why.
        transport(tab).report(SessionStatus.CLOSED);
        assertEquals(List.of(tab), state.sessions);
    }
}
