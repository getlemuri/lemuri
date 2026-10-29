package lemuri.fe.v.sections;

import java.util.List;
import java.util.function.Consumer;
import javafx.application.Platform;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.OverrunStyle;
import javafx.scene.control.ScrollPane;
import javafx.scene.control.Tooltip;
import javafx.scene.input.ScrollEvent;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.Region;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Session;
import lemuri.fe.v.Icons;
import lemuri.fe.v.Ui;

/**
 * Row of the SSH session tabs above the console, of all hosts. The tabs scroll
 * horizontally when they do not fit; on the right the commands of the console.
 */
public final class TabBar extends HBox {
    /** Widest a tab title is drawn, in pixels. */
    private static final double TITLE_WIDTH = 180;

    private final HBox right = new HBox(2);

    /**
     * @param active  the highlighted tab (may be null)
     * @param choose  click on a tab
     * @param onNew   the "+" at the end of the tabs, null to hide it
     */
    public TabBar(AppContext app, List<Session> all, Session active,
            Consumer<Session> choose, Runnable onNew, String newTitle) {
        super(6);
        // The parts of a split tab are drawn inside it, not as tabs of their own.
        List<Session> sessions = all.stream().filter(s -> s.owner() == null).toList();
        getStyleClass().add("overlay-bar");
        setAlignment(Pos.CENTER_LEFT);

        HBox tabs = new HBox(2);
        // The tabs stand on the bottom edge of the bar, on its line.
        tabs.setAlignment(Pos.BOTTOM_LEFT);
        for (Session s : sessions) {
            // A long title ends in "…" so one tab does not take the whole row; the tooltip has it whole.
            Label title = new Label(s.title);
            title.setMaxWidth(TITLE_WIDTH);
            title.setTextOverrun(OverrunStyle.ELLIPSIS);
            HBox tab = new HBox(5, Icons.icon("terminal", 13, Ui.sessionStatusClass(s.state.get())), title,
                    Ui.action("close", I18n.t("sessions.close"), () -> app.sessions.closeSession(s)));
            tab.setAlignment(Pos.CENTER_LEFT);
            tab.setMinWidth(Region.USE_PREF_SIZE);
            tab.getStyleClass().add("terminal-tab");
            if (s == active) tab.getStyleClass().add("active");
            Tooltip.install(tab, new Tooltip(s.title + "\n" + s.host.destination() + " · " + Ui.text(s.state.get())));
            tab.setOnMouseClicked(e -> choose.accept(s));
            tabs.getChildren().add(tab);
        }
        if (onNew != null) {
            Button plus = Ui.action("plus", newTitle, onNew);
            // Centred on the tabs, not sitting on the line like them.
            HBox.setMargin(plus, new Insets(0, 0, 1, 0));
            tabs.getChildren().add(plus);
        }

        // The tabs scroll horizontally and the active tab stays in view; the commands on the right do not shrink.
        ScrollPane scroll = new ScrollPane(tabs);
        scroll.getStyleClass().add("tabs-scroll");
        scroll.setVbarPolicy(ScrollPane.ScrollBarPolicy.NEVER);
        scroll.setHbarPolicy(ScrollPane.ScrollBarPolicy.NEVER);
        scroll.setFitToHeight(true);
        scroll.setMinWidth(0);
        // As high as the tabs: by default a scroll pane keeps room for its (hidden) scroll bars, almost twice as much.
        scroll.setMinHeight(Region.USE_PREF_SIZE);
        HBox.setHgrow(scroll, Priority.ALWAYS);
        scroll.addEventFilter(ScrollEvent.SCROLL, e -> {
            double width = tabs.getWidth() - scroll.getViewportBounds().getWidth();
            if (width > 0) scroll.setHvalue(scroll.getHvalue() - (e.getDeltaY() + e.getDeltaX()) / width);
            e.consume();
        });
        right.setAlignment(Pos.CENTER_RIGHT);
        right.setMinWidth(Region.USE_PREF_SIZE);
        // The bar has padding only at the top (the tabs stand on its bottom line): the same below, so the commands sit in the middle.
        HBox.setMargin(right, new Insets(0, 0, 2, 0));
        getChildren().addAll(scroll, right);

        int index = sessions.indexOf(active);
        if (index >= 0) Platform.runLater(() -> {
            // Brings the active tab into view when the row is wider than the bar.
            double width = tabs.getWidth() - scroll.getViewportBounds().getWidth();
            if (width <= 0) return;
            Node n = tabs.getChildren().get(index);
            double x = n.getBoundsInParent().getMinX(), w = n.getBoundsInParent().getWidth();
            double view = scroll.getViewportBounds().getWidth(), from = scroll.getHvalue() * width;
            if (x < from) scroll.setHvalue(x / width);
            else if (x + w > from + view) scroll.setHvalue((x + w - view) / width);
        });
    }

    /** The commands on the right of the tabs. */
    public void addToRight(Node... n) {
        right.getChildren().addAll(n);
    }
}
