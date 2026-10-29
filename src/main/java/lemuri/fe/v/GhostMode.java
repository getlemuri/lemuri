package lemuri.fe.v;

import java.io.IOException;
import java.util.List;
import java.util.Locale;
import java.util.prefs.Preferences;
import javafx.animation.AnimationTimer;
import javafx.animation.PauseTransition;
import javafx.beans.InvalidationListener;
import javafx.beans.value.ChangeListener;
import javafx.collections.ListChangeListener;
import javafx.collections.MapChangeListener;
import javafx.geometry.Insets;
import javafx.geometry.Orientation;
import javafx.geometry.Pos;
import javafx.geometry.Rectangle2D;
import javafx.scene.Cursor;
import javafx.scene.Node;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.Slider;
import javafx.scene.control.SplitPane;
import javafx.scene.control.ToggleButton;
import javafx.scene.control.Tooltip;
import javafx.scene.input.KeyCode;
import javafx.scene.input.KeyEvent;
import javafx.scene.input.MouseButton;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.BorderPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.scene.paint.Color;
import javafx.stage.Stage;
import javafx.stage.StageStyle;
import javafx.util.Duration;
import lemuri.App;
import lemuri.be.util.I18n;
import lemuri.be.util.X11;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Session;
import lemuri.fe.v.sections.TabBar;
import lemuri.fe.v.terminal.Terminal;

/**
 * Ghost mode: the main window goes away and a borderless window takes its place, black and see-through like
 * a conky widget on the desktop, with only the session tabs at the top, the host tree on the left, the
 * terminal and the status bar at the bottom. Everything works as in the console: typing, switching tab, opening a host from the tree. Pressing
 * and holding anywhere moves the window. The way back is the tray icon (its menu, or a click on it): without a
 * tray icon the mode is not offered. It also ends by itself when the last session closes. One for the main
 * window, created by its console; where the window goes and how it comes to the front is in {@link WindowPlacement}.
 */
public final class GhostMode {
    /** How long a press must last before dragging moves the window instead of selecting. */
    private static final Duration HOLD = Duration.millis(450);
    /** Opacity of the black background, remembered between uses: from almost invisible to almost solid. */
    private static final String OPACITY = "ghostOpacity";
    private static final double MIN_OPACITY = 0.15, MAX_OPACITY = 0.95, DEFAULT_OPACITY = 0.55;
    private static final Preferences PREF = Preferences.userRoot().node("lemuri/ui");

    private Stage window, main;
    /** Where the main window was, to put it back exactly there. */
    private double[] bounds;
    private Console console;
    private AppContext app;
    /** The session whose terminal is shown. */
    private Session shown;
    private BorderPane layout;
    /** Tabs on top of tree and terminal: the activity column on the left and the status bar span the whole height and width. */
    private BorderPane inner;
    /** The datacenter button: it shows and hides the host tree, as in the main window. */
    private ToggleButton hostsButton;
    /** The width of the tree when it was hidden, to give it back the same. */
    private double treeWidth = 240;
    /** The tree was closed with the datacenter button: it stays closed the next time too. */
    private boolean treeHidden;
    /** The Terminal tab at the bottom, as in the panel of the main window: it shows and hides the local shell. */
    private ToggleButton shellTab;
    /** The strip with the tab, always at the bottom of the session terminal: the shell, when open, goes below it. */
    private HBox shellHead;
    /** The local shell was opened with the Terminal tab: it is there the next time too. */
    private boolean shellOpen;
    /** Where the separator above the local shell was, as a fraction of the height. */
    private double shellDivider = 0.7;
    /** The local shell of the bottom panel while it is lent to the ghost. */
    private Terminal shell;
    /** On the right of the tree: the session terminal with the Terminal tab, the local shell below when it is open. */
    private SplitPane vertical;
    private ChangeListener<Terminal> onShellExit;
    /** Host tree and terminal, with the separator between them that can be dragged. */
    private SplitPane split;
    private StackPane root;
    private static final List<String> SIDES = List.of("left", "top", "bottom", "right");
    /** The side the ghost sticks to now ("left", "top", "bottom", "right"); empty while it floats free, as it always starts. */
    private String dock = "";
    private boolean maximized;
    /** How thick the docked band is, as dragged by its inner edge: width on the left and right, height above and below. 0 until dragged. */
    private double dockWidth, dockHeight;
    /** How near the inner edge of a docked ghost, in pixels, the mouse grabs it to resize. */
    private static final double EDGE = 6;
    /** Where the window is when it floats free: it goes back there when maximize or docking end. */
    private Rectangle2D floating;
    /** The opacity control, on the right of the tabs: one for every time, so it keeps its value. */
    private HBox opacity;
    /** Where the shown tab is drawn: its terminal, or its parts after a split. */
    private final StackPane slot = new StackPane();
    /** One tree for every time ghost mode is used: it listens to the state for as long as the app lives. */
    private HostTree tree;
    /** The tree with the small bar above it, as the split holds it. */
    private VBox treeColumn;
    private ListChangeListener<Session> onSessions;
    private MapChangeListener<String, Session> onActive;
    private InvalidationListener onPosition, onSessionState;

