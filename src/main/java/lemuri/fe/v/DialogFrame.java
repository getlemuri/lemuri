package lemuri.fe.v;

import javafx.geometry.Pos;
import javafx.scene.Cursor;
import javafx.scene.Node;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.Tooltip;
import javafx.scene.input.MouseButton;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.StackPane;
import javafx.scene.paint.Color;
import javafx.stage.Stage;
import javafx.stage.StageStyle;
import javafx.stage.Window;
import lemuri.be.util.I18n;

/**
 * The main window frame on dialogs too: no system bar, in its place a grey bar
 * with the title and the X, rounded corners and shadow (the stage is transparent and the
 * dialog pane leaves the margin for the shadow around it, see .framed in style.css). It is
 * moved by dragging the bar; resizable ones grow from the edges.
 */
final class DialogFrame {
    /** The transparent margin around the dialog: equal to -fx-padding of .framed. */
    static final double SHADOW = 12;
    private static final double BORDER = 6;

    private final Dialog<?> dialog;
    private double dragX, dragY, x0, y0, l0, a0;
    private Cursor border = Cursor.DEFAULT;

    private DialogFrame(Dialog<?> dialog) {
        this.dialog = dialog;
    }

    /** Installs the frame: call it before showing the dialog. */
    static void install(Dialog<?> d, String title) {
        new DialogFrame(d).mount(title);
    }

    private void mount(String title) {
        dialog.initStyle(StageStyle.TRANSPARENT);
        DialogPane p = dialog.getDialogPane();
        p.getStyleClass().add("framed");

        Label t = new Label(title);
        t.getStyleClass().add("window-bar-title");
        t.setMaxWidth(Double.MAX_VALUE);
        Button x = new Button(null, Icons.icon("close", 14));
        x.getStyleClass().addAll("window-control", "close-control");
        x.setFocusTraversable(false);
        x.setTooltip(new Tooltip(I18n.t("window.close")));
        x.setOnAction(e -> close());
        // The app icon on the left, the X on the right, the title in the middle of the whole bar.
        StackPane middle = Dialogs.centredTitle(t);
        middle.getChildren().add(x);
        StackPane.setAlignment(x, Pos.CENTER_RIGHT);
        HBox.setHgrow(middle, Priority.ALWAYS);
        HBox bar = new HBox(middle);
        bar.setAlignment(Pos.CENTER_LEFT);
        bar.getStyleClass().add("window-bar");
        p.setHeader(bar);
        drag(bar);

        // Without a transparent scene the rounded corners would sit on a white background.
        if (p.getScene() != null) prepare(p.getScene());
        p.sceneProperty().addListener((o, before, now) -> {
            if (now != null) prepare(now);
        });
    }

    private void prepare(Scene s) {
        s.setFill(Color.TRANSPARENT);
        s.addEventFilter(MouseEvent.MOUSE_MOVED, e -> {
            border = dialog.isResizable() ? cursor(e.getSceneX(), e.getSceneY(), s) : Cursor.DEFAULT;
            s.setCursor(border);
        });
        s.addEventFilter(MouseEvent.MOUSE_PRESSED, e -> {
            if (border == Cursor.DEFAULT || e.getButton() != MouseButton.PRIMARY) return;
            parts(e);
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
    }

    /**
     * The X does what Esc would do: the cancel button, or the only button there is
     * (a notice with only OK). So whoever opened the dialog gets the usual answer.
     */
    private void close() {
        DialogPane p = dialog.getDialogPane();
        ButtonType single = p.getButtonTypes().size() == 1 ? p.getButtonTypes().get(0) : null;
        for (ButtonType b : p.getButtonTypes()) {
            ButtonBar.ButtonData d = b.getButtonData();
            if (d == ButtonBar.ButtonData.CANCEL_CLOSE || d.isCancelButton()) {
                ((Button) p.lookupButton(b)).fire();
                return;
            }
        }
        if (single != null) ((Button) p.lookupButton(single)).fire();
        else dialog.close();
    }

    private Window window() {
        return dialog.getDialogPane().getScene().getWindow();
    }

    private void parts(MouseEvent e) {
        Window w = window();
        dragX = e.getScreenX();
        dragY = e.getScreenY();
        x0 = w.getX();
        y0 = w.getY();
        l0 = w.getWidth();
        a0 = w.getHeight();
    }

    private void drag(Node bar) {
        bar.setOnMousePressed(e -> {
            if (e.getButton() == MouseButton.PRIMARY && border == Cursor.DEFAULT) parts(e);
        });
        bar.setOnMouseDragged(e -> {
            if (e.getButton() != MouseButton.PRIMARY || border != Cursor.DEFAULT) return;
            Window w = window();
            w.setX(x0 + e.getScreenX() - dragX);
            w.setY(y0 + e.getScreenY() - dragY);
        });
    }

    private static Cursor cursor(double x, double y, Scene s) {
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
        Stage w = (Stage) window();
        double minL = Math.max(w.getMinWidth(), 200), minA = Math.max(w.getMinHeight(), 150);
        double dx = e.getScreenX() - dragX, dy = e.getScreenY() - dragY;
        double x = x0, y = y0, l = l0, a = a0;
        if (border == Cursor.E_RESIZE || border == Cursor.NE_RESIZE || border == Cursor.SE_RESIZE) l = l0 + dx;
        if (border == Cursor.S_RESIZE || border == Cursor.SW_RESIZE || border == Cursor.SE_RESIZE) a = a0 + dy;
        if (border == Cursor.W_RESIZE || border == Cursor.NW_RESIZE || border == Cursor.SW_RESIZE) {
            l = Math.max(minL, l0 - dx);
            x = x0 + l0 - l;
        }
        if (border == Cursor.N_RESIZE || border == Cursor.NW_RESIZE || border == Cursor.NE_RESIZE) {
            a = Math.max(minA, a0 - dy);
            y = y0 + a0 - a;
        }
        w.setX(x);
        w.setY(y);
        w.setWidth(Math.max(minL, l));
        w.setHeight(Math.max(minA, a));
    }
}
