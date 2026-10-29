package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Field;
import java.util.List;
import javafx.scene.Node;
import javafx.scene.control.ButtonBase;
import javafx.scene.control.TableRow;
import javafx.scene.input.Clipboard;
import javafx.scene.input.ClipboardContent;
import javafx.scene.input.KeyCode;
import javafx.scene.web.WebView;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.SessionStatus;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Session;
import lemuri.fe.m.Task;
import lemuri.fe.v.terminal.Terminal;

/** Sessions, tabs and split terminals, driven from the window (transports are fake). */
class SessionsUiTest extends UiTest {
    private Session openDb() {
        robot.clickOn(text("db-01"));
        settle();
        robot.clickOn(text("Console"));
        until(() -> sessions().size() == 1 && sessions().get(0).state.get() == SessionStatus.CONNECTED, "a connected session");
        return sessions().get(0);
    }

    private Node terminalOf(Session s) {
        return get(() -> SessionViews.terminal(s));
    }

    @Test
    void consoleOpensASessionAndThenGoesBackToIt() {
        Session s = openDb();
        assertEquals("db-01", s.host.name);
        assertTrue(shows("db-01 #1"));
        // From the console back to the summary with the button of its bar, then Console again.
        robot.clickOn(byTooltip("Summary of db-01"));
        until(() -> "summary".equals(app.state.position.get().item()), "summary");
        robot.clickOn(text("Console"));
        settle();
        assertEquals(1, sessions().size(), "the open session is shown, not a new one");
        assertEquals("console", get(() -> app.state.position.get().item()));
    }

    @Test
    void thePlusOpensAnotherSessionOnTheSameHost() {
        openDb();
        robot.clickOn(byTooltip("New session on db-01"));
        until(() -> sessions().size() == 2, "second session");
        assertTrue(shows("db-01 #2"));
    }

    @Test
    void splitRightOnTheSameHostAndCloseThePart() {
        Session s = openDb();
        contextMenu(() -> terminalOf(s));
        choose("Split right", "db-01 (this host)");
        until(() -> sessions().size() == 2, "the second part");
        Session part = sessions().get(1);
        assertSame(s, part.tab());
        assertEquals(2, get(() -> s.panes().size()));
        contextMenu(() -> terminalOf(part));
        choose("Close this pane");
        until(() -> sessions().size() == 1, "back to one part");
        assertTrue(transport(part).closed);
    }

    @Test
    void splitDownWithAnotherHostFromItsFolder() {
        Session s = openDb();
        contextMenu(() -> terminalOf(s));
        choose("Split down", "Production/US", "api-us-1");
        until(() -> sessions().size() == 2, "a part on another host");
        assertEquals("api-us-1", sessions().get(1).host.name);
        assertSame(s, sessions().get(1).tab());
        // Each part has the commands of its own host, on its terminal; none are left in the tab bar.
        until(() -> all(".terminal-actions").size() == 2, "a bar on each terminal");
        assertTrue(get(() -> byTooltip("Summary of db-01") != null && byTooltip("Summary of api-us-1") != null));
        assertTrue(all(".overlay-bar .action").stream().noneMatch(n -> n instanceof ButtonBase b
                && b.getTooltip() != null && b.getTooltip().getText().startsWith("Summary of")), "no host commands in the tab bar");
    }

    @Test
    void syncTypesInEveryPart() {
        Session s = openDb();
        on(() -> app.sessions.split(s, true));
        until(() -> sessions().size() == 2, "split");
        Session part = sessions().get(1);
        until(() -> part.state.get() == SessionStatus.CONNECTED, "part connected");
        typeInto(s, "uptime\r");
        assertTrue(transport(s).sent().contains("uptime"));
        assertFalse(transport(part).sent().contains("uptime"), "without sync only the part in use gets it");
        robot.clickOn(byTooltip("Type in all the parts of this tab at once"));
        until(() -> s.synced.get(), "sync on");
        typeInto(s, "df -h\r");
        assertTrue(transport(part).sent().contains("df -h"), "with sync every part gets it");
        robot.clickOn(byTooltip("Stop typing in all the parts at once"));
        until(() -> !s.synced.get(), "sync off");
    }

    @Test
    void whenTheShellOfAPartEndsThePartGoes() {
        Session s = openDb();
        on(() -> app.sessions.split(s, false));
        until(() -> sessions().size() == 2, "split");
        transport(sessions().get(1)).end();
        until(() -> sessions().size() == 1, "the ended part closed");
        // A tab alone stays, to show why it ended.
        transport(s).end();
        until(() -> s.state.get() == SessionStatus.CLOSED, "closed");
        assertEquals(1, sessions().size());
    }

    @Test
    void closingTheTabAndDisconnectingTheHost() {
        Session s = openDb();
        robot.clickOn(byTooltip("New session on db-01"));
        until(() -> sessions().size() == 2, "two sessions");
        List<Session> two = sessions();
        Node close = robot.lookup(n -> n instanceof ButtonBase b && b.getTooltip() != null
                && "Close session".equals(b.getTooltip().getText()) && b.isVisible()).query();
        robot.clickOn(close);
        until(() -> sessions().size() == 1, "one closed");
        // Disconnect closes the connections; the tab stays, to show why it ended.
        menu("db-01", "Disconnect");
        until(() -> two.stream().allMatch(x -> transport(x).closed), "all connections closed");
        until(() -> sessions().stream().allMatch(x -> x.state.get() == SessionStatus.CLOSED), "all shown as closed");
    }

