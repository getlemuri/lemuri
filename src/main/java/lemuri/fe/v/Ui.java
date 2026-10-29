package lemuri.fe.v;

import java.time.Instant;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.List;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.TextFormatter;
import javafx.scene.control.TextInputControl;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.ColumnConstraints;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.scene.shape.Rectangle;
import lemuri.be.dto.HostStatus;
import lemuri.be.dto.SessionStatus;
import lemuri.be.service.SharingService;
import lemuri.be.util.I18n;

/** Common building blocks, like components/ui in the web version: buttons, states, panels, counters. */
public final class Ui {
    private Ui() {}

    private static final DateTimeFormatter DATE_TIME = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")
            .withZone(ZoneId.systemDefault());

    public static String dateTime(long t) {
        return DATE_TIME.format(Instant.ofEpochMilli(t));
    }

    public static Button button(String text, String icon, Runnable action) {
        Button b = new Button(text, icon != null ? Icons.icon(icon) : null);
        b.getStyleClass().add("btn");
        b.setOnAction(e -> action.run());
        return b;
    }

    public static Button primary(String text, String icon, Runnable action) {
        Button b = button(text, icon, action);
        b.getStyleClass().add("btn-primary");
        return b;
    }

    public static Button danger(String text, String icon, Runnable action) {
        Button b = button(text, icon, action);
        b.getStyleClass().add("btn-danger");
        return b;
    }

    /** Icon-only button, like the actions on grid rows. */
    public static Button action(String icon, String title, Runnable action) {
        Button b = new Button(null, Icons.icon(icon, 13));
        b.getStyleClass().add("action");
        b.setTooltip(new Tooltip(title));
        b.setFocusTraversable(false);
        b.setOnAction(e -> action.run());
        return b;
    }

    /** Longest texts the input fields take; the same bounds as {@link lemuri.be.dto.Parcel#problem()} where both exist. */
    public static final int MAX_NAME = 64, MAX_FOLDER = 128, MAX_ADDRESS = 255, MAX_USER = 64, MAX_KEY = 1024, MAX_OS = 200,
            MAX_JUMP = 1024, MAX_PASSWORD = 4096, MAX_MESSAGE = 2000, MAX_SEARCH = 200, MAX_NOTE = 200, MAX_CARD = 2048;

    /**
     * Keeps the text of the field within {@code max} characters: typing stops there and a longer
     * paste is cut. Deleting is always allowed, so a longer text already in the field can be shortened.
     */
    public static <T extends TextInputControl> T limit(T field, int max) {
        field.setTextFormatter(new TextFormatter<String>(c -> {
            int room = max - (c.getControlText().length() - (c.getRangeEnd() - c.getRangeStart()));
            String added = c.getText();
            if (added.length() <= Math.max(room, 0)) return c;
            if (room <= 0) return null;
            // Do not cut a surrogate pair in half.
            int cut = Character.isHighSurrogate(added.charAt(room - 1)) ? room - 1 : room;
            c.setText(added.substring(0, cut));
            // The caret goes after what was kept, not after what was typed or pasted.
            c.selectRange(c.getRangeStart() + cut, c.getRangeStart() + cut);
            return c;
        }));
        return field;
    }

    /** Like {@link #space()}, but vertical: pushes what comes after to the bottom. */
    public static Region verticalSpace() {
        Region r = new Region();
        VBox.setVgrow(r, Priority.ALWAYS);
        return r;
    }

    public static Region space() {
        Region r = new Region();
        HBox.setHgrow(r, Priority.ALWAYS);
        return r;
    }

    /** Toolbar above the grids: buttons on the left, filters on the right. */
    public static HBox bar(Node... children) {
        HBox h = new HBox(6, children);
        h.getStyleClass().add("toolbar");
        h.setAlignment(Pos.CENTER_LEFT);
        return h;
    }

    /** Coloured label: ok, warn, error, info, neutral. */
    public static Label state(String tone, String text) {
        Label l = new Label(text);
        l.getStyleClass().addAll("status", "status-tone-" + tone);
        return l;
    }

    public static Label hostStatus(HostStatus s) {
        return switch (s) {
            case ONLINE -> state("ok", text(s));
            case OFFLINE -> state("error", text(s));
            case UNKNOWN -> state("neutral", text(s));
        };
    }

    public static String text(HostStatus s) {
        return switch (s) {
            case ONLINE -> I18n.t("host.state.online");
            case OFFLINE -> I18n.t("host.state.offline");
            case UNKNOWN -> I18n.t("host.state.unknown");
        };
    }

    public static Label sessionStatus(SessionStatus s) {
        return switch (s) {
            case CONNECTING -> state("info", text(s));
            case CONNECTED -> state("ok", text(s));
            case CLOSED -> state("neutral", text(s));
            case ERROR -> state("error", text(s));
        };
    }

