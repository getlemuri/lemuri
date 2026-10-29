package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.prefs.Preferences;
import javafx.geometry.Point2D;
import javafx.geometry.Rectangle2D;
import javafx.scene.Cursor;
import javafx.scene.Node;
import javafx.scene.control.ButtonBase;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.Labeled;
import javafx.scene.control.Slider;
import javafx.scene.control.ToggleButton;
import javafx.scene.input.MouseButton;
import javafx.stage.Screen;
import javafx.stage.Stage;
import javafx.stage.Window;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.SessionStatus;
import lemuri.fe.m.Session;

/** Ghost mode, on a made-up tray icon (ghost mode is offered only with one). */
class GhostUiTest extends UiTest {
    private Object indicator;

    @BeforeEach
    void trayIcon() throws Exception {
        // An indicator made without its constructor (no D-Bus), with an empty menu: Tray.present() says yes.
        var unsafeField = Class.forName("sun.misc.Unsafe").getDeclaredField("theUnsafe");
        unsafeField.setAccessible(true);
        sun.misc.Unsafe unsafe = (sun.misc.Unsafe) unsafeField.get(null);
        indicator = unsafe.allocateInstance(AppIndicator.class);
        var menu = AppIndicator.class.getDeclaredField("menu");
        unsafe.putObject(indicator, unsafe.objectFieldOffset(menu), unsafe.allocateInstance(menu.getType()));
        var f = Tray.class.getDeclaredField("indicator");
        f.setAccessible(true);
        f.set(null, indicator);
    }

    @AfterEach
    void noTrayIcon() throws Exception {
        var f = Tray.class.getDeclaredField("indicator");
        f.setAccessible(true);
        f.set(null, null);
    }

    /** The "Exit ghost mode" flag of the tray menu. */
    private boolean trayOffersExit() throws Exception {
        var menuField = AppIndicator.class.getDeclaredField("menu");
        menuField.setAccessible(true);
        Object menu = menuField.get(indicator);
        var ghost = menu.getClass().getDeclaredField("ghost");
        ghost.setAccessible(true);
        return ghost.getBoolean(menu);
    }

    private Session enterWithASession() {
        robot.clickOn(text("db-01"));
        settle();
        robot.clickOn(text("Console"));
        until(() -> sessions().size() == 1 && sessions().get(0).state.get() == SessionStatus.CONNECTED, "session");
        robot.clickOn(byTooltip("Ghost mode"));
        until(() -> console.ghost().active(), "ghost mode on");
        return sessions().get(0);
    }

    private Stage ghostWindow() {
        return get(() -> (Stage) Window.getWindows().stream().filter(w -> w != stage && w.isShowing()
                && w.getScene() != null && w.getScene().getRoot().getStyleClass().contains("ghost")).findFirst().orElseThrow());
    }

    @Test
    void enterAndLeave() throws Exception {
        enterWithASession();
        assertFalse(get(stage::isShowing), "the main window goes away");
        assertNotNull(ghostWindow());
        assertTrue(shows("db-01 #1"));
        until(() -> { try { return trayOffersExit(); } catch (Exception e) { return false; } }, "the tray offers to exit ghost mode");
        robot.clickOn(byTooltip("Exit ghost mode"));
        until(() -> !console.ghost().active() && stage.isShowing(), "back to the window");
        until(() -> { try { return !trayOffersExit(); } catch (Exception e) { return true; } }, "the tray no longer offers it");
    }

    @Test
    void theHostTreeOpensAndCloses() {
        enterWithASession();
        assertTrue(get(() -> !ghostWindow().getScene().getRoot().lookupAll(".ghost-tree").isEmpty()
                && ghostWindow().getScene().getRoot().lookup(".ghost-tree").isVisible() && ghostWindow().getScene().getRoot().lookup(".ghost-tree").getScene() != null));
        robot.clickOn(byTooltip("Hosts (opens and closes the sidebar)"));
        until(() -> ghostWindow().getScene().getRoot().lookup(".ghost-tree") == null, "tree hidden");
        robot.clickOn(byTooltip("Hosts (opens and closes the sidebar)"));
        until(() -> ghostWindow().getScene().getRoot().lookup(".ghost-tree") != null, "tree back");
    }

    @Test
    void theIconAboveTheTreeHidesItAndTheHostsButtonBringsItBack() {
        enterWithASession();
        Node hide = robot.lookup(n -> n instanceof ButtonBase b && b.getTooltip() != null
                && b.getTooltip().getText().startsWith("Hide the sidebar") && n.getScene() != null
                && n.getScene().getRoot().getStyleClass().contains("ghost")).query();
        robot.clickOn(hide);
        until(() -> ghostWindow().getScene().getRoot().lookup(".ghost-tree") == null, "tree hidden");
        ToggleButton hosts = (ToggleButton) byTooltip("Hosts (opens and closes the sidebar)");
        assertFalse(get(hosts::isSelected), "the Hosts button shows the tree is closed");
        robot.clickOn(hosts);
        until(() -> ghostWindow().getScene().getRoot().lookup(".ghost-tree") != null, "tree back");
    }

