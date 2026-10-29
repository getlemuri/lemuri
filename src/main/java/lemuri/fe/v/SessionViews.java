package lemuri.fe.v;

import java.util.HashMap;
import java.util.Map;
import javafx.beans.value.ChangeListener;
import javafx.beans.value.WeakChangeListener;
import javafx.collections.ListChangeListener;
import javafx.concurrent.Worker;
import javafx.css.PseudoClass;
import javafx.geometry.Insets;
import javafx.geometry.Orientation;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Label;
import javafx.scene.control.ProgressIndicator;
import javafx.scene.control.SplitPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.scene.web.WebView;
import lemuri.be.dto.SessionStatus;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Pane;
import lemuri.fe.m.Session;
import lemuri.fe.v.sections.Slot;
import lemuri.fe.v.terminal.Terminal;

/**
 * The views that live as long as a session: the terminal, which survives the
 * console being rebuilt without losing its screen, and the file panel. The
 * terminal is born with the session, the file panel the first time it is opened;
 * they are thrown away when the session closes. From the JavaFX thread.
 */
public final class SessionViews {
    private SessionViews() {}

    private static AppContext app;
    private static final PseudoClass SYNCED = PseudoClass.getPseudoClass("synced");
    private static final Map<String, Terminal> terminals = new HashMap<>();
    /** tab id -> the part of the tab that last had the keyboard: it gets it back when the view is rebuilt. */
    private static final Map<String, Session> typing = new HashMap<>();

    /** Follows the open sessions: a terminal for each new one, and its views dropped when it closes. Once, at startup. */
    public static void attach(AppContext a) {
        app = a;
        a.state.sessions.addListener((ListChangeListener<Session>) c -> {
            while (c.next()) {
                // Right away, even if the session is not in view: it is the ready terminal that starts the connection.
                for (Session s : c.getAddedSubList()) terminal(s);
                for (Session s : c.getRemoved()) {
                    terminals.remove(s.id);
                    typing.remove(s.id);
                    typing.values().removeIf(x -> x == s);
                }
            }
        });
    }

    public static Terminal terminal(Session s) {
        return terminals.computeIfAbsent(s.id, k -> {
            Terminal t = new Terminal(s.transport);
            // On the terminal itself, so a split tab knows which part was clicked: wherever the terminal is shown.
            Actions.rightClick(t, () -> Actions.menuConsole(app, s));
            Transfers.allowUpload(t, () -> s);
            t.onInput(d -> type(s, d));
            t.typing().addListener((o, before, now) -> {
                if (now) typing.put(s.tab().id, s);
            });
            return t;
        });
    }

    /** What is typed in {@code s}: to its shell, or to every part of its tab if the tab is synced. */
    private static void type(Session s, String data) {
        Session tab = s.tab();
        if (!tab.synced.get()) {
            s.transport.send(data);
            return;
        }
        for (Session p : tab.panes()) p.transport.send(data);
    }

    /**
     * What a tab shows: its terminal, or its parts side by side and one above the other, with separators
     * that can be dragged. The part that last had the keyboard gets it back.
     */
    public static Node tab(Session tab) {
        Session focus = typing.get(tab.id);
        if (focus == null || !tab.panes().contains(focus)) focus = tab;
        // Synced: every part has a coloured edge, so it is clear that typing goes everywhere.
        boolean synced = tab.synced.get() && tab.panes().size() > 1;
        Node n = tab.layout() == null ? slot(tab, false) : draw(tab.layout(), synced);
        for (Session p : tab.panes()) if (p != focus) terminal(p).redraw();
        terminal(focus).focus();
        return n;
    }

    private static Node draw(Pane p, boolean synced) {
        if (!p.divided()) return slot(p.session(), synced);
        SplitPane split = new SplitPane(draw(p.first(), synced), draw(p.second(), synced));
        split.getStyleClass().add("terminal-split");
        split.setOrientation(p.sideBySide() ? Orientation.HORIZONTAL : Orientation.VERTICAL);
        split.setDividerPositions(p.divider());
        split.getDividers().get(0).positionProperty().addListener((o, before, now) -> p.divider(now.doubleValue()));
        return split;
    }

    private static Node slot(Session s, boolean synced) {
        Slot slot = new Slot();
        slot.pseudoClassStateChanged(SYNCED, synced);
        // Over the terminal, against its top right corner: the commands of this part's host on a much darker patch,
        // just as wide as the icons, as if the bar above went on there. The rest of the row shows the terminal, and
        // the scroll bar on the right stays free.
        HBox bar = new HBox(2);
        bar.getChildren().setAll(Actions.terminalButtons(app, s));
        bar.getStyleClass().add("terminal-actions");
        bar.setMaxSize(Region.USE_PREF_SIZE, Region.USE_PREF_SIZE);
        StackPane.setAlignment(bar, Pos.TOP_RIGHT);
        Terminal t = terminal(s);
        StackPane.setMargin(bar, new Insets(0, t.scrollBarWidth(), 0, 0));
        t.ghostShown().addListener(new WeakChangeListener<>(keepOffScrollBar(bar, t)));
        slot.getChildren().setAll(t, connecting(s), bar);
        return slot;
    }

    /** Moves the bar away from the scroll bar when its width changes; kept by the bar, so the listener lives as long. */
    private static ChangeListener<Boolean> keepOffScrollBar(HBox bar, Terminal t) {
        ChangeListener<Boolean> l = (o, before, now) -> StackPane.setMargin(bar, new Insets(0, t.scrollBarWidth(), 0, 0));
        bar.getProperties().put(ChangeListener.class, l);
        return l;
    }

    /**
     * Over the terminal while the session connects: a spinner with the host, so the wait (a slow server, a jump
     * host, a password asked) is visible. It goes away when the shell is there, or on an error, which the terminal shows.
     */
    private static Node connecting(Session s) {
        ProgressIndicator spinner = new ProgressIndicator();
        spinner.getStyleClass().add("connecting-spinner");
        Label what = new Label(I18n.t("session.connectingTo", s.host.name));
        what.getStyleClass().add("connecting-title");
        Label where = new Label(s.host.destination());
        where.getStyleClass().add("connecting-where");
        VBox box = new VBox(10, spinner, what, where);
        box.setAlignment(Pos.CENTER);
        box.getStyleClass().add("connecting");
        box.visibleProperty().bind(s.state.isEqualTo(SessionStatus.CONNECTING));
        box.managedProperty().bind(box.visibleProperty());
        return box;
    }

    /**
     * The first web view of a run costs about two seconds on the JavaFX thread (WebKit starts up): done at
     * startup, behind the last frame of the splash, the first terminal opens at once instead of freezing the window.
     */
    public static void warmUp() {
        warm = new WebView();
        // Kept until the page has loaded, then left to the garbage collector.
        warm.getEngine().getLoadWorker().stateProperty().addListener((o, before, now) -> {
            if (now == Worker.State.SUCCEEDED || now == Worker.State.FAILED) warm = null;
        });
        warm.getEngine().load(Terminal.class.getResource("/lemuri/terminal/terminal.html").toExternalForm());
    }

    private static WebView warm;

}
