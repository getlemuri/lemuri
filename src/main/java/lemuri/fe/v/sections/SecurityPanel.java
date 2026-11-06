package lemuri.fe.v.sections;

import java.util.Comparator;
import java.util.List;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Label;
import javafx.scene.control.OverrunStyle;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Pane;
import javafx.scene.layout.Priority;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.scene.shape.Arc;
import javafx.scene.shape.ArcType;
import javafx.scene.shape.Circle;
import javafx.util.Duration;
import lemuri.be.dto.Security;
import lemuri.be.dto.Security.Check;
import lemuri.be.dto.Security.Level;
import lemuri.be.util.I18n;
import lemuri.fe.v.Icons;
import lemuri.fe.v.Ui;

/**
 * The SSH security checks of a host in three cards side by side: the connection, the key used to log in, the
 * server. Each card has its icon, its own score in a small ring and its checks, each with a switch that only
 * shows: on and green when safe, on and amber when it could be better, off and red when not safe. In each card
 * what is not right comes first, with what to do under it; why, on the mouse. Nothing is changed from here.
 */
public final class SecurityPanel extends VBox {
    /** Red, then amber, then green; inside each, what counts more in the score first. */
    private static final Comparator<Check> WORST_FIRST = Comparator.comparingInt((Check c) -> switch (c.level()) {
        case BAD -> 0;
        case WARN -> 1;
        case OK -> 2;
    }).thenComparing(Comparator.comparingInt(Check::weight).reversed());

    private record Area(String prefix, String icon, String title) {}

    private static final List<Area> AREAS = List.of(
            new Area("conn.", "lock", "security.area.connection"),
            new Area("client.", "key", "security.area.key"),
            new Area("server.", "server", "security.area.server"));

    private final HBox cards = new HBox(10);
    private final Label note = Ui.faint("");

    public SecurityPanel() {
        getStyleClass().add("security");
        setSpacing(8);
        note.setWrapText(true);
        note.setMinHeight(Region.USE_PREF_SIZE);
        note.getStyleClass().add("security-note");
        show(null, I18n.t("summary.updates.checking"));
    }

    /** The report, or null with the reason why there is none yet. */
    public void show(Security s, String waiting) {
        if (s == null || s.checks().isEmpty()) {
            getChildren().setAll(Ui.faint(s != null && s.error() != null ? s.error() : waiting));
            return;
        }
        cards.getChildren().clear();
        for (Area a : AREAS) {
            List<Check> mine = s.checks().stream().filter(c -> c.id().startsWith(a.prefix())).sorted(WORST_FIRST).toList();
            String empty = a.prefix().equals("server.") ? (s.error() != null ? s.error() : I18n.t("security.serverUnknown"))
                    : I18n.t("security.keyUnknown");
            Node card = card(a, mine, empty);
            HBox.setHgrow(card, Priority.ALWAYS);
            cards.getChildren().add(card);
        }
        String text = I18n.t("security.readOnly");
        if (s.checks().stream().anyMatch(c -> c.level() != Level.OK && c.fix() != null && c.id().startsWith("server.")))
            text += " " + I18n.t("security.fixHint");
        if (s.checks().stream().anyMatch(c -> c.level() != Level.OK)) text += " " + I18n.t("security.hover");
        note.setText(text);
        getChildren().setAll(cards, note);
    }

    private static Node card(Area a, List<Check> checks, String empty) {
        StackPane badge = new StackPane(Icons.icon(a.icon(), 18, "security-card-icon"));
        badge.getStyleClass().add("security-badge");
        badge.setMinSize(34, 34);
        badge.setMaxSize(34, 34);
        Label title = new Label(I18n.t(a.title()));
        title.getStyleClass().add("security-card-title");
        long toLook = checks.stream().filter(c -> c.level() != Level.OK).count();
        Label sub = Ui.faint(checks.isEmpty() ? "" : toLook == 0 ? I18n.t("security.allGood") : I18n.t("security.toLook", toLook));
        VBox names = new VBox(1, title, sub);
        Region space = new Region();
        HBox.setHgrow(space, Priority.ALWAYS);
        HBox head = new HBox(10, badge, names, space);
        head.setAlignment(Pos.CENTER_LEFT);
        Double score = new Security(checks, null, 0).score();
        if (score != null) head.getChildren().add(ring(score));

        VBox rows = new VBox(5);
        rows.getStyleClass().add("security-rows");
        if (checks.isEmpty()) {
            Label l = Ui.faint(empty);
            l.setWrapText(true);
            rows.getChildren().add(l);
        }
        for (Check c : checks) rows.getChildren().add(row(c));

        VBox card = new VBox(10, head, rows);
        card.getStyleClass().addAll("security-card", "security-card-" + tone(score));
        card.setPrefWidth(0);
        card.setMaxWidth(Double.MAX_VALUE);
        badge.getStyleClass().add("security-badge-" + tone(score));
        return card;
    }