    public static String text(SharingService.State s) {
        return switch (s) {
            case READY -> I18n.t("sharing.state.ready");
            case VAULT_LOCKED -> I18n.t("sharing.state.vaultLocked");
        };
    }

    public static String text(SessionStatus s) {
        return switch (s) {
            case CONNECTING -> I18n.t("session.state.connecting");
            case CONNECTED -> I18n.t("session.state.connected");
            case CLOSED -> I18n.t("session.state.closed");
            case ERROR -> I18n.t("session.state.error");
        };
    }

    public static String hostStatusClass(HostStatus s) {
        return switch (s) {
            case ONLINE -> "status-active";
            case OFFLINE -> "status-error";
            case UNKNOWN -> "status-idle";
        };
    }

    public static String sessionStatusClass(SessionStatus s) {
        return s == SessionStatus.CONNECTED ? "status-active" : s == SessionStatus.CONNECTING ? "status-idle" : "status-error";
    }

    /** Box with a grey title bar, like the panels of a host summary. */
    public static VBox panel(String title, Node body) {
        Label t = new Label(title);
        t.getStyleClass().add("panel-head");
        t.setMaxWidth(Double.MAX_VALUE);
        VBox c = new VBox(body);
        c.getStyleClass().add("panel-body");
        VBox.setVgrow(c, Priority.ALWAYS);
        VBox p = new VBox(t, c);
        p.getStyleClass().add("panel");
        return p;
    }

    /** As above, with an icon before the title. */
    public static VBox panel(String title, String icon, Node body) {
        VBox p = panel(title, body);
        Label t = (Label) p.getChildren().get(0);
        t.setGraphic(Icons.icon(icon, 14, "panel-icon"));
        t.setGraphicTextGap(7);
        return p;
    }

    /** Puts some buttons (for example "refresh") on the right of the panel header. */
    public static VBox withActions(VBox panel, Node... actions) {
        // Removed first: a node has only one parent, and putting it into the StackPane would remove it by itself,
        // moving the body to position 0.
        Node t = panel.getChildren().remove(0);
        HBox right = new HBox(2, actions);
        right.setAlignment(Pos.CENTER_RIGHT);
        right.setMaxWidth(Region.USE_PREF_SIZE);
        right.setPickOnBounds(false);
        StackPane head = new StackPane(t, right);
        StackPane.setAlignment(right, Pos.CENTER_RIGHT);
        StackPane.setMargin(right, new Insets(0, 6, 0, 0));
        panel.getChildren().add(0, head);
        return panel;
    }

    public record Counter(String label, String value, String tone) {}

    public static HBox counters(List<Counter> items) {
        HBox h = new HBox(8);
        for (Counter c : items) {
            Label v = new Label(c.value());
            v.getStyleClass().add("counter-value");
            Label e = new Label(c.label());
            e.getStyleClass().add("counter-caption");
            VBox b = new VBox(v, e);
            b.getStyleClass().add("counter");
            if (c.tone() != null) b.getStyleClass().add("counter-" + c.tone());
            HBox.setHgrow(b, Priority.ALWAYS);
            b.setMaxWidth(Double.MAX_VALUE);
            h.getChildren().add(b);
        }
        return h;
    }

    public record Detail(String label, Node value) {}

    /** Read-only label: value pairs. */
    public static GridPane details(List<Detail> items) {
        GridPane g = new GridPane();
        g.getStyleClass().add("details");
        int r = 0;
        for (Detail d : items) {
            Label e = new Label(d.label());
            e.getStyleClass().add("details-caption");
            Node v = d.value() != null ? d.value() : faint(I18n.t("common.notSet"));
            g.addRow(r++, e, v);
            e.getStyleClass().add("details-row");
            v.getStyleClass().add("details-row");
        }
        g.getColumnConstraints().addAll(new ColumnConstraints(150), new ColumnConstraints());
        return g;
    }

    public static Label faint(String text) {
        Label l = new Label(text);
        l.getStyleClass().add("faint");
        return l;
    }

    public static Label code(String text) {
        Label l = new Label(text);
        l.getStyleClass().add("code");
        return l;
    }

    /**
     * Rounds the corners of a container, content included: a background radius alone does not stop a
     * terminal or a tree from drawing into the corners. Same radius as the CSS of the main containers.
     */
    public static void rounded(Region r, double radius) {
        Rectangle clip = new Rectangle();
        clip.setArcWidth(radius * 2);
        clip.setArcHeight(radius * 2);
        clip.widthProperty().bind(r.widthProperty());
        clip.heightProperty().bind(r.heightProperty());
        r.setClip(clip);
    }

    /** Centred message in place of an empty content. */
    public static VBox placeholders(Node... children) {
        VBox v = new VBox(8, children);
        v.setAlignment(Pos.CENTER);
        v.getStyleClass().add("empty-state");
        VBox.setVgrow(v, Priority.ALWAYS);
        return v;
    }
}