    /** Where the ghost window goes, and how it is brought to the front. */
    private WindowPlacement placement;

    /** One for the main window: {@code c} is its console, whose sessions the ghost shows. */
    GhostMode(AppContext context, Console c) {
        app = context;
        console = c;
    }

    public boolean active() {
        return window != null;
    }

    /** From the activity bar: the console of the session in view (or of the last one opened) becomes the ghost. */
    public void enter() {
        if (active()) return;
        if (!Tray.present()) {
            Dialogs.warning(I18n.t("ghost.title"), I18n.t("ghost.noTray"));
            return;
        }
        if (app.state.sessions.isEmpty()) return;
        main = (Stage) console.getScene().getWindow();
        if (tree == null) {
            tree = new HostTree(app);
            slot.getStyleClass().add("terminal-slot");
        }
        if (!tree.getStyleClass().contains("ghost-tree")) tree.getStyleClass().add("ghost-tree");
        // Above the tree, on the right, the icon that closes it, as in the main window; the Hosts button opens it again.
        HBox treeHead = new HBox(Ui.space(), Ui.action("sidebar-close", I18n.t("tree.hide"), () -> {
            showTree(false);
            if (hostsButton != null) hostsButton.setSelected(false);
        }));
        treeHead.getStyleClass().add("ghost-tree-head");
        VBox.setVgrow(tree, Priority.ALWAYS);
        treeColumn = new VBox(treeHead, tree);
        treeColumn.getStyleClass().add("ghost-tree-column");

        layout = new BorderPane();
        // The shell stays on the right of the tree: the line beside the tree goes down to the status bar.
        BorderPane upper = new BorderPane(slot);
        vertical = new SplitPane(upper);
        vertical.setOrientation(Orientation.VERTICAL);
        vertical.getStyleClass().add("ghost-split");
        split = new SplitPane(treeColumn, vertical);
        split.getStyleClass().add("ghost-split");
        SplitPane.setResizableWithParent(treeColumn, false);
        split.setDividerPositions(treeWidth / Math.max(480, main.getWidth()));
        if (treeHidden) split.getItems().remove(treeColumn);
        inner = new BorderPane(split);
        shellTab = new ToggleButton(I18n.t("log.tab.terminal"), Icons.icon("terminal", 13));
        shellTab.getStyleClass().add("log-tab");
        shellTab.setFocusTraversable(false);
        shellTab.setSelected(shellOpen);
        shellTab.setOnAction(e -> {
            shellOpen = shellTab.isSelected();
            showShell(shellOpen);
        });
        shellHead = new HBox(shellTab);
        shellHead.getStyleClass().add("ghost-shell-head");
        upper.setBottom(shellHead);
        layout.setCenter(inner);
        layout.setLeft(activityColumn());
        // The shell exited (exit, Ctrl+D): its place goes away.
        onShellExit = (o, before, now) -> {
            if (now == null && shell != null) {
                shellTab.setSelected(false);
                showShell(false);
            }
        };
        console.log().shell.getReadOnlyProperty().addListener(onShellExit);
        if (shellOpen) showShell(true);
        layout.setBottom(console.lendStatusBar());
        root = new StackPane(layout);
        root.getStyleClass().add("ghost");
        background(PREF.getDouble(OPACITY, DEFAULT_OPACITY));
        Scene scene = new Scene(root, main.getWidth(), main.getHeight());
        scene.setFill(Color.TRANSPARENT);
        scene.getStylesheets().add(App.class.getResource("style.css").toExternalForm());

        Stage g = new Stage(StageStyle.TRANSPARENT);
        g.setTitle("Lemuri");
        g.getIcons().setAll(AppIcon.images());
        g.setScene(scene);
        g.setX(main.getX());
        g.setY(main.getY());
        moveOnHold(g, scene);
        resizeDocked(g, scene);
        // F11 hides it; it comes back with the same key if the system shortcut is set (see toggle), or from the tray icon.
        scene.addEventFilter(KeyEvent.KEY_PRESSED, e -> {
            if (e.getCode() == KeyCode.F11) {
                e.consume();
                toggle(main);
            }
        });
        // Closing it from the keyboard (Alt+F4) also brings back the normal window, instead of losing the sessions.
        g.setOnCloseRequest(e -> {
            e.consume();
            leave();
        });
        window = g;
        placement = new WindowPlacement(g);

        onSessions = ch -> refresh();
        onActive = ch -> refresh();
        onPosition = o -> refresh();
        onSessionState = o -> refresh();
        app.state.sessions.addListener(onSessions);
        app.state.activeSession.addListener(onActive);
        app.state.position.addListener(onPosition);
        bounds = new double[] {main.getX(), main.getY(), main.getWidth(), main.getHeight()};
        // The hidden console lets go of the terminals: from now on they live here.
        console.reshow();
        floating = new Rectangle2D(main.getX(), main.getY(), main.getWidth(), main.getHeight());
        maximized = false;
        dock = "";
        refresh();
        // The ghost takes the focus while the main window still has it: hidden first, the focus would go to another
        // app and GNOME would leave the ghost behind it, with only a "ready" notification.
        g.show();
        g.toFront();
        g.requestFocus();
        Tray.hide(main);
        Tray.ghostMode(true);
        WindowPlacement.activate(g);
    }