    private static String tone(Double score) {
        return score == null ? "none" : score < 0.5 ? "bad" : score < 0.8 ? "warn" : "ok";
    }

    /** The score of a card: a small ring with the percentage inside, coloured like the large one. */
    private static Node ring(double score) {
        double r = 17, c = 21;
        Circle back = new Circle(c, c, r);
        back.getStyleClass().add("security-ring-bg");
        Arc value = new Arc(c, c, r, r, 90, -360 * Math.max(0, Math.min(1, score)));
        value.setType(ArcType.OPEN);
        value.getStyleClass().addAll("security-ring", "security-ring-" + tone(score));
        Label pct = new Label(Math.round(score * 100) + "%");
        pct.getStyleClass().add("security-ring-text");
        StackPane ring = new StackPane(new Pane(back, value), pct);
        ring.setMinSize(2 * c, 2 * c);
        ring.setMaxSize(2 * c, 2 * c);
        return ring;
    }

    private static Node row(Check c) {
        Label title = new Label(I18n.t("security." + c.id()));
        title.getStyleClass().add("security-title");
        title.setMinWidth(Region.USE_PREF_SIZE);
        Region space = new Region();
        space.setMinWidth(8);
        HBox.setHgrow(space, Priority.ALWAYS);
        HBox line = new HBox(8, indicator(c.level()), title, space);
        line.setAlignment(Pos.CENTER_LEFT);
        String shown = shown(c);
        if (!shown.isEmpty()) {
            Label v = Ui.code(shown);
            v.getStyleClass().add("faint");
            v.setTextOverrun(OverrunStyle.ELLIPSIS);
            v.setMinWidth(0);
            line.getChildren().add(v);
        }
        VBox box = new VBox(3, line);
        box.getStyleClass().add("security-row");
        String found = full(c);
        Tooltip.install(box, tip(c.level() == Level.OK
                ? I18n.t("security." + c.id()) + (found.isEmpty() ? "" : ": " + found)
                : I18n.t("security." + c.id() + ".advice") + (found.isEmpty() ? "" : "\n\n" + found)));
        if (c.level() != Level.OK && c.fix() != null) {
            // Under the switch's line, in from its left edge: what to write in sshd_config, or to run here.
            Label fix = Ui.code((c.id().startsWith("client.") ? "$ " : "→ ") + c.fix());
            fix.getStyleClass().addAll("security-fix", c.level() == Level.BAD ? "security-fix-bad" : "security-fix-warn");
            fix.setMinWidth(0);
            fix.setMaxWidth(Region.USE_PREF_SIZE);
            HBox in = new HBox(fix);
            in.getStyleClass().add("security-fix-row");
            box.getChildren().add(in);
        }
        return box;
    }

    /**
     * What was found, short enough for its line: without the name of the setting (the line says it), without
     * "@openssh.com" and the distribution's part of the version; the security updates with the words of the summary.
     */
    private static String shown(Check c) {
        String v = c.value();
        if (v == null) return "";
        if (c.id().equals("server.updates")) return I18n.t("summary.updates.security", v);
        if (c.id().equals("conn.version")) return v.split("\\s+")[0].replace('_', ' ');
        if (c.id().startsWith("server.") && v.matches("[A-Z][A-Za-z]+ \\S+")) return v.substring(v.indexOf(' ') + 1);
        return v.replace("@openssh.com", "");
    }

    /** All that was found, for the tooltip. */
    private static String full(Check c) {
        return c.value() == null ? "" : c.id().equals("server.updates") ? shown(c) : c.value();
    }

    private static Tooltip tip(String text) {
        Tooltip t = new Tooltip(text);
        t.setWrapText(true);
        t.setMaxWidth(420);
        t.setShowDelay(Duration.millis(250));
        t.setShowDuration(Duration.seconds(30));
        return t;
    }

    /** The switch: a track with a knob, on the right when safe or almost, on the left when not. Not clickable. */
    private static Node indicator(Level level) {
        Region track = new Region();
        track.getStyleClass().add("switch-track");
        Circle knob = new Circle(5);
        knob.getStyleClass().add("switch-knob");
        StackPane s = new StackPane(track, knob);
        s.getStyleClass().addAll("switch", switch (level) {
            case OK -> "switch-ok";
            case WARN -> "switch-warn";
            case BAD -> "switch-bad";
        });
        StackPane.setAlignment(knob, level == Level.BAD ? Pos.CENTER_LEFT : Pos.CENTER_RIGHT);
        s.setMinSize(26, 14);
        s.setPrefSize(26, 14);
        s.setMaxSize(26, 14);
        s.setMouseTransparent(true);
        s.setFocusTraversable(false);
        return s;
    }
}
