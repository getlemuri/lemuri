package lemuri.fe.v;

import java.util.List;
import java.util.prefs.Preferences;
import javafx.application.Platform;
import javafx.beans.Observable;
import javafx.beans.binding.Bindings;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.collections.ListChangeListener;
import javafx.event.ActionEvent;
import javafx.geometry.Orientation;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.ScrollPane;
import javafx.scene.control.SplitPane;
import javafx.scene.control.ToggleButton;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.BorderPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import lemuri.App;
import lemuri.be.dto.Host;
import lemuri.be.service.SharingService;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Session;
import lemuri.fe.v.sections.Contacts;
import lemuri.fe.v.sections.HostConsole;
import lemuri.fe.v.sections.HostList;
import lemuri.fe.v.sections.HostSummary;
import lemuri.fe.v.terminal.Terminal;

/**
 * Main window: top bar, activity bar on the left, host tree, central area with
 * the content of the node, log and status bar at the bottom.
 */
public class Console extends BorderPane {
    // No side menu: the node chosen on the left decides the content, which takes the whole width.
    // The only alternative view is the console of a host, opened by the buttons.

    private final AppContext app;
    private final Label title = new Label();
    private final HBox areaActions = new HBox(4);
    private final StackPane content = new StackPane();
    /** Top bar: with the custom frame it is also the title bar (see {@link WindowFrame}). */
    public final HBox topBar;

    private final Label sessionsStatus = new Label();
    /** The status bar: lent to the ghost window while ghost mode is on (a node has only one parent). */
    private HBox status;
    /** The bottom panel: its local shell can be lent to the ghost window. */
    private ActivityLog log;
    /** The ghost window this console can turn into (see {@link GhostMode}). */
    private final GhostMode ghost;

