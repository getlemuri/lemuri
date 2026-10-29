package lemuri.fe.v;

import java.util.prefs.Preferences;
import javafx.css.PseudoClass;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.geometry.Rectangle2D;
import javafx.scene.Cursor;
import javafx.scene.Node;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBase;
import javafx.scene.control.ComboBoxBase;
import javafx.scene.control.TextInputControl;
import javafx.scene.control.Tooltip;
import javafx.scene.input.MouseButton;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;
import javafx.scene.paint.Color;
import javafx.scene.shape.Rectangle;
import javafx.stage.Screen;
import javafx.stage.Stage;
import javafx.stage.StageStyle;
import lemuri.be.util.I18n;
import lemuri.be.util.Os;

/**
 * Window without the system frame: the console top bar is also the title bar,
 * with the minimize / maximize / close buttons. Rounded corners and shadow come
 * from a transparent stage; dragging, double click and resizing from the edges
 * are done here.
 */
public final class WindowFrame {
    /** Transparent space around the window, where the shadow is drawn. */
    private static final double SHADOW = 12;
    /** Edge band that resizes, measured from the visible edge inwards and outwards. */
    private static final double BORDER = 6;
    private static final double CORNER = 12;
    /** Below this width the top bar buttons no longer fit. */
    private static final double MIN_L = 1280, MIN_A = 600;
    private static final String PREF = "customFrame";

    private final Stage stage;
    private final Console console;
    private final StackPane outer;
    private final Rectangle clip = new Rectangle();
    private final Region shadow = new Region();
    private final Button maximize;
    private Rectangle2D beforeMaximize;
    private boolean maximized;

    // Drag and resize in progress.
    private double dragX, dragY, stageX, stageY, stageL, stageA;
    private Cursor border = Cursor.DEFAULT;

    /**
     * Whether the custom frame is on: changed from the settings, it takes effect at the next start.
     * On Mac it starts off: the system frame, with the traffic lights, is what people expect.
     */
    public static boolean active() {
        return Preferences.userRoot().node("lemuri/ui").getBoolean(PREF, !Os.MAC);
    }

    public static void set(boolean yes) {
        Preferences.userRoot().node("lemuri/ui").putBoolean(PREF, yes);
    }

    /** Mounts the console in the stage, with the custom frame or with the system one. */
    public static Scene mount(Stage stage, Console console, Runnable close, double l, double a) {
        if (!active()) return new Scene(console, l, a);
        return new WindowFrame(stage, console, close).scene(l, a);
    }

    private WindowFrame(Stage stage, Console console, Runnable close) {
        this.stage = stage;
        this.console = console;
        stage.initStyle(StageStyle.TRANSPARENT);

        // The shadow is a box behind the console, which instead is clipped with rounded corners.
        shadow.getStyleClass().add("window-shadow");
        console.setClip(clip);
        clip.widthProperty().bind(console.widthProperty());
        clip.heightProperty().bind(console.heightProperty());
        outer = new StackPane(shadow, console);
        outer.getStyleClass().add("outer-frame");

        maximize = windowButton("maximize", I18n.t("window.maximize"), this::toggleMaximized);
        Button minimize = windowButton("minimize", I18n.t("window.minimize"), () -> stage.setIconified(true));
        Button quit = windowButton("close", I18n.t("window.close"), close);
        quit.getStyleClass().add("close-control");
        HBox controls = new HBox(2, minimize, maximize, quit);
        controls.setAlignment(Pos.CENTER);
        controls.getStyleClass().add("window-controls");
        console.topBar.getChildren().add(controls);

        drag(console.topBar);
        refreshFrame();
    }

    private Scene scene(double l, double a) {
        Scene s = new Scene(outer, l + 2 * SHADOW, a + 2 * SHADOW, Color.TRANSPARENT);
        stage.setMinWidth(MIN_L);
        stage.setMinHeight(MIN_A);
        // Filters on the scene: the edges win over the controls below them.
        s.addEventFilter(MouseEvent.MOUSE_MOVED, e -> {
            border = maximized ? Cursor.DEFAULT : cursor(e.getSceneX(), e.getSceneY(), s);
            s.setCursor(border);
        });
        s.addEventFilter(MouseEvent.MOUSE_PRESSED, e -> {
            if (border == Cursor.DEFAULT || e.getButton() != MouseButton.PRIMARY) return;
            rememberStart(e);
            e.consume();
        });
        s.addEventFilter(MouseEvent.MOUSE_DRAGGED, e -> {
            if (border == Cursor.DEFAULT) return;
            resize(e);
            e.consume();
        });
        s.addEventFilter(MouseEvent.MOUSE_RELEASED, e -> {
            if (border != Cursor.DEFAULT) e.consume();
        });
        return s;
    }

    private static Button windowButton(String icon, String title, Runnable action) {
        Button b = new Button(null, Icons.icon(icon, 14));
        b.getStyleClass().add("window-control");
        b.setFocusTraversable(false);
        b.setTooltip(new Tooltip(title));
        b.setOnAction(e -> action.run());
        return b;
    }

    // ── Moving ───────────────────────────────────────────────────────────────

    /** True if the click landed on a bar control (button, field, menu), which must be left to do its job. */
    private static boolean onCheck(Node n, Node bar) {
        for (; n != null && n != bar; n = n.getParent()) {
            if (n instanceof ButtonBase || n instanceof TextInputControl || n instanceof ComboBoxBase) return true;
        }
        return false;
    }