    /**
     * Shows the right session: the one active for the host chosen in the tree, otherwise the one already shown,
     * otherwise the last one opened. With no sessions left the normal window comes back.
     */
    private void refresh() {
        if (!active()) return;
        if (app.state.sessions.isEmpty()) {
            leave();
            return;
        }
        Session next = null;
        if (app.state.node() instanceof Selection.HostItem d) next = app.state.activeSession.get(d.hostId());
        if (next == null && shown != null && app.state.sessions.contains(shown)) next = shown;
        if (next == null) next = app.state.sessions.get(app.state.sessions.size() - 1).tab();
        shown = next;
        // Only the parts of the tab in view are see-through: the others are drawn in the normal window later.
        for (Session x : app.state.sessions) SessionViews.terminal(x).ghost(x.tab() == shown);
        for (Session s : app.state.sessions) {
            s.state.removeListener(onSessionState);
            s.state.addListener(onSessionState);
            s.synced.removeListener(onSessionState);
            s.synced.addListener(onSessionState);
        }
        Session s = shown;
        TabBar bar = new TabBar(app, app.state.sessions, s, app.sessions::showSession,
                () -> app.sessions.openSession(s.host), I18n.t("console.newSessionOn", s.host.name));
        bar.getStyleClass().add("ghost-tabs");
        bar.addToRight(opacityControl(), separator());
        for (String side : SIDES) bar.addToRight(dockButton(side));
        bar.addToRight(separator(), minimizeButton(), maximizeButton());
        inner.setTop(bar);
        slot.getChildren().setAll(SessionViews.tab(s));
    }