    @Test
    void theBottomPanelListsTasksAndSessions() {
        openDb();
        robot.clickOn(text("Sessions (1)"));
        until(() -> shows("db-01 #1") && all(".table-row-cell").stream().anyMatch(n -> n instanceof TableRow<?> r && r.getItem() != null), "the session in the table");
        robot.clickOn(text("Task"));
        settle();
        assertTrue(get(() -> app.state.tasks.stream().anyMatch(t -> t.destination.equals("db-01") && t.outcome.get() == Task.Outcome.OK)));
        assertTrue(shows("SSH session deploy@203.0.113.20"));
    }

    @Test
    void anOpenSessionShowsInTheTreeAndTheSummary() {
        openDb();
        on(() -> app.state.select(new Selection.HostItem("db-01"), "summary"));
        settle();
        assertEquals(1L, get(() -> app.state.connected("db-01")));
    }

    /** The double click on the empty space at the prompt, as the terminal page reports it. */
    private void doubleClickAtThePrompt(Session s) {
        on(() -> {
            try {
                Field f = Terminal.class.getDeclaredField("bridge");
                f.setAccessible(true);
                ((Terminal.Bridge) f.get(SessionViews.terminal(s))).listHere();
            } catch (ReflectiveOperationException e) {
                throw new AssertionError(e);
            }
        });
        settle();
    }

    /** Interactive mode: ls at the prompt, typed in plain sight; nothing once it is turned off in Settings. */
    @Test
    void doubleClickAtThePromptListsTheFolderOnlyIfInteractive() {
        Session s = openDb();
        // The fake server printed its prompt: the line under the cursor looks like an idle shell.
        until(() -> SessionViews.terminal(s).shellReady(), "the prompt on screen");
        doubleClickAtThePrompt(s);
        until(() -> transport(s).sent().contains(" ls\r"), "ls typed, in plain sight");
        int before = transport(s).sent().length();
        try {
            Terminal.typesCommands(false);
            doubleClickAtThePrompt(s);
            assertEquals(before, transport(s).sent().length(), "nothing typed");
        } finally {
            Terminal.typesCommands(true);
        }
    }

    /** Asks the terminal page something, as JavaScript. */
    private Object script(Session s, String js) {
        return get(() -> SessionViews.terminal(s).getChildrenUnmodifiable().stream().filter(n -> n instanceof WebView)
                .map(n -> (WebView) n).findFirst().orElseThrow().getEngine().executeScript(js));
    }

    /**
     * The double click on a folder types "cd app && ls": its names are listed under the old folder's prompt and
     * the new prompt is another, yet they can be clicked. After a cd elsewhere they cannot.
     */
    @Test
    void theNamesOfAFolderEnteredByDoubleClickCanBeClicked() {
        Session s = openDb();
        until(() -> SessionViews.terminal(s).shellReady(), "the prompt");
        transport(s).print(" cd app && ls\r\nnotes.txt  sub\r\ndeploy@db-01:~/app$ ");
        until(() -> String.valueOf(script(s, "lineText(2)")).startsWith("deploy@db-01:~/app$"), "the listing on screen");
        assertEquals(true, script(s, "inLsOutput(1) && shellReady(1)"));
        transport(s).print("cd /tmp\r\ndeploy@db-01:/tmp$ ");
        until(() -> String.valueOf(script(s, "lineText(3)")).startsWith("deploy@db-01:/tmp$"), "the new prompt");
        assertEquals(false, script(s, "shellReady(1)"), "another folder now");
    }

    @Test
    void uploadIsOfferedOnlyOnALiveSession() {
        Session s = openDb();
        contextMenu(() -> terminalOf(s));
        assertFalse(menuItemDisabled("Upload files to the current folder…"));
        transport(s).end();
        until(() -> s.state.get() == SessionStatus.CLOSED, "closed");
        contextMenu(() -> terminalOf(s));
        assertTrue(menuItemDisabled("Upload files to the current folder…"));
    }

    private static void clipboard(String text) {
        on(() -> {
            ClipboardContent c = new ClipboardContent();
            c.putString(text);
            Clipboard.getSystemClipboard().setContent(c);
        });
    }

    @Test
    void rightClickPastesAndCopies() {
        Session s = openDb();
        until(() -> SessionViews.terminal(s).shellReady(), "the prompt");
        clipboard("echo pasted");
        contextMenu(() -> terminalOf(s));
        assertTrue(menuItemDisabled("Copy"), "nothing selected yet");
        contextMenu(() -> terminalOf(s));
        choose("Paste");
        until(() -> transport(s).sent().contains("echo pasted"), "the clipboard went to the shell");
        // Select what is on screen, then Copy: it lands in the clipboard.
        on(() -> SessionViews.terminal(s).getChildrenUnmodifiable().stream().filter(n -> n instanceof WebView)
                .map(n -> (WebView) n).findFirst().orElseThrow().getEngine().executeScript("term.selectAll()"));
        contextMenu(() -> terminalOf(s));
        choose("Copy");
        until(() -> String.valueOf(Clipboard.getSystemClipboard().getString()).contains("deploy@db-01"), "the selection copied");
    }

    @Test
    void ctrlShiftVPastesInEveryPartOfASyncedTab() {
        Session s = openDb();
        on(() -> app.sessions.split(s, true));
        until(() -> sessions().size() == 2 && sessions().get(1).state.get() == SessionStatus.CONNECTED, "split");
        Session part = sessions().get(1);
        on(() -> s.synced.set(true));
        clipboard("uname -a");
        // Sync on redraws the tab: the terminal is clicked once it is back on screen.
        until(() -> SessionViews.terminal(s).getScene() != null, "the terminal back in view");
        robot.clickOn(terminalOf(s));
        robot.press(KeyCode.CONTROL, KeyCode.SHIFT, KeyCode.V)
                .release(KeyCode.V, KeyCode.SHIFT, KeyCode.CONTROL);
        until(() -> transport(s).sent().contains("uname -a") && transport(part).sent().contains("uname -a"), "pasted in both parts");
    }
}
