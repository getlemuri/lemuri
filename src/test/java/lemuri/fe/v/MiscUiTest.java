package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import java.util.prefs.Preferences;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.MenuItem;
import javafx.scene.control.ScrollPane;
import javafx.scene.control.TextArea;
import javafx.scene.control.TextField;
import javafx.scene.control.ToggleButton;
import javafx.scene.input.KeyCode;
import javafx.stage.Window;
import org.junit.jupiter.api.Test;
import org.testfx.util.WaitForAsyncUtils;
import lemuri.be.dto.SessionStatus;
import lemuri.fe.m.Session;
import lemuri.fe.v.terminal.Terminal;

/** The rest of the window: local shell, notes, font size, disks check, the bottom panel. */
class MiscUiTest extends UiTest {
    private void typeIntoTerminal(Terminal t, String data) {
        on(() -> {
            try {
                Field f = Terminal.class.getDeclaredField("bridge");
                f.setAccessible(true);
                ((Terminal.Bridge) f.get(t)).input(data);
            } catch (ReflectiveOperationException e) {
                throw new AssertionError(e);
            }
        });
    }

    @Test
    void theTerminalTabRunsALocalShellUntilExit() {
        Node tab = robot.lookup(n -> n instanceof ToggleButton b && "Terminal".equals(b.getText())).query();
        robot.clickOn(tab);
        until(() -> console.log().shell.get() != null, "the local shell");
        Terminal shell = get(() -> console.log().shell.get());
        // Its right click offers copy and paste, nothing else.
        contextMenu(() -> shell);
        assertTrue(get(() -> new ArrayList<>(Window.getWindows()).stream().filter(w -> w instanceof ContextMenu && w.isShowing())
                .map(w -> ((ContextMenu) w).getItems().stream().map(MenuItem::getText).toList())
                .anyMatch(List.of("Copy", "Paste")::equals)), "copy and paste only");
        on(() -> new ArrayList<>(Window.getWindows()).stream().filter(w -> w instanceof ContextMenu).forEach(Window::hide));
        // The shell is real (bash on this computer): it ends with exit, and the panel goes back to the tasks.
        WaitForShell.prompt();
        typeIntoTerminal(shell, "exit\r");
        until(() -> console.log().shell.get() == null, "the shell ended");
        until(() -> robot.lookup(n -> n instanceof ToggleButton b && "Task".equals(b.getText())).queryAs(ToggleButton.class).isSelected(), "back to the tasks");
    }

    /** bash takes a moment to start: what is typed before is kept by the terminal anyway, but exit is sent after it. */
    private static final class WaitForShell {
        static void prompt() {
            WaitForAsyncUtils.sleep(1500, TimeUnit.MILLISECONDS);
        }
    }

    @Test
    void notesAreSavedWithTheHost() {
        robot.clickOn(text("db-01"));
        settle();
        TextArea notes = (TextArea) robot.lookup(n -> n instanceof TextArea t && "Notes about this host…".equals(t.getPromptText())).query();
        // At the bottom of the summary, below the edge of the window: scrolled down as the user would.
        on(() -> ((ScrollPane) console.lookup(".contents-scroll")).setVvalue(1));
        settle();
        robot.clickOn(notes).write("backup at 3am");
        until(() -> shows("Unsaved changes"), "unsaved marker");
        robot.clickOn(robot.lookup(n -> n instanceof Button b && "Save".equals(b.getText()) && b.getScene() == stage.getScene()).queryButton());
        until(() -> "backup at 3am".equals(host("db-01").note), "saved");
    }

    @Test
    void ctrlPlusMakesTheTerminalFontBigger() {
        robot.clickOn(text("db-01"));
        settle();
        robot.clickOn(text("Console"));
        until(() -> sessions().size() == 1 && sessions().get(0).state.get() == SessionStatus.CONNECTED, "session");
        Session s = sessions().get(0);
        int before = Preferences.userRoot().node("lemuri/ui").getInt("terminalFontSize", 13);
        robot.clickOn(get(() -> SessionViews.terminal(s)));
        robot.press(KeyCode.CONTROL, KeyCode.PLUS).release(KeyCode.PLUS, KeyCode.CONTROL);
        until(() -> Preferences.userRoot().node("lemuri/ui").getInt("terminalFontSize", 13) == before + 1, "font one step bigger");
        robot.press(KeyCode.CONTROL, KeyCode.DIGIT0).release(KeyCode.DIGIT0, KeyCode.CONTROL);
        until(() -> Preferences.userRoot().node("lemuri/ui").getInt("terminalFontSize", 13) == 13, "back to the default");
    }

    @Test
    void checkDisksReadsEveryHost() {
        robot.clickOn(text("Hosts"));
        settle();
        synchronized (metrics.refreshed) { metrics.refreshed.clear(); }
        robot.clickOn(text("Check disks"));
        until(() -> { synchronized (metrics.refreshed) { return metrics.refreshed.containsAll(List.of("web-01", "db-01", "api-us-1", "homelab")); } }, "every host read");
    }

    @Test
    void theBottomPanelOpensAndCloses() {
        // It starts closed: the space goes to the shells.
        assertFalse(get(() -> console.log().opened.get()));
        robot.clickOn(byTooltip("Open the log"));
        until(() -> console.log().opened.get() && console.log().lookup(".log-body") != null, "open");
        robot.clickOn(byTooltip("Collapse the log"));
        until(() -> !console.log().opened.get() && console.log().lookup(".log-body") == null, "closed again");
        // A click on one of its tabs opens it too.
        robot.clickOn(robot.lookup(n -> n instanceof ToggleButton b && "Task".equals(b.getText())).queryAs(ToggleButton.class));
        until(() -> console.log().opened.get(), "opened by its tab");
    }

    @Test
    void fieldsStopAtTheirLimitAndCutAPaste() {
        on(() -> {
            TextField f = Ui.limit(new TextField(), 5);
            f.setText("abc");
            f.appendText("defgh");
            assertEquals("abcde", f.getText(), "a paste is cut to what fits");
            f.appendText("x");
            assertEquals("abcde", f.getText(), "typing stops at the limit");
            f.selectRange(0, 2);
            f.replaceSelection("XYZW");
            assertEquals("XYcde", f.getText(), "a replacement counts what it removes");
            f.deleteText(0, 1);
            assertEquals("Ycde", f.getText());
            // An emoji is two chars: it is not split in half at the limit.
            f.appendText("\uD83D\uDE00");
            assertEquals("Ycde", f.getText());
        });
    }

    @Test
    void theLastIconOfTheHostColumnHidesItAndTheActivityBarBringsItBack() {
        Supplier<Boolean> shown = () -> get(() -> { Node c = stage.getScene().getRoot().lookup(".column-resources"); return c != null && c.getScene() != null && c.isVisible(); });
        assertTrue(shown.get(), "open at the start");
        robot.clickOn(byTooltip("Hide the sidebar (the Hosts button on the left opens it again)"));
        until(() -> !shown.get(), "the column is gone");
        robot.clickOn(byTooltip("Hosts (opens and closes the sidebar)"));
        until(shown, "back");
    }
}