    /**
     * On the left: the app icon at the top, the datacenter button, which opens and closes the tree, and the
     * ghost button, lit and coloured while the mode is on: pressed again, it brings back the normal window.
     */
    private VBox activityColumn() {
        StackPane brand = new StackPane(AppIcon.view(26));
        brand.getStyleClass().add("ghost-brand");
        hostsButton = new ToggleButton(null, Icons.icon("server", 22));
        hostsButton.getStyleClass().add("activity");
        hostsButton.setTooltip(new Tooltip(I18n.t("activity.hosts")));
        hostsButton.setFocusTraversable(false);
        hostsButton.setSelected(split.getItems().contains(treeColumn));
        hostsButton.setOnAction(e -> showTree(hostsButton.isSelected()));
        ToggleButton ghostButton = new ToggleButton(null, Icons.icon("ghost", 22));
        ghostButton.getStyleClass().addAll("activity", "ghost-on");
        ghostButton.setTooltip(new Tooltip(I18n.t("tray.ghostOff")));
        ghostButton.setFocusTraversable(false);
        ghostButton.setSelected(true);
        ghostButton.setOnAction(e -> leave());
        VBox column = new VBox(brand, hostsButton, ghostButton);
        column.getStyleClass().add("ghost-activity");
        return column;
    }

    /** The local shell of the bottom panel, under the session terminal and its tab; given back to the panel when it goes. */
    private void showShell(boolean show) {
        if (show == (shell != null)) return;
        if (show) {
            shell = console.log().lendShell();
            StackPane screen = new StackPane(shell);
            screen.getStyleClass().add("terminal-slot");
            vertical.getItems().add(screen);
            vertical.setDividerPositions(shellDivider);
            shell.ghost(true);
            shell.focus();
        } else {
            if (vertical.getDividers().size() > 0) shellDivider = vertical.getDividerPositions()[0];
            vertical.getItems().remove(1, vertical.getItems().size());
            shell.ghost(false);
            ((StackPane) shell.getParent()).getChildren().clear();
            shell = null;
            console.log().takeBackShell();
        }
    }

    private void showTree(boolean show) {
        treeHidden = !show;
        if (show == split.getItems().contains(treeColumn)) return;
        if (!show) {
            treeWidth = Math.max(120, treeColumn.getWidth());
            split.getItems().remove(treeColumn);
        } else {
            split.getItems().add(0, treeColumn);
            split.setDividerPositions(treeWidth / Math.max(treeWidth + 240, split.getWidth()));
        }
    }

    /**
     * Press and hold, then drag: the whole window moves. A short click or a quick drag stays what it is (a click,
     * a text selection in the terminal, dragging a host in the tree).
     */
    private void moveOnHold(Stage g, Scene scene) {
        PauseTransition hold = new PauseTransition(HOLD);
        double[] start = new double[2], offset = new double[2];
        boolean[] moving = {false};
        scene.addEventFilter(MouseEvent.MOUSE_PRESSED, e -> {
            // Docked or maximized, the window stays where it is put.
            if (e.getButton() != MouseButton.PRIMARY || onControl(e) || maximized || !dock.isEmpty()) return;
            start[0] = e.getScreenX();
            start[1] = e.getScreenY();
            offset[0] = g.getX() - e.getScreenX();
            offset[1] = g.getY() - e.getScreenY();
            moving[0] = false;
            hold.setOnFinished(x -> {
                moving[0] = true;
                scene.setCursor(Cursor.MOVE);
            });
            hold.playFromStart();
        });
        scene.addEventFilter(MouseEvent.MOUSE_DRAGGED, e -> {
            if (moving[0]) {
                g.setX(e.getScreenX() + offset[0]);
                g.setY(e.getScreenY() + offset[1]);
                e.consume();
            } else if (Math.hypot(e.getScreenX() - start[0], e.getScreenY() - start[1]) > 5) {
                // It moved before the hold ended: an ordinary drag.
                hold.stop();
            }
        });
        scene.addEventFilter(MouseEvent.DRAG_DETECTED, e -> {
            if (moving[0]) e.consume();
        });
        scene.addEventFilter(MouseEvent.MOUSE_RELEASED, e -> {
            hold.stop();
            if (moving[0]) {
                moving[0] = false;
                scene.setCursor(null);
            }
        });
    }