    @Test
    void theTerminalTabOpensTheLocalShellAndGivesItBack() {
        enterWithASession();
        Node tab = robot.lookup(n -> n instanceof ToggleButton t && "Terminal".equals(t.getText())
                && n.getScene() != null && n.getScene().getRoot().getStyleClass().contains("ghost")).query();
        double width = get(() -> tab.getLayoutBounds().getWidth());
        robot.clickOn(tab);
        until(() -> console.log().shell.get() != null && console.log().shell.get().getScene() == ghostWindow().getScene(), "the local shell in the ghost");
        assertEquals(width, get(() -> tab.getLayoutBounds().getWidth()), 0.5, "the tab keeps its size when open");
        robot.clickOn(tab);
        until(() -> console.log().shell.get() == null || console.log().shell.get().getScene() != ghostWindow().getScene(), "closed");
        robot.clickOn(tab);
        until(() -> console.log().shell.get() != null && console.log().shell.get().getScene() == ghostWindow().getScene(), "open again");
        robot.clickOn(byTooltip("Exit ghost mode"));
        until(() -> !console.ghost().active(), "left");
    }

    @Test
    void opacityIsRemembered() {
        enterWithASession();
        Slider slider = robot.lookup(".slider").queryAs(Slider.class);
        on(() -> slider.setValue(0.8));
        settle();
        assertEquals(0.8, Preferences.userRoot().node("lemuri/ui").getDouble("ghostOpacity", 0), 0.01);
    }

    @Test
    void dockingToTheRightMakesABand() {
        enterWithASession();
        robot.clickOn(byTooltip("Stick to the right side (F11 shows and hides)"));
        settle();
        Stage g = ghostWindow();
        Rectangle2D screen = get(() -> Screen.getPrimary().getVisualBounds());
        until(() -> g.getWidth() <= screen.getWidth() / 2 + 1 && Math.abs(g.getX() + g.getWidth() - screen.getMaxX()) < 2, "a band on the right");
        robot.clickOn(byTooltip("Stick to the right side (F11 shows and hides)"));
        until(() -> !(g.getWidth() <= screen.getWidth() / 2 + 1 && Math.abs(g.getX() + g.getWidth() - screen.getMaxX()) < 2) || g.getWidth() > 0, "free again");
    }

    @Test
    void dockedBelowTheTopEdgeRaisesAndLowersTheWindow() {
        enterWithASession();
        robot.clickOn(byTooltip("Stick to the bottom (F11 shows and hides)"));
        Stage g = ghostWindow();
        Rectangle2D screen = get(() -> Screen.getPrimary().getVisualBounds());
        until(() -> Math.abs(g.getY() + g.getHeight() - screen.getMaxY()) < 2 && g.getWidth() >= screen.getWidth() - 1, "a band at the bottom");
        double before = get(g::getHeight);
        // Grab the top edge and pull it up by 150 pixels: taller, still on the bottom of the screen.
        double x = get(() -> g.getX() + g.getWidth() / 2), y = get(() -> g.getY() + 2);
        robot.moveTo(new Point2D(x, y));
        assertEquals(Cursor.V_RESIZE, get(() -> g.getScene().getCursor()), "the edge shows it can be dragged");
        robot.press(MouseButton.PRIMARY);
        robot.moveTo(new Point2D(x, y - 75));
        robot.moveTo(new Point2D(x, y - 150));
        robot.release(MouseButton.PRIMARY);
        until(() -> Math.abs(g.getHeight() - (before + 150)) < 3 && Math.abs(g.getY() + g.getHeight() - screen.getMaxY()) < 2, "taller by 150, still at the bottom");
    }

    @Test
    void menusInGhostModeAreDarkAlsoInSubmenus() {
        Session s = enterWithASession();
        contextMenu(() -> get(() -> SessionViews.terminal(s)));
        until(() -> all(".context-menu").stream().anyMatch(Node::isVisible), "the menu");
        assertTrue(get(() -> Window.getWindows().stream().filter(w -> w instanceof ContextMenu && w.isShowing())
                .allMatch(w -> ((ContextMenu) w).getStyleClass().contains("ghost-menu"))), "the menu is dark");
        // Opening a submenu (pointer over "Split right"): it takes the classes of its menu.
        Node split = robot.lookup(n -> n.getStyleClass().contains("menu") && n.lookup(".label") instanceof Labeled l
                && "Split right".equals(l.getText()) && n.getScene() != null).query();
        robot.moveTo(split);
        on(split::requestFocus);
        until(() -> Window.getWindows().stream().filter(w -> w instanceof ContextMenu && w.isShowing()).count() >= 2, "a submenu");
        assertTrue(get(() -> Window.getWindows().stream().filter(w -> w instanceof ContextMenu && w.isShowing())
                .allMatch(w -> ((ContextMenu) w).getStyleClass().contains("ghost-menu"))), "the submenu is dark too");
    }

    @Test
    void closingTheLastSessionEndsGhostMode() {
        Session s = enterWithASession();
        on(() -> app.sessions.closeSession(s));
        until(() -> !console.ghost().active() && stage.isShowing(), "back to the window");
    }
}