    private void drag(Node bar) {
        bar.setOnMousePressed(e -> {
            if (e.getButton() != MouseButton.PRIMARY || onCheck((Node) e.getTarget(), bar)) return;
            rememberStart(e);
        });
        bar.setOnMouseDragged(e -> {
            if (e.getButton() != MouseButton.PRIMARY || border != Cursor.DEFAULT || onCheck((Node) e.getTarget(), bar)) return;
            if (maximized) {
                // As in operating systems: back to the previous size, with the pointer at the same proportional spot.
                double quota = (e.getScreenX() - stage.getX()) / stage.getWidth();
                restore();
                stageX = e.getScreenX() - stage.getWidth() * quota;
                stageY = e.getScreenY() - SHADOW - 20;
                dragX = e.getScreenX();
                dragY = e.getScreenY();
            }
            stage.setX(stageX + e.getScreenX() - dragX);
            stage.setY(Math.max(visible().getMinY() - SHADOW, stageY + e.getScreenY() - dragY));
        });
        bar.setOnMouseClicked(e -> {
            if (e.getButton() == MouseButton.PRIMARY && e.getClickCount() == 2 && !onCheck((Node) e.getTarget(), bar)) toggleMaximized();
        });
    }

    private void rememberStart(MouseEvent e) {
        dragX = e.getScreenX();
        dragY = e.getScreenY();
        stageX = stage.getX();
        stageY = stage.getY();
        stageL = stage.getWidth();
        stageA = stage.getHeight();
    }

    // ── Resizing ─────────────────────────────────────────────────────────────

    private Cursor cursor(double x, double y, Scene s) {
        double l = s.getWidth(), a = s.getHeight();
        boolean sx = x < SHADOW + BORDER, dx = x > l - SHADOW - BORDER;
        boolean on = y < SHADOW + BORDER, down = y > a - SHADOW - BORDER;
        if (on && sx) return Cursor.NW_RESIZE;
        if (on && dx) return Cursor.NE_RESIZE;
        if (down && sx) return Cursor.SW_RESIZE;
        if (down && dx) return Cursor.SE_RESIZE;
        if (sx) return Cursor.W_RESIZE;
        if (dx) return Cursor.E_RESIZE;
        if (on) return Cursor.N_RESIZE;
        if (down) return Cursor.S_RESIZE;
        return Cursor.DEFAULT;
    }

    private void resize(MouseEvent e) {
        double dx = e.getScreenX() - dragX, dy = e.getScreenY() - dragY;
        double x = stageX, y = stageY, l = stageL, a = stageA;
        if (border == Cursor.E_RESIZE || border == Cursor.NE_RESIZE || border == Cursor.SE_RESIZE) l = stageL + dx;
        if (border == Cursor.S_RESIZE || border == Cursor.SW_RESIZE || border == Cursor.SE_RESIZE) a = stageA + dy;
        if (border == Cursor.W_RESIZE || border == Cursor.NW_RESIZE || border == Cursor.SW_RESIZE) {
            l = Math.max(MIN_L, stageL - dx);
            x = stageX + stageL - l;
        }
        if (border == Cursor.N_RESIZE || border == Cursor.NW_RESIZE || border == Cursor.NE_RESIZE) {
            a = Math.max(MIN_A, stageA - dy);
            y = stageY + stageA - a;
        }
        stage.setX(x);
        stage.setY(y);
        stage.setWidth(Math.max(MIN_L, l));
        stage.setHeight(Math.max(MIN_A, a));
    }

    // ── Maximizing ───────────────────────────────────────────────────────────

    /** Usable area of the screen the window is on, system panels excluded. */
    private Rectangle2D visible() {
        var screens = Screen.getScreensForRectangle(stage.getX() + SHADOW, stage.getY() + SHADOW, Math.max(1, stage.getWidth() - 2 * SHADOW), 1);
        return (screens.isEmpty() ? Screen.getPrimary() : screens.get(0)).getVisualBounds();
    }

    /**
     * Maximized by hand on the usable area: setMaximized on a transparent stage
     * would also cover the system panels and would leave the shadow margin.
     */
    private void toggleMaximized() {
        if (maximized) {
            restore();
            return;
        }
        beforeMaximize = new Rectangle2D(stage.getX(), stage.getY(), stage.getWidth(), stage.getHeight());
        Rectangle2D v = visible();
        maximized = true;
        refreshFrame();
        stage.setX(v.getMinX());
        stage.setY(v.getMinY());
        stage.setWidth(v.getWidth());
        stage.setHeight(v.getHeight());
    }

    private void restore() {
        maximized = false;
        refreshFrame();
        if (beforeMaximize != null) {
            stage.setX(beforeMaximize.getMinX());
            stage.setY(beforeMaximize.getMinY());
            stage.setWidth(beforeMaximize.getWidth());
            stage.setHeight(beforeMaximize.getHeight());
        }
    }

    /** Maximized: no margin or rounded corners, the window goes edge to edge. */
    private void refreshFrame() {
        double m = maximized ? 0 : SHADOW, r = maximized ? 0 : CORNER * 2;
        outer.setPadding(new Insets(m));
        clip.setArcWidth(r);
        clip.setArcHeight(r);
        shadow.setVisible(!maximized);
        console.topBar.pseudoClassStateChanged(PseudoClass.getPseudoClass("maximized"), maximized);
        maximize.setGraphic(Icons.icon(maximized ? "restore" : "maximize", 14));
        maximize.getTooltip().setText(maximized ? I18n.t("window.restore") : I18n.t("window.maximize"));
    }
}