    /** Only the background changes: the text of the terminal stays fully readable. */
    private void background(double alpha) {
        root.setStyle("-fx-background-color: rgba(0, 0, 0, " + String.format(Locale.ROOT, "%.2f", alpha) + ");");
    }

    /** A sun and a small slider; the mouse wheel on it works too. */
    private Node opacityControl() {
        if (opacity != null) return opacity;
        Slider s = new Slider(MIN_OPACITY, MAX_OPACITY,
                Math.max(MIN_OPACITY, Math.min(MAX_OPACITY, PREF.getDouble(OPACITY, DEFAULT_OPACITY))));
        s.setPrefWidth(90);
        s.setFocusTraversable(false);
        // The sun grows as the background fades: bigger means more see-through.
        Node sun = Icons.icon("sun", 13);
        StackPane sunBox = new StackPane(sun);
        sunBox.setMinSize(22, 22);
        sunBox.setPrefSize(22, 22);
        sunBox.setMaxSize(22, 22);
        Runnable grow = () -> {
            double t = (MAX_OPACITY - s.getValue()) / (MAX_OPACITY - MIN_OPACITY);
            sun.setScaleX(0.8 + 0.8 * t);
            sun.setScaleY(0.8 + 0.8 * t);
        };
        s.valueProperty().addListener((o, a, b) -> {
            background(b.doubleValue());
            PREF.putDouble(OPACITY, b.doubleValue());
            grow.run();
        });
        grow.run();
        s.setOnScroll(e -> s.setValue(s.getValue() + (e.getDeltaY() > 0 ? 0.05 : -0.05)));
        opacity = new HBox(6, sunBox, s);
        opacity.setAlignment(Pos.CENTER);
        opacity.getStyleClass().add("ghost-opacity");
        Tooltip.install(opacity, new Tooltip(I18n.t("ghost.opacity")));
        return opacity;
    }

    /** A short vertical line between groups of commands on the right of the tabs. */
    private static Node separator() {
        Region r = new Region();
        r.getStyleClass().add("ghost-bar-sep");
        HBox.setMargin(r, new Insets(0, 6, 0, 6));
        return r;
    }

    /** On the right of the slider: the ghost goes down to the dock, and comes back from it or from a click on the tray icon. */
    private Node minimizeButton() {
        Button b = Ui.action("minimize", I18n.t("window.minimize"), () -> window.setIconified(true));
        b.getStyleClass().add("ghost-control");
        return b;
    }

    /** Next to minimize: the ghost fills the screen it is on, and the second press puts it back where it was. */
    private Node maximizeButton() {
        Button b = Ui.action(maximized ? "restore" : "maximize",
                I18n.t(maximized ? "window.restore" : "window.maximize"), this::toggleMaximized);
        b.getStyleClass().add("ghost-control");
        return b;
    }

