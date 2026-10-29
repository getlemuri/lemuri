package lemuri.fe.v.sections;

import java.util.HashSet;
import java.util.Set;
import javafx.beans.InvalidationListener;
import javafx.collections.MapChangeListener;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Label;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import lemuri.be.dto.Host;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Session;
import lemuri.fe.v.Icons;
import lemuri.fe.v.SessionViews;
import lemuri.fe.v.Ui;

/**
 * Tabbed SSH console: a single row with all open sessions, of any host, in
 * opening order. In view is the active session of the chosen host; clicking
 * the tab of another host brings the tree and the buttons there too. Tabs and
 * buttons are in a thin dark bar, fixed above the shell.
 */
public final class HostConsole extends VBox {
    private final AppContext app;
    private final Host host;
    /** Where the active tab is drawn: its terminal, or its parts after a split. */
    private final StackPane area = new StackPane();
    private final VBox screen = new VBox();
    private final InvalidationListener onChange = o -> refresh();
    private final MapChangeListener<String, Session> onActive = c -> refresh();
    /** Sessions whose state is being listened to, once each. */
    private final Set<Session> listened = new HashSet<>();

    public HostConsole(AppContext app, Host host) {
        this.app = app;
        this.host = host;
        getStyleClass().add("terminal");
        VBox.setVgrow(screen, Priority.ALWAYS);
        VBox.setVgrow(area, Priority.ALWAYS);
        area.getStyleClass().add("terminal-slot");

        // Listeners removed when the view leaves the scene, so as not to keep it alive.
        sceneProperty().addListener((o, before, now) -> {
            if (now != null) {
                app.state.sessions.addListener(onChange);
                app.state.activeSession.addListener(onActive);
                refresh();
            } else {
                app.state.sessions.removeListener(onChange);
                app.state.activeSession.removeListener(onActive);
                listened.forEach(s -> {
                    s.state.removeListener(onChange);
                    s.synced.removeListener(onChange);
                });
                listened.clear();
            }
        });
        refresh();
    }

    private void refresh() {
        var sessions = app.state.sessions;
        Session active = app.state.activeSession.get(host.id);
        if (sessions.isEmpty()) {
            getChildren().setAll(Ui.placeholders(
                    Icons.icon("terminal", 40, "faint-icon"),
                    strong(I18n.t("sessions.none")),
                    new HBox(4, Ui.faint(I18n.t("sessions.col.destination")), Ui.code(host.destination())),
                    Ui.primary(I18n.t("console.open"), "terminal", () -> app.sessions.openSession(host))));
            ((HBox) ((VBox) getChildren().get(0)).getChildren().get(2)).setAlignment(Pos.CENTER);
            return;
        }

        for (Session s : sessions) if (!listened.contains(s)) s.state.addListener(onChange);
        // Sync on or off changes the edges of the parts and their sync button: drawn again.
        for (Session s : sessions) if (listened.add(s)) s.synced.addListener(onChange);
        // Only the tabs: the commands of each host are on its terminal (see lemuri.fe.v.Actions#terminalButtons).
        TabBar bar = new TabBar(app, sessions, active, app.sessions::showSession,
                () -> app.sessions.openSession(host), I18n.t("console.newSessionOn", host.name));
        if (active != null) area.getChildren().setAll(SessionViews.tab(active));
        // This host has no sessions, the others do: the tabs stay, and in place of the shell an invitation to open one.
        VBox empty = active != null ? null : Ui.placeholders(Icons.icon("terminal", 40, "faint-icon"), strong(I18n.t("console.noSessionsOn", host.name)),
                Ui.primary(I18n.t("console.open"), "terminal", () -> app.sessions.openSession(host)));
        if (empty != null) VBox.setVgrow(empty, Priority.ALWAYS);
        Node body = active == null ? empty : area;
        screen.getChildren().setAll(bar, body);
        getChildren().setAll(screen);
    }

    private static Label strong(String t) {
        Label l = new Label(t);
        l.getStyleClass().add("strong");
        return l;
    }
}
