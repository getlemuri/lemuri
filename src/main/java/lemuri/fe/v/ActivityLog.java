package lemuri.fe.v;

import javafx.application.Platform;
import javafx.beans.Observable;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.ReadOnlyObjectWrapper;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.collections.ListChangeListener;
import javafx.scene.control.Button;
import javafx.scene.control.TableCell;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableRow;
import javafx.scene.control.TableView;
import javafx.scene.control.ToggleButton;
import javafx.scene.control.ToggleGroup;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import lemuri.be.dto.SessionStatus;
import lemuri.be.terminal.LocalTransport;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Session;
import lemuri.fe.m.Task;
import lemuri.fe.v.terminal.Terminal;

/** Bottom panel: a shell on this computer, the activity log (tasks) and the open sessions. */
public final class ActivityLog extends VBox {
    public final BooleanProperty opened = new SimpleBooleanProperty(true);
    /** The local shell, started the first time its tab is chosen and again after it exits. */
    private Terminal local;
    /** The local shell while it is alive; null before it starts and after it exits. Ghost mode listens to it. */
    public final ReadOnlyObjectWrapper<Terminal> shell = new ReadOnlyObjectWrapper<>();
    private final StackPane body;
    private final ToggleButton sTerminal;
    private final ToggleButton sTask;

    public ActivityLog(AppContext app) {
        getStyleClass().add("log");

        TableView<Task> task = Grids.table(app.state.tasks, I18n.t("log.none"));
        task.getColumns().add(Grids.column(I18n.t("log.col.start"), 150, t -> Ui.dateTime(t.start)));
        task.getColumns().add(Grids.column(I18n.t("log.col.end"), 150, t -> t.end.get() == null ? "" : Ui.dateTime(t.end.get())));
        task.getColumns().add(Grids.column(I18n.t("common.host"), 150, t -> t.destination));
        TableColumn<Task, String> desc = Grids.column(I18n.t("log.col.description"), 360, t -> t.description);
        task.getColumns().add(desc);
        TableColumn<Task, Task> outcome = new TableColumn<>(I18n.t("common.status"));
        outcome.setPrefWidth(320);
        outcome.setCellValueFactory(c -> new ReadOnlyObjectWrapper<>(c.getValue()));
        outcome.setCellFactory(c -> new TableCell<>() {
            @Override
            protected void updateItem(Task t, boolean empty) {
                super.updateItem(t, empty);
                if (empty || t == null) {
                    setGraphic(null);
                    return;
                }
                setGraphic(switch (t.outcome.get()) {
                    case RUNNING -> Ui.state("info", I18n.t("log.running"));
                    case OK -> Ui.state("ok", I18n.t("log.ok"));
                    case ERROR -> Ui.state("error", t.message.get() != null ? t.message.get() : I18n.t("session.state.error"));
                });
            }
        });
        task.getColumns().add(outcome);
        task.setRowFactory(t -> new TableRow<>() {
            @Override
            protected void updateItem(Task x, boolean empty) {
                super.updateItem(x, empty);
                getStyleClass().remove("error-row");
                if (!empty && x != null && x.outcome.get() == Task.Outcome.ERROR) getStyleClass().add("error-row");
            }
        });
        // Outcomes change after insertion: redraw when a task finishes.
        app.state.tasks.addListener((ListChangeListener<Task>) c -> {
            while (c.next()) for (Task t : c.getAddedSubList()) t.outcome.addListener(o -> task.refresh());
        });

        TableView<Session> sessions = Grids.sessionsTable(app, app.state.sessions);
        app.state.sessions.addListener((ListChangeListener<Session>) c -> {
            while (c.next()) for (Session s : c.getAddedSubList()) s.state.addListener(o -> sessions.refresh());
        });
        sessions.setOnMouseClicked(e -> {
            Session s = sessions.getSelectionModel().getSelectedItem();
            if (e.getClickCount() == 2 && s != null) app.sessions.showSession(s);
        });

        body = new StackPane(task);
        body.getStyleClass().add("log-body");
        VBox.setVgrow(body, Priority.ALWAYS);

        ToggleGroup tabs = new ToggleGroup();
        sTerminal = tab(I18n.t("log.tab.terminal"), tabs);
        sTask = tab(I18n.t("log.tab.tasks"), tabs);
        ToggleButton sSessions = tab(I18n.t("log.tab.sessions", 0), tabs);
        app.state.sessions.addListener((Observable o) -> sSessions.setText(I18n.t("log.tab.sessions", app.state.sessions.size())));
        sTask.setSelected(true);
        tabs.selectedToggleProperty().addListener((o, before, now) -> {
            if (now == null) {
                before.setSelected(true);
                return;
            }
            if (now == sTerminal) {
                body.getChildren().setAll(localShell());
                local.focus();
            } else body.getChildren().setAll(now == sTask ? task : sessions);
        });

        Button clear = Ui.action("trash", I18n.t("log.clear"), () -> app.state.tasks.removeIf(t -> t.outcome.get() != Task.Outcome.RUNNING));
        clear.visibleProperty().bind(sTask.selectedProperty().and(opened));
        Button toggle = Ui.action("down", I18n.t("log.collapse"), () -> opened.set(!opened.get()));
        opened.addListener((o, before, now) -> {
            toggle.setGraphic(Icons.icon(now ? "down" : "up", 13));
            toggle.setTooltip(new Tooltip(now ? I18n.t("log.collapse") : I18n.t("log.open")));
            if (now) getChildren().add(body);
            else getChildren().remove(body);
        });

        // With the log closed, a click on a tab opens it again.
        sTerminal.setOnMouseClicked(e -> opened.set(true));
        sTask.setOnMouseClicked(e -> opened.set(true));
        sSessions.setOnMouseClicked(e -> opened.set(true));
        HBox head = new HBox(sTerminal, sTask, sSessions, Ui.space(), clear, toggle);
        head.getStyleClass().add("log-head");
        getChildren().addAll(head, body);
    }

    /** The local shell; when it exits (exit, Ctrl+D) the panel goes back to the tasks and the next click starts a new one. */
    private Terminal localShell() {
        if (local != null) return local;
        LocalTransport transport = new LocalTransport();
        Terminal t = new Terminal(transport);
        Actions.rightClick(t, () -> Actions.menuText(t));
        transport.onState((state, message) -> {
            if (state != SessionStatus.CLOSED) return;
            Platform.runLater(() -> {
                if (local != t) return;
                local = null;
                shell.set(null);
                if (body.getChildren().contains(t)) sTask.setSelected(true);
            });
        });
        local = t;
        shell.set(t);
        return t;
    }

    /** Gives the local shell to the ghost window (a node has only one parent), starting it if needed. */
    public Terminal lendShell() {
        return localShell();
    }

    /** Takes the local shell back from the ghost window, into its tab if that is the one chosen. */
    public void takeBackShell() {
        if (local != null && sTerminal.isSelected()) body.getChildren().setAll(local);
    }

    private static ToggleButton tab(String text, ToggleGroup g) {
        ToggleButton b = new ToggleButton(text);
        b.setToggleGroup(g);
        b.getStyleClass().add("log-tab");
        return b;
    }
}