    public Console(AppContext app) {
        this.app = app;
        this.ghost = new GhostMode(app, this);
        // "root" even when the console is not the root of the scene: the palette is defined there.
        // "layout": the style.css rules for activity bar, host column and status bar.
        getStyleClass().addAll("root", "console", "layout");

        // ── Top bar ─────
        Label name = new Label("Lemuri");
        name.getStyleClass().add("brand-name");
        Label version = new Label(App.VERSION);
        version.getStyleClass().add("brand-version");
        // The icon sits in a box as wide as the activity bar (see .brand-box), right above it.
        Region box = AppIcon.tile();
        box.getStyleClass().add("brand-box");
        HBox brand = new HBox(6, box, name, version);
        brand.setAlignment(Pos.CENTER_LEFT);
        // The icon box goes from the top to the bottom of the bar.
        brand.setMaxHeight(Double.MAX_VALUE);

        // Theme as chosen in the settings (SettingsDialog), remembered between runs.
        Preferences pref = Preferences.userRoot().node("lemuri/ui");
        theme(pref.getBoolean("darkTheme", false));
        // The commands are in the activity bar, in the host header and in the status bar:
        // at the top only the brand remains and, with the custom frame, the window controls.
        HBox high = new HBox(14, brand, Ui.space());
        topBar = high;
        // The brand is not truncated when space is short.
        brand.setMinWidth(Region.USE_PREF_SIZE);
        high.getStyleClass().add("top-bar");
        high.setAlignment(Pos.CENTER_LEFT);
        setTop(high);

        // ── Resource column ──
        // Whether the host column is open: from the activity bar, and from the last icon of its own header.
        BooleanProperty openResources = new SimpleBooleanProperty();
        HostTree tree = new HostTree(app);
        // Header: only the icon commands, on the right; the first item of the tree says "Hosts".
        // The column opens and closes from the activity bar.
        HBox hostHead = new HBox(2, Ui.space(),
                Ui.action("plus", I18n.t("hosts.add"), () -> HostDialog.forNew(app, currentFolder())),
                Ui.action("folder", I18n.t("folders.new"), () -> Actions.newFolder(app, currentFolder())),
                Ui.action("expand", I18n.t("tree.expandAll"), () -> tree.expandAll(true)),
                Ui.action("collapse", I18n.t("tree.collapseAll"), () -> tree.expandAll(false)),
                Ui.action("sidebar-close", I18n.t("tree.hide"), () -> openResources.set(false)));
        hostHead.setAlignment(Pos.CENTER_LEFT);
        hostHead.getStyleClass().add("host-column-head");
        VBox.setVgrow(tree, Priority.ALWAYS);
        VBox resources = new VBox(hostHead, tree);
        resources.getStyleClass().add("column-resources");

        // ── Central area ───
        title.getStyleClass().add("area-title");
        title.setMaxWidth(Double.MAX_VALUE);
        HBox.setHgrow(title, Priority.ALWAYS);
        HBox areaHead = new HBox(8, title, areaActions);
        areaHead.getStyleClass().add("area-head");
        areaHead.setAlignment(Pos.CENTER_LEFT);
        // The title row exists only when it carries buttons (hosts and folders): elsewhere it is space gained.
        areaHead.visibleProperty().bind(Bindings.isNotEmpty(areaActions.getChildren()));
        areaHead.managedProperty().bind(areaHead.visibleProperty());
        content.getStyleClass().add("contents");
        HBox.setHgrow(content, Priority.ALWAYS);
        VBox.setVgrow(content, Priority.ALWAYS);
        VBox center = new VBox(areaHead, content);
        center.getStyleClass().add("area");

        // ── Log at the bottom ──
        log = new ActivityLog(app);
        SplitPane vertical = new SplitPane();
        SplitPane horizontal = new SplitPane(resources, center);
        for (Region r : List.of(resources, center, log)) Ui.rounded(r, 5);
        horizontal.setDividerPositions(0.2);
        resources.setMinWidth(220);
        SplitPane.setResizableWithParent(resources, false);
        vertical.setOrientation(Orientation.VERTICAL);
        vertical.getItems().addAll(horizontal, log);
        vertical.setDividerPositions(0.78);
        SplitPane.setResizableWithParent(log, false);
        vertical.getStyleClass().add("main-split");
        horizontal.getStyleClass().add("main-split");
        log.opened.addListener((o, before, now) -> vertical.setDividerPositions(now ? 0.78 : 0.97));
        // The choice is remembered between runs, together with the width of the open column.
        // When closed, the column disappears completely: the activity bar reopens it.
        double[] width = {0.2};
        Runnable layoutResources = () -> {
            boolean yes = openResources.get();
            Node first = horizontal.getItems().get(0);
            if (first == resources && horizontal.getItems().size() > 1) width[0] = horizontal.getDividerPositions()[0];
            if (first != center) horizontal.getItems().remove(0);
            if (yes) {
                horizontal.getItems().add(0, resources);
                horizontal.setDividerPositions(width[0]);
            }
        };
        openResources.addListener((o, before, now) -> {
            layoutResources.run();
            pref.putBoolean("hostColumn", now);
        });
        openResources.set(pref.getBoolean("hostColumn", true));
        layoutResources.run();
        // At startup tasks and sessions stay closed: the space goes to the terminals.
        log.opened.set(false);

        // ── Activity bar on the left and status bar at the bottom ──
        VBox activity = activityBar(openResources, log);
        HBox.setHgrow(vertical, Priority.ALWAYS);
        setCenter(new HBox(activity, vertical));
        status = statusBar();
        setBottom(status);

        // ── Reactions to state ───
        app.state.position.addListener(o -> show());
        app.state.hosts.addListener((Observable o) -> show());
        app.state.createdFolders.addListener((Observable o) -> show());
        app.state.sessions.addListener((ListChangeListener<Session>) c -> {
            while (c.next()) for (Session s : c.getAddedSubList()) s.state.addListener(x -> refreshIndicators());
            refreshIndicators();
        });
        show();
    }