    /** One of the four sides: the icon shows the screen with the band where the ghost goes, lit while it is there. */
    private Node dockButton(String side) {
        String title = switch (side) {
            case "left" -> I18n.t("ghost.dock.left");
            case "top" -> I18n.t("ghost.dock.top");
            case "bottom" -> I18n.t("ghost.dock.bottom");
            default -> I18n.t("ghost.dock.right");
        };
        Button b = Ui.action("dock-" + side, title, () -> dock(side));
        b.getStyleClass().add("ghost-control");
        if (side.equals(dock)) b.getStyleClass().add("on");
        return b;
    }

    /** Sticks the ghost to that side of the screen; pressed again on the same side, the ghost floats free where it was. */
    private void dock(String side) {
        if (dock.isEmpty() && !maximized) floating = WindowPlacement.bounds(window);
        dock = side.equals(dock) ? "" : side;
        maximized = false;
        place();
        refresh();
    }

    private void toggleMaximized() {
        if (dock.isEmpty() && !maximized) floating = WindowPlacement.bounds(window);
        maximized = !maximized;
        place();
        // The tabs are drawn again, and with them the buttons with the new state.
        refresh();
    }

    /**
     * Puts the window where its state says: the whole usable area if maximized, a band along the docked side
     * (as thick as the free window, at most half the screen), otherwise where it floated.
     */
    private void place() {
        Rectangle2D v = WindowPlacement.screen(floating);
        Rectangle2D r;
        if (maximized) r = v;
        else if (dock.isEmpty()) r = floating;
        else {
            double w = dockWidth > 0 ? thickness(dockWidth, v.getWidth(), 320) : Math.max(320, Math.min(floating.getWidth(), v.getWidth() / 2));
            double h = dockHeight > 0 ? thickness(dockHeight, v.getHeight(), 240) : Math.max(240, Math.min(floating.getHeight(), v.getHeight() / 2));
            r = switch (dock) {
                case "left" -> new Rectangle2D(v.getMinX(), v.getMinY(), w, v.getHeight());
                case "right" -> new Rectangle2D(v.getMaxX() - w, v.getMinY(), w, v.getHeight());
                case "top" -> new Rectangle2D(v.getMinX(), v.getMinY(), v.getWidth(), h);
                default -> new Rectangle2D(v.getMinX(), v.getMaxY() - h, v.getWidth(), h);
            };
        }
        placement.place(r);
    }

    /** A dragged thickness within reason: at least {@code min}, at most nine tenths of the screen. */
    private static double thickness(double wanted, double screen, double min) {
        return Math.max(min, Math.min(wanted, screen * 0.9));
    }

    /**
     * Docked, the edge that faces the screen can be dragged: docked below, the top edge raises and lowers the
     * window; on the left, the right edge makes it wider or narrower. The thickness stays for the next docks.
     * Resizing this see-through window with its terminals at every frame is slow on GNOME, while moving a small
     * window is not: during the drag a thin line shows where the edge will be, and the window takes that size
     * when the button is released. GNOME also stops sending drag events once something moves under the mouse,
     * so while the button is held the pointer is read from X11 at every frame; without X11 the events are used.
     */
    private void resizeDocked(Stage g, Scene scene) {
        boolean[] dragging = {false};
        X11[] x11 = {null};
        AnimationTimer follow = new AnimationTimer() {
            @Override
            public void handle(long now) {
                int[] p = x11[0] != null ? x11[0].pointer() : null;
                if (p == null || p[2] == 0) {
                    stop();
                    endResize(g, scene, dragging);
                    return;
                }
                guideTo(g, p[0], p[1]);
            }

            @Override
            public void stop() {
                super.stop();
                closeQuietly(x11[0]);
                x11[0] = null;
            }
        };
        scene.addEventFilter(MouseEvent.MOUSE_MOVED, e -> {
            if (dragging[0]) return;
            scene.setCursor(onInnerEdge(e, scene) ? (dock.equals("left") || dock.equals("right") ? Cursor.H_RESIZE : Cursor.V_RESIZE) : null);
        });
        scene.addEventFilter(MouseEvent.MOUSE_PRESSED, e -> {
            if (e.getButton() != MouseButton.PRIMARY || !onInnerEdge(e, scene)) return;
            dragging[0] = true;
            e.consume();
            guideTo(g, e.getScreenX(), e.getScreenY());
            // One X connection for the whole drag: opening one at every frame would add to the lag.
            x11[0] = X11.pointerTracker();
            int[] p = x11[0] != null ? x11[0].pointer() : null;
            if (p != null && p[2] == 1) follow.start();
            else {
                closeQuietly(x11[0]);
                x11[0] = null;
            }
        });
        scene.addEventFilter(MouseEvent.MOUSE_DRAGGED, e -> {
            if (!dragging[0]) return;
            e.consume();
            guideTo(g, e.getScreenX(), e.getScreenY());
        });
        scene.addEventFilter(MouseEvent.DRAG_DETECTED, e -> {
            if (dragging[0]) e.consume();
        });
        scene.addEventFilter(MouseEvent.MOUSE_RELEASED, e -> {
            if (!dragging[0]) return;
            e.consume();
            follow.stop();
            endResize(g, scene, dragging);
        });
    }

