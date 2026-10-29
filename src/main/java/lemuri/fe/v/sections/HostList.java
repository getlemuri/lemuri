package lemuri.fe.v.sections;

import java.time.Instant;
import java.time.LocalTime;
import java.time.ZoneId;
import java.util.List;
import java.util.Locale;
import java.util.function.Predicate;
import javafx.collections.MapChangeListener;
import javafx.collections.WeakMapChangeListener;
import javafx.collections.transformation.FilteredList;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableRow;
import javafx.scene.control.TableView;
import javafx.scene.control.TextField;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.Region;
import javafx.scene.layout.VBox;
import lemuri.be.dto.Disk;
import lemuri.be.dto.Host;
import lemuri.be.dto.MetricsResult;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Selection;
import lemuri.fe.v.Actions;
import lemuri.fe.v.Dialogs;
import lemuri.fe.v.Grids;
import lemuri.fe.v.HostDialog;
import lemuri.fe.v.Icons;
import lemuri.fe.v.SshImportDialog;
import lemuri.fe.v.Ui;

/**
 * List of hosts, of the datacenter or of a folder (subfolders included).
 * A double click opens the host. The Disk column shows the fullest filesystem,
 * read over SSH; on opening, readings older than 5 minutes are refreshed.
 */
public final class HostList extends VBox {
    private static final long OLD_MS = 5 * 60_000;
    /** Strong reference: the table listens to the resources with a weak listener, so a discarded view does not linger. */
    private final MapChangeListener<String, MetricsResult> onResources;

    /** Hosts of the datacenter (folder null) or of a folder: received hosts not yet sorted are kept apart. */
    public HostList(AppContext app, String folder) {
        this(app, folder, h -> !h.received && (folder == null || Folders.inside(h.group, folder)));
    }

    /** The hosts arrived from contacts and left outside your own folders. */
    public static HostList received(AppContext app) {
        return new HostList(app, null, h -> h.received);
    }

