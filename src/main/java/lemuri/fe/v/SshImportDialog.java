package lemuri.fe.v;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.collections.FXCollections;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Dialog;
import javafx.scene.control.Label;
import javafx.scene.control.TableView;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.util.StringConverter;
import lemuri.be.dto.Host;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.be.util.SshConfig;
import lemuri.fe.c.AppContext;

/**
 * Imports hosts from ~/.ssh/config: every host found, with a check box. Hosts already in the list and git
 * services start unchecked, with the reason next to them; hosts behind a ProxyJump say through which host.
 */
public final class SshImportDialog {
    private SshImportDialog() {}

    /** {@code folder}: where the hosts go by default ("" is the datacenter). */
    public static void open(AppContext app, String folder) {
        List<SshConfig.Entry> found;
        try {
            found = SshConfig.read();
        } catch (IOException e) {
            Dialogs.warning(I18n.t("sshImport.title"), I18n.t("sshImport.cannotRead", e.getMessage()));
            return;
        }
        if (found.isEmpty()) {
            Dialogs.warning(I18n.t("sshImport.title"), I18n.t("sshImport.none", home(SshConfig.sshDir().resolve("config").toString())));
            return;
        }

        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("sshImport.title"));
        // One line per host: hosts already in the list and git services start unticked, and say why.
        List<Row> rows = new ArrayList<>();
        for (SshConfig.Entry e : found) {
            // Same name, other login: two hosts with one name would make jump hosts and "lemuri -s" ambiguous.
            String unchecked = already(app, e) ? I18n.t("sshImport.already")
                    : sameName(app, e) ? I18n.t("sshImport.sameName")
                    : e.gitService() ? I18n.t("sshImport.git")
                    : null;
            String why = unchecked != null ? unchecked : e.proxyJump() != null ? I18n.t("sshImport.proxyJump", e.proxyJump()) : null;
            rows.add(new Row(e, why, unchecked == null));
        }
        TableView<Row> table = Grids.table(FXCollections.observableArrayList(rows), "");
        table.getColumns().add(Grids.checkColumn(rows, r -> r.chosen));
        table.getColumns().add(Grids.column(I18n.t("host.name"), 130, r -> r.entry.alias()));
        table.getColumns().add(Grids.nodeColumn(I18n.t("sshImport.col.destination"), 230, r -> Ui.code(r.entry.destination())));
        table.getColumns().add(Grids.nodeColumn(I18n.t("sshImport.col.key"), 150,
                r -> r.entry.identityFile() != null ? Ui.code(home(r.entry.identityFile())) : Ui.faint("-")));
        table.getColumns().add(Grids.nodeColumn(I18n.t("sshImport.col.note"), 230, r -> {
            if (r.why == null) return Ui.faint("-");
            Label l = Ui.faint(r.why);
            l.setTooltip(new Tooltip(r.why));
            return l;
        }));
        Grids.fitRows(table, rows.size(), 10);

        List<String> folders = new ArrayList<>();
        folders.add("");
        folders.addAll(app.state.folders());
        ComboBox<String> where = new ComboBox<>();
        where.setEditable(true);
        where.getItems().setAll(folders);
        // "" is the datacenter: shown with its name, and typed back as "".
        where.setConverter(new StringConverter<>() {
            @Override public String toString(String p) { return p == null || p.isEmpty() ? Folders.label("") : p; }

            @Override public String fromString(String s) { return s == null || s.equals(Folders.label("")) ? "" : s; }
        });
        where.setValue(folder != null ? folder : "");
        where.setMaxWidth(Double.MAX_VALUE);
        HBox.setHgrow(where, Priority.ALWAYS);

        d.getDialogPane().setContent(new VBox(10,
                new Label(I18n.t("sshImport.found", String.valueOf(found.size()), home(SshConfig.sshDir().resolve("config").toString()))),
                table,
                new HBox(8, new Label(I18n.t("sshImport.folder")), where)));
        d.getDialogPane().setPrefWidth(860);
        ButtonType ok = new ButtonType(I18n.t("sshImport.import"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        var button = d.getDialogPane().lookupButton(ok);
        button.getStyleClass().add("btn-primary");
        Runnable refresh = () -> button.setDisable(rows.stream().noneMatch(r -> r.chosen.get()));
        rows.forEach(r -> r.chosen.addListener((o, a, b) -> refresh.run()));
        refresh.run();

        if (d.showAndWait().orElse(ButtonType.CANCEL) != ok) return;
        String group = Folders.normalize(where.getConverter().fromString(where.getEditor().getText()));
        List<Host> hosts = new ArrayList<>();
        for (Row r : rows) if (r.chosen.get()) hosts.add(r.entry.toHost(group));
        jumpsByName(hosts, found, app.state.hosts);
        app.hosts.importHosts(hosts, "~/.ssh/config");
    }

    /** A host of the configuration as a table line: whether to import it, and why it starts unticked or how it is reached. */
    private static final class Row {
        final SshConfig.Entry entry;
        final String why;
        final BooleanProperty chosen;

        Row(SshConfig.Entry entry, String why, boolean chosen) {
            this.entry = entry;
            this.why = why;
            this.chosen = new SimpleBooleanProperty(chosen);
        }
    }

    /**
     * A ProxyJump names another alias of the configuration: if that host is imported too (or already in the
     * list with that name) the jump refers to it by name, otherwise it is written as user@host:port.
     */
    static void jumpsByName(List<Host> imported, List<SshConfig.Entry> found, List<Host> existing) {
        Set<String> names = new HashSet<>();
        for (Host h : existing) names.add(h.name.toLowerCase(Locale.ROOT));
        for (Host h : imported) names.add(h.name.toLowerCase(Locale.ROOT));
        Map<String, SshConfig.Entry> byAlias = new HashMap<>();
        for (SshConfig.Entry e : found) byAlias.put(e.alias().toLowerCase(Locale.ROOT), e);
        for (Host h : imported) {
            if (h.jump == null) continue;
            h.jump = Arrays.stream(h.jump.split(",")).map(String::strip).map(t -> {
                SshConfig.Entry e = byAlias.get(t.toLowerCase(Locale.ROOT));
                return names.contains(t.toLowerCase(Locale.ROOT)) || e == null ? t : e.destination();
            }).collect(Collectors.joining(","));
        }
    }

    /** A path under the home written with "~", as in the SSH configuration. */
    private static String home(String path) {
        String h = System.getProperty("user.home");
        return path.startsWith(h + "/") ? "~" + path.substring(h.length()) : path;
    }

    /** The same destination is already in the list: same address, port and user. */
    private static boolean sameName(AppContext app, SshConfig.Entry e) {
        return app.state.hosts.stream().anyMatch(x -> e.alias().equalsIgnoreCase(x.name));
    }

    private static boolean already(AppContext app, SshConfig.Entry e) {
        Host h = e.toHost("");
        return app.state.hosts.stream().anyMatch(x -> x.address != null && x.address.equalsIgnoreCase(h.address)
                && x.port == h.port && Objects.equals(x.user, h.user));
    }
}