    private static void closeQuietly(X11 x) {
        if (x == null) return;
        try {
            x.close();
        } catch (IOException e) {
            // the drag is over anyway
        }
    }

    /** The thin line that shows, during a drag, where the inner edge of the docked window will be. */
    private Stage guide;
    /** The thickness the drag has reached: the window takes it on release. */
    private double guided;

    private static final double GUIDE = 3;

    private void endResize(Stage g, Scene scene, boolean[] dragging) {
        if (!dragging[0]) return;
        dragging[0] = false;
        scene.setCursor(null);
        if (guide != null) guide.hide();
        if (guided > 0) {
            if (dock.equals("left") || dock.equals("right")) dockWidth = guided;
            else dockHeight = guided;
        }
        guided = 0;
        place();
    }

    /** Puts the line where the inner edge would be with the mouse at {@code x}, {@code y} (on the screen). */
    private void guideTo(Stage g, double x, double y) {
        Rectangle2D v = WindowPlacement.screen(WindowPlacement.bounds(g));
        boolean across = dock.equals("left") || dock.equals("right");
        double t = switch (dock) {
            case "bottom" -> v.getMaxY() - y;
            case "top" -> y - v.getMinY();
            case "left" -> x - v.getMinX();
            case "right" -> v.getMaxX() - x;
            default -> -1;
        };
        if (t < 0) return;
        guided = Math.round(across ? thickness(t, v.getWidth(), 320) : thickness(t, v.getHeight(), 240));
        if (guide == null) {
            guide = new Stage(StageStyle.TRANSPARENT);
            guide.initOwner(g);
            guide.setAlwaysOnTop(true);
            Region line = new Region();
            line.getStyleClass().add("ghost-resize-guide");
            Scene sc = new Scene(new StackPane(line), Color.TRANSPARENT);
            sc.getStylesheets().add(App.class.getResource("style.css").toExternalForm());
            guide.setScene(sc);
        }
        double gx, gy, gw, gh;
        switch (dock) {
            case "bottom" -> { gx = v.getMinX(); gy = v.getMaxY() - guided - GUIDE / 2; gw = v.getWidth(); gh = GUIDE; }
            case "top" -> { gx = v.getMinX(); gy = v.getMinY() + guided - GUIDE / 2; gw = v.getWidth(); gh = GUIDE; }
            case "left" -> { gx = v.getMinX() + guided - GUIDE / 2; gy = v.getMinY(); gw = GUIDE; gh = v.getHeight(); }
            default -> { gx = v.getMaxX() - guided - GUIDE / 2; gy = v.getMinY(); gw = GUIDE; gh = v.getHeight(); }
        }
        if (!guide.isShowing()) {
            guide.setWidth(gw);
            guide.setHeight(gh);
            guide.setX(gx);
            guide.setY(gy);
            guide.show();
        }
        // Only moved while dragging: a move is quick, a resize is not.
        guide.setX(gx);
        guide.setY(gy);
    }