    /**
     * Activity bar: Hosts opens and closes the column (or brings you there, if you are elsewhere),
     * then Console and Team; at the bottom the log
     * and the settings. The item of the current view is marked with the line on the left.
     */
    private VBox activityBar(BooleanProperty openResources, ActivityLog log) {
        ToggleButton host = activity("server", I18n.t("activity.hosts"));
        ToggleButton console = activity("terminal", I18n.t("hosts.console"));
        ToggleButton contacts = activity("share", I18n.t("team.title"));
        // Ghost mode needs a terminal to show: like the console button, it is off without sessions.
        ToggleButton ghostButton = activity("ghost", I18n.t("activity.ghost"));
        ghostButton.disableProperty().bind(Bindings.isEmpty(app.state.sessions));
        ghostButton.setOnAction(e -> {
            ghostButton.setSelected(false);
            ghost.enter();
        });
        ToggleButton logB = activity("list", I18n.t("activity.log"));
        ToggleButton menu = activity("gear", I18n.t("activity.settings"));
        Label arrivals = new Label();
        arrivals.getStyleClass().add("activity-number");
        arrivals.setMouseTransparent(true);
        StackPane contactsBox = new StackPane(contacts, arrivals);
        StackPane.setAlignment(arrivals, Pos.BOTTOM_RIGHT);

        host.setOnAction(e -> openResources.set(!openResources.get()));
        // Without open sessions there is no console to show: the button is off, with no hover and no hand cursor.
        console.disableProperty().bind(Bindings.isEmpty(app.state.sessions));
        console.setOnAction(e -> {
            Session s = app.state.sessions.stream().reduce((a, b) -> b).orElse(null);
            if (app.state.node() instanceof Selection.HostItem d && app.state.activeSession.get(d.hostId()) != null) s = app.state.activeSession.get(d.hostId());
            if (s != null) app.sessions.showSession(s);
            else if (app.state.node() instanceof Selection.HostItem d) app.state.select(d, "console");
        });
        contacts.setOnAction(e -> app.state.select(new Selection.Contacts(), null));
        logB.setOnAction(e -> log.opened.set(!log.opened.get()));
        menu.setOnAction(e -> SettingsDialog.open(app, this));

        Runnable refresh = () -> {
            String item = app.state.position.get().item();
            host.setSelected(openResources.get());
            console.setSelected("console".equals(item));
            contacts.setSelected(app.state.node() instanceof Selection.Contacts);
            logB.setSelected(log.opened.get());
            menu.setSelected(false);
            int n = app.sharing.incoming.size();
            arrivals.setText(String.valueOf(n));
            arrivals.setVisible(n > 0);
        };
        for (ToggleButton t : List.of(host, console, contacts, logB, menu)) t.addEventFilter(ActionEvent.ACTION, e -> Platform.runLater(refresh));
        app.state.position.addListener(o -> refresh.run());
        openResources.addListener(o -> refresh.run());
        log.opened.addListener(o -> refresh.run());
        app.sharing.incoming.addListener((Observable o) -> refresh.run());
        refresh.run();

        VBox bar = new VBox(host, console, contactsBox, ghostButton, Ui.verticalSpace(), logB, menu);
        bar.getStyleClass().add("activity-bar");
        return bar;
    }

    private static ToggleButton activity(String icon, String title) {
        ToggleButton b = new ToggleButton(null, Icons.icon(icon, 22));
        b.getStyleClass().add("activity");
        b.setTooltip(new Tooltip(title));
        b.setFocusTraversable(false);
        return b;
    }

    /**
     * Status bar, every item with its icon: open sessions, sharing state; on the right the
     * upload or download in progress, the vault (click to unlock or lock it) and the version.
     */
    private HBox statusBar() {
        sessionsStatus.getStyleClass().add("status-item");
        Label contacts = new Label();
        contacts.getStyleClass().add("status-item");
        contacts.setOnMouseClicked(e -> app.state.select(new Selection.Contacts(), null));
        Runnable onContacts = () -> {
            var st = app.sharing.status.get();
            contacts.setText(I18n.t("status.team", Ui.text(st)));
            contacts.setGraphic(Icons.icon("group", 13, st == SharingService.State.READY ? "color-ok" : "color-team"));
            contacts.setTooltip(new Tooltip(app.sharing.message.get()));
        };
        app.sharing.status.addListener(o -> onContacts.run());
        onContacts.run();
        Label vault = new Label();
        vault.getStyleClass().add("status-item");
        Runnable onVault = () -> {
            boolean isOpen = app.vault.isOpen();
            vault.setText(isOpen ? I18n.t("status.vault.open") : I18n.t("status.vault.locked"));
            vault.setGraphic(Icons.icon(isOpen ? "lock-open" : "lock", 13, isOpen ? "color-ok" : "color-vault"));
            vault.setTooltip(new Tooltip(isOpen ? I18n.t("status.vault.lockTip") : I18n.t("status.vault.unlockTip")));
        };
        vault.setOnMouseClicked(e -> {
            if (app.vault.isOpen()) app.vault.lock();
            else Dialogs.openVault(app.vault);
            onVault.run();
        });
        // Locked or unlocked from anywhere (the settings, a backup import): the status bar follows.
        app.vault.onUnlock(() -> Platform.runLater(onVault));
        app.vault.onLock(() -> Platform.runLater(onVault));
        onVault.run();
        Label version = new Label("Lemuri " + App.VERSION, AppIcon.view(14));
        version.getStyleClass().add("status-item");
        HBox bar = new HBox(sessionsStatus, contacts, Ui.space(), Transfers.indicator(), vault, version);
        bar.getStyleClass().add("status-bar");
        bar.setAlignment(Pos.CENTER_LEFT);
        return bar;
    }

    /** Applies the theme to the console and to the windows that will open. */
    public void theme(boolean dark) {
        Dialogs.dark = dark;
        Terminal.theme(dark);
        cssClass("dark", dark);
    }

    private void cssClass(String c, boolean active) {
        getStyleClass().remove(c);
        if (active) getStyleClass().add(c);
    }