    private HostList(AppContext app, String folder, Predicate<Host> inside) {
        getStyleClass().add("section");
        FilteredList<Host> lines = new FilteredList<>(app.state.hosts);
        TextField search = Ui.limit(new TextField(), Ui.MAX_SEARCH);
        search.setPromptText(I18n.t("hosts.search"));
        HBox field = new HBox(6, Icons.icon("search", 13), search);
        field.getStyleClass().add("search");
        // The magnifier in the middle of the field's height, not at the top.
        field.setAlignment(Pos.CENTER_LEFT);
        Runnable filter = () -> {
            String t = search.getText().trim().toLowerCase(Locale.ROOT);
            lines.setPredicate(h -> inside.test(h)
                    && (t.isEmpty() || h.name.toLowerCase(Locale.ROOT).contains(t) || h.address.contains(t) || h.user.toLowerCase(Locale.ROOT).contains(t)));
        };
        search.textProperty().addListener(o -> filter.run());
        filter.run();

        TableView<Host> t = Grids.table(lines, I18n.t("hosts.none"));
        TableColumn<Host, Host> colStatus = Grids.nodeColumn(I18n.t("common.status"), 120, h -> Ui.hostStatus(h.state));
        colStatus.setMinWidth(90);
        t.getColumns().add(colStatus);
        t.getColumns().add(Grids.column(I18n.t("host.name"), 140, h -> h.name));
        t.getColumns().add(Grids.nodeColumn(I18n.t("host.address"), 170, h -> Ui.code(h.address + ":" + h.port)));
        t.getColumns().add(Grids.column(I18n.t("host.user"), 90, h -> h.user));
        t.getColumns().add(Grids.column(I18n.t("host.folder"), 140, h -> h.received ? I18n.t("hosts.from", h.fromContact) : Folders.label(h.group)));
        t.getColumns().add(Grids.column(I18n.t("host.system"), 140, h -> h.os != null ? h.os : ""));
        TableColumn<Host, Host> colDisk = Grids.nodeColumn(I18n.t("hosts.col.disk"), 210, h -> disk(app, h));
        // The most interesting column does not shrink with the others.
        colDisk.setMinWidth(225);
        t.getColumns().add(colDisk);
        t.getColumns().add(Grids.column(I18n.t("hosts.col.sessions"), 85, h -> {
            long n = app.state.connected(h.id);
            return n == 0 ? "" : String.valueOf(n);
        }));
        t.getColumns().add(Grids.nodeColumn(I18n.t("hosts.col.actions"), 80, h -> new HBox(2,
                Ui.action("terminal", I18n.t("hosts.console"), () -> app.sessions.showOrOpen(h)),
                Ui.action("edit", I18n.t("common.edit"), () -> HostDialog.open(app, h)))));
        t.setRowFactory(x -> {
            TableRow<Host> line = new TableRow<>();
            line.setOnContextMenuRequested(e -> {
                if (line.getItem() != null) Actions.menuHost(app, line.getItem()).show(line, e.getScreenX(), e.getScreenY());
            });
            return line;
        });
        t.setOnMouseClicked(e -> {
            Host h = t.getSelectionModel().getSelectedItem();
            if (e.getClickCount() == 2 && h != null) app.state.select(new Selection.HostItem(h.id), null);
        });
        VBox.setVgrow(t, Priority.ALWAYS);

        Button edit = Ui.button(I18n.t("common.edit"), "edit", () -> HostDialog.open(app, t.getSelectionModel().getSelectedItem()));
        Button console = Ui.button(I18n.t("hosts.console"), "terminal", () -> app.sessions.showOrOpen(t.getSelectionModel().getSelectedItem()));
        edit.disableProperty().bind(t.getSelectionModel().selectedItemProperty().isNull());
        console.disableProperty().bind(edit.disableProperty());

        Button remove = Ui.button(I18n.t("common.remove"), "trash", () -> Actions.removeHost(app, t.getSelectionModel().getSelectedItem()));
        remove.disableProperty().bind(edit.disableProperty());

        onResources = c -> t.refresh();
        app.state.resources.addListener(new WeakMapChangeListener<>(onResources));
        // The button may ask to unlock the vault; the automatic reading does not.
        Button disks = Ui.button(I18n.t("hosts.checkDisks"), "refresh", () -> {
            if (!app.vault.isOpen() && app.vault.exists()
                    && lines.stream().anyMatch(h -> h.authMethod == Host.AuthMethod.PASSWORD))
                Dialogs.openVault(app.vault);
            lines.forEach(h -> app.monitor.refreshMetrics(h, true));
            t.refresh();
        });
        long now = System.currentTimeMillis();
        for (Host h : lines) {
            MetricsResult e = app.state.resources.get(h.id);
            if (e == null || now - e.when() > OLD_MS) app.monitor.refreshMetrics(h, false);
        }

        Button ssh = Ui.action("download", I18n.t("sshImport.menu"), () -> SshImportDialog.open(app, folder != null ? folder : ""));
        Button add = Ui.primary(I18n.t("common.add"), "plus", () -> HostDialog.forNew(app, folder != null ? folder : ""));
        // The buttons never get cut: the search field gives way first.
        for (Button b : List.of(add, edit, console, remove, disks)) b.setMinWidth(Region.USE_PREF_SIZE);
        field.setMinWidth(120);
        getChildren().addAll(Ui.bar(add, ssh, edit, console, remove, disks, Ui.space(), field), t);
    }

    private static Node disk(AppContext app, Host h) {
        MetricsResult e = app.state.resources.get(h.id);
        if (e == null || e.resources() == null || e.resources().disks().isEmpty()) {
            Label l = Ui.faint(app.monitor.metricsRunning(h.id) ? I18n.t("metrics.reading") : e == null ? "" : I18n.t("metrics.notAvailable"));
            if (e != null && e.error() != null) Tooltip.install(l, new Tooltip(e.error()));
            return l;
        }
        Disk d = e.resources().fullest();
        String text = (d.dot().equals("/") ? I18n.t("disk.free", UsageBar.measure(d.freeKb())) : I18n.t("disk.freeIn", UsageBar.measure(d.freeKb()), d.dot()));
        UsageBar b = new UsageBar(d.quota(), text, 50);
        StringBuilder tip = new StringBuilder();
        for (Disk x : e.resources().disks())
            tip.append(String.format("%-18s %3d%%  %s%n", x.dot(), Math.round(x.quota() * 100), I18n.t("disk.freeOf",
                    UsageBar.measure(x.freeKb()), UsageBar.measure(x.totalKb()))));
        tip.append(I18n.t("metrics.readAt", LocalTime.ofInstant(Instant.ofEpochMilli(e.when()), ZoneId.systemDefault())
                .withNano(0).toString()));
        if (e.error() != null) tip.append("\n").append(I18n.t("metrics.tip.failed", e.error()));
        Tooltip.install(b, new Tooltip(tip.toString()));
        return b;
    }
}