    /** The mouse is on the edge of the docked window that faces the rest of the screen. */
    private boolean onInnerEdge(MouseEvent e, Scene scene) {
        if (dock.isEmpty() || maximized) return false;
        return switch (dock) {
            case "bottom" -> e.getSceneY() <= EDGE;
            case "top" -> e.getSceneY() >= scene.getHeight() - EDGE;
            case "left" -> e.getSceneX() >= scene.getWidth() - EDGE;
            case "right" -> e.getSceneX() <= EDGE;
            default -> false;
        };
    }

    /**
     * F11, from the ghost itself or from anywhere through the system shortcut ({@code lemuri --toggle}): hides
     * the ghost, or brings it back in its place. Without ghost mode it does the same with the main window.
     */
    public void toggle(Stage mainWindow) {
        if (window == null) {
            if (mainWindow.isShowing() && !mainWindow.isIconified() && Tray.present()) Tray.hide(mainWindow);
            else Tray.bringToFront(mainWindow);
            return;
        }
        if (window.isShowing() && !window.isIconified()) {
            // Free, it may have been moved since: it comes back where it is now.
            if (dock.isEmpty() && !maximized) floating = WindowPlacement.bounds(window);
            window.hide();
        } else restore();
    }

    /** Brings back a minimized or hidden ghost window; false if there was none to bring back. */
    boolean restore() {
        if (window == null || window.isShowing() && !window.isIconified()) return false;
        if (!window.isShowing()) {
            window.show();
            place();
        }
        window.setIconified(false);
        window.toFront();
        window.requestFocus();
        WindowPlacement.activate(window);
        return true;
    }

    /** A press on the opacity slider or on the separator is for them, even if held: it never moves the window. */
    private boolean onControl(MouseEvent e) {
        for (Node n = e.getPickResult().getIntersectedNode(); n != null; n = n.getParent())
            if (n instanceof Slider || n.getStyleClass().contains("split-pane-divider")) return true;
        return false;
    }

    /** Back to the normal window, with the terminal in its console. From the tray, or when the sessions are over. */
    public void leave() {
        if (!active()) return;
        Stage g = window;
        window = null;
        Tray.ghostMode(false);
        maximized = false;
        app.state.sessions.removeListener(onSessions);
        app.state.activeSession.removeListener(onActive);
        app.state.position.removeListener(onPosition);
        for (Session s : app.state.sessions) {
            s.state.removeListener(onSessionState);
            s.synced.removeListener(onSessionState);
        }
        for (Session s : app.state.sessions) SessionViews.terminal(s).ghost(false);
        shown = null;
        slot.getChildren().clear();
        console.log().shell.getReadOnlyProperty().removeListener(onShellExit);
        boolean open = shellOpen;
        showShell(false);
        shellOpen = open;
        if (split.getItems().contains(treeColumn)) treeWidth = Math.max(120, treeColumn.getWidth());
        split.getItems().clear();
        layout.setBottom(null);
        console.takeBackStatusBar();
        main.setX(bounds[0]);
        main.setY(bounds[1]);
        main.setWidth(bounds[2]);
        main.setHeight(bounds[3]);
        // The main window takes the focus while the ghost still has it, as on the way in: closed first, the ghost
        // would hand the focus to another app and GNOME would leave the window behind, with a "ready" notification.
        main.show();
        // The console page is rebuilt so the terminal goes back into it.
        console.reshow();
        main.toFront();
        main.requestFocus();
        g.close();
        // From the tray icon the request does not come from the app: GNOME needs it the way a dock asks.
        WindowPlacement.activate(main);
    }
}