    private void refreshIndicators() {
        long n = app.state.connected(null);
        sessionsStatus.setText(n == 1 ? I18n.t("status.sessionConnected", n) : I18n.t("status.sessionsConnected", n));
        sessionsStatus.setGraphic(Icons.icon("terminal", 13, n > 0 ? "color-sessions" : "color-sessions-idle"));
        Selection s = app.state.node();
        if (s instanceof Selection.HostItem) refreshHostActions();
    }

    /** Gives the status bar to the ghost window. */
    HBox lendStatusBar() {
        setBottom(null);
        return status;
    }

    /** Takes the status bar back after ghost mode. */
    void takeBackStatusBar() {
        setBottom(status);
    }

    ActivityLog log() {
        return log;
    }

    public GhostMode ghost() {
        return ghost;
    }

    /** Rebuilds the central area for the current node: after ghost mode, to take the terminal back. */
    void reshow() {
        show();
    }

    /** Rebuilds title and content according to the chosen node and item. */
    private void show() {
        // In ghost mode the terminals belong to the ghost window: the hidden console must not take them back.
        if (ghost.active()) {
            content.getChildren().clear();
            return;
        }
        Selection s = app.state.node();
        Host host = s instanceof Selection.HostItem d ? app.state.host(d.hostId()) : null;
        // The host was removed, or the folder disappeared because it became empty.
        if ((s instanceof Selection.HostItem && host == null) || (s instanceof Selection.Folder c && !app.state.folderExists(c.path()))
                || (s instanceof Selection.Received && app.state.hosts.stream().noneMatch(x -> x.received))) {
            app.state.select(new Selection.Datacenter(), null);
            return;
        }
        String item = app.state.position.get().item();
        if (item == null) item = "";

        Node view;
        if (s instanceof Selection.Datacenter) {
            title.setText(I18n.t("title.datacenter", app.state.hosts.stream().filter(x -> !x.received).count()));
            view = new HostList(app, null);
        } else if (s instanceof Selection.Contacts) {
            title.setText(I18n.t("team.title"));
            view = new Contacts(app);
        } else if (s instanceof Selection.Received) {
            title.setText(I18n.t("tree.received"));
            view = HostList.received(app);
        } else if (s instanceof Selection.Folder c) {
            title.setText(I18n.t("title.folder", c.path()));
            view = new HostList(app, c.path());
        } else {
            title.setText(I18n.t("title.host", host.name, host.user + "@" + host.address, Folders.label(host.group)));
            view = item.equals("console") ? new HostConsole(app, host) : new HostSummary(app, host);
        }
        boolean full = item.equals("console");
        content.getStyleClass().remove("contents-full");
        if (full) content.getStyleClass().add("contents-full");
        if (full) content.getChildren().setAll(view);
        else {
            ScrollPane sc = new ScrollPane(view);
            sc.setFitToWidth(true);
            sc.setFitToHeight(true);
            sc.getStyleClass().add("contents-scroll");
            content.getChildren().setAll(sc);
        }
        refreshHostActions();
        refreshIndicators();
    }

    /** Where a host or folder created from the top bar ends up: in the open folder or in the host's folder. */
    private String currentFolder() {
        Selection s = app.state.node();
        if (s instanceof Selection.Folder c) return c.path();
        if (s instanceof Selection.HostItem d && app.state.host(d.hostId()) != null) return app.state.host(d.hostId()).group;
        return "";
    }

    private void refreshHostActions() {
        areaActions.getChildren().clear();
        if (app.state.node() instanceof Selection.Folder c) {
            String p = c.path();
            areaActions.getChildren().addAll(
                    Ui.primary(I18n.t("hosts.add"), "plus", () -> HostDialog.forNew(app, p)),
                    Ui.button(I18n.t("folders.newSub"), "folder", () -> Actions.newFolder(app, p)),
                    Ui.button(I18n.t("folders.rename"), "edit", () -> Actions.renameFolder(app, p)),
                    Ui.button(I18n.t("common.move"), "right", () -> Actions.moveFolder(app, p)),
                    Ui.danger(I18n.t("common.delete"), "trash", () -> Actions.deleteFolder(app, p)));
            return;
        }
        if (!(app.state.node() instanceof Selection.HostItem d)) return;
        Host host = app.state.host(d.hostId());
        // In the console the buttons are in the pop-up bar above the shell: the title row disappears.
        if (host == null || "console".equals(app.state.position.get().item())) return;
        areaActions.getChildren().add(Ui.primary(I18n.t("hosts.console"), "terminal", () -> app.sessions.showOrOpen(host)));
        areaActions.getChildren().addAll(Actions.hostButtons(app, host));
    }
}
