package lemuri.fe.v;

import java.io.File;
import java.io.IOException;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.time.LocalDate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.collections.FXCollections;
import javafx.event.ActionEvent;
import javafx.scene.Node;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Dialog;
import javafx.scene.control.Label;
import javafx.scene.control.PasswordField;
import javafx.scene.control.RadioButton;
import javafx.scene.control.TableView;
import javafx.scene.control.ToggleGroup;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.VBox;
import javafx.stage.FileChooser;
import lemuri.be.dto.Host;
import lemuri.be.storage.Backup;
import lemuri.be.util.I18n;
import lemuri.be.util.SshFolder;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Task;

/** Export everything to one encrypted file, and import it on another PC. */
public final class BackupDialogs {
    private BackupDialogs() {}

    // ── Export ───────────────────────────────────────────────────────────────

    /** Asks the system file chooser for the file (true: to save); the interface tests answer in its place. */
    static BiFunction<FileChooser, Boolean, File> files = (fc, save) -> save ? fc.showSaveDialog(Dialogs.main) : fc.showOpenDialog(Dialogs.main);

    public static void export(AppContext app) {
        // The passwords are part of it: the vault must be open.
        if (!Dialogs.openVault(app.vault)) return;
        Map<String, List<String>> usedBy = keysUsedByHosts(app.state.hosts);
        List<String> keys = new ArrayList<>(SshFolder.privateKeys());
        for (String k : usedBy.keySet()) if (!keys.contains(k) && SshFolder.exists(k)) keys.add(k);

        // One line per file: the keys (those used by a host ticked already), then known_hosts and config.
        List<SshRow> rows = new ArrayList<>();
        for (String k : keys) {
            List<String> hosts = usedBy.get(k);
            boolean pub = SshFolder.exists(k + ".pub");
            rows.add(new SshRow(k, I18n.t(pub ? "backup.kind.privateWithPub" : "backup.kind.private"),
                    hosts != null ? String.join(", ", hosts) : null, hosts != null, pub, k));
        }
        if (SshFolder.exists(SshFolder.KNOWN_HOSTS))
            rows.add(new SshRow(SshFolder.KNOWN_HOSTS, I18n.t("backup.kind.knownHosts"), null, true, false, SshFolder.KNOWN_HOSTS));
        if (SshFolder.exists(SshFolder.CONFIG))
            rows.add(new SshRow(SshFolder.CONFIG, I18n.t("backup.kind.config"), null, true, false, SshFolder.CONFIG));
        TableView<SshRow> table = sshTable(rows, I18n.t("backup.col.usedBy"), I18n.t("backup.noKeys", SshFolder.dir()));

        PasswordField pass = Ui.limit(new PasswordField(), Ui.MAX_PASSWORD), again = Ui.limit(new PasswordField(), Ui.MAX_PASSWORD);
        Label problem = new Label();
        problem.getStyleClass().add("warn-text");
        problem.setWrapText(true);
        Runnable check = () -> problem.setText(pass.getText().length() < Backup.MIN_PASSPHRASE ? I18n.t("backup.passphraseShort", Backup.MIN_PASSPHRASE)
                : !pass.getText().equals(again.getText()) ? I18n.t("backup.passphraseDiffers") : "");
        pass.textProperty().addListener((o, a, b) -> check.run());
        again.textProperty().addListener((o, a, b) -> check.run());
        check.run();

        VBox body = new VBox(10,
                wrapped(I18n.t("backup.export.what", app.state.hosts.size(), app.state.createdFolders.size(), app.vault.entries().size())),
                new Label(I18n.t("backup.sshKeys")), table,
                new Label(I18n.t("backup.passphrase")), pass, new Label(I18n.t("vault.repeat")), again, problem,
                note(I18n.t("backup.passphraseNote")));
        body.setPrefWidth(640);
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("backup.export.title"));
        ButtonType ok = new ButtonType(I18n.t("backup.export.button"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        d.getDialogPane().setContent(body);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        bOk.disableProperty().bind(problem.textProperty().isNotEmpty());
        if (d.showAndWait().filter(ok::equals).isEmpty()) return;

        FileChooser fc = new FileChooser();
        fc.setTitle(I18n.t("backup.export.title"));
        fc.setInitialFileName("lemuri-backup-" + LocalDate.now() + ".lemuri");
        fc.getExtensionFilters().add(new FileChooser.ExtensionFilter(I18n.t("backup.fileType"), "*.lemuri"));
        File to = files.apply(fc, true);
        if (to == null) return;
        try {
            List<Backup.SshFile> ssh = new ArrayList<>();
            List<String> left = new ArrayList<>();
            for (SshRow r : rows) {
                if (!r.chosen.get()) continue;
                String name = (String) r.data;
                List<String> names = r.withPub ? List.of(name, name + ".pub") : List.of(name);
                for (String n : names) {
                    // Only what an import accepts (see SshFolder.kind): anything else would make the whole file unreadable.
                    byte[] b = SshFolder.read(n);
                    if (SshFolder.kind(n, b) != null) ssh.add(Backup.SshFile.of(n, b));
                    else left.add(n);
                }
            }
            List<Host> hosts = app.state.hosts.stream().map(Host::copy).toList();
            Backup.Contents c = new Backup.Contents(whoAmI(), System.currentTimeMillis(), new ArrayList<>(app.state.createdFolders),
                    hosts, app.vault.entries(), ssh);
            char[] secret = pass.getText().toCharArray();
            byte[] file;
            try {
                file = Backup.seal(c, secret);
            } finally {
                Arrays.fill(secret, '\0');
            }
            writePrivate(to.toPath(), file);
            app.state.task(to.getName(), I18n.t("task.backupSaved", hosts.size(), ssh.size())).finish(Task.Outcome.OK, null);
            Dialogs.warning(I18n.t("backup.export.title"), I18n.t("backup.export.done", to, hosts.size(), ssh.size())
                    + (left.isEmpty() ? "" : "\n" + I18n.t("backup.export.left", String.join(", ", left))));
        } catch (IOException e) {
            Dialogs.warning(I18n.t("backup.export.title"), I18n.t("backup.export.failed", e.getMessage()));
        }
    }

    /** Key file name (in ~/.ssh) → the hosts that use it. */
    private static Map<String, List<String>> keysUsedByHosts(List<Host> hosts) {
        Map<String, List<String>> m = new LinkedHashMap<>();
        Path ssh = SshFolder.dir();
        for (Host h : hosts) {
            if (h.authMethod != Host.AuthMethod.KEY || h.key == null || h.key.isBlank()) continue;
            Path k = Path.of(h.key);
            // Only keys that live in ~/.ssh: a key elsewhere would come back in another place.
            Path file = k.getFileName();
            if (k.isAbsolute() && !ssh.equals(k.getParent()) || file == null) continue;
            String name = file.toString();
            if (SshFolder.safeName(name)) m.computeIfAbsent(name, x -> new ArrayList<>()).add(h.name);
        }
        return m;
    }

    /** The file holds every password, even if encrypted: readable by the user only, from the start. */
    private static void writePrivate(Path p, byte[] content) throws IOException {
        Files.deleteIfExists(p);
        try {
            Files.createFile(p, PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------")));
        } catch (UnsupportedOperationException e) {
            Files.createFile(p);
        }
        Files.write(p, content);
    }

    private static String whoAmI() {
        String host;
        try {
            host = InetAddress.getLocalHost().getHostName();
        } catch (UnknownHostException e) {
            host = "?";
        }
        return System.getProperty("user.name") + "@" + host;
    }

    // ── Import ───────────────────────────────────────────────────────────────

    public static void importFrom(AppContext app) {
        FileChooser fc = new FileChooser();
        fc.setTitle(I18n.t("backup.import.title"));
        fc.getExtensionFilters().addAll(new FileChooser.ExtensionFilter(I18n.t("backup.fileType"), "*.lemuri"),
                new FileChooser.ExtensionFilter(I18n.t("backup.allFiles"), "*"));
        File from = files.apply(fc, false);
        if (from == null) return;
        byte[] file;
        try {
            if (Files.size(from.toPath()) > Backup.MAX_FILE) throw new IOException(I18n.t("backup.notABackup"));
            file = Files.readAllBytes(from.toPath());
        } catch (IOException e) {
            Dialogs.warning(I18n.t("backup.import.title"), I18n.t("backup.import.failed", e.getMessage()));
            return;
        }
        Backup.Contents in = unlock(file);
        if (in != null) importContents(app, in, from.getName());
    }

    /** An opened backup: how to import it and which files of ~/.ssh, then the import and its report. */
    public static void importContents(AppContext app, Backup.Contents in, String source) {
        // The passwords go into this PC's vault: open it, or create it the first time.
        if (!Dialogs.openVault(app.vault)) return;

        ToggleGroup how = new ToggleGroup();
        RadioButton merge = new RadioButton(I18n.t("backup.merge"));
        RadioButton replace = new RadioButton(I18n.t("backup.replace"));
        merge.setToggleGroup(how);
        replace.setToggleGroup(how);
        // A PC with nothing yet takes everything; otherwise adding is the safe choice.
        (app.state.hosts.isEmpty() ? replace : merge).setSelected(true);
        // One line per file, with what importing it would do here.
        List<SshRow> rows = new ArrayList<>();
        for (Backup.SshFile f : in.ssh()) {
            byte[] content = f.bytes();
            SshFolder.Kind kind = SshFolder.kind(f.name(), content);
            SshFolder.Outcome outcome = SshFolder.preview(f.name(), content);
            String what = switch (outcome) {
                case ADDED -> I18n.t("backup.state.new");
                case SAME -> I18n.t("backup.state.same");
                case MERGED -> I18n.t("backup.state.merge");
                case BESIDE -> I18n.t("backup.state.beside", f.name() + ".imported");
            };
            String tip = what;
            boolean tick = true;
            if (kind == SshFolder.Kind.KNOWN_HOSTS && outcome != SshFolder.Outcome.SAME && outcome != SshFolder.Outcome.BESIDE) {
                // Which servers would be trusted from now on: the lines are shown, the CA lines are never added.
                List<String> lines = knownHostsPreview(content);
                long markers = SshFolder.markerLines(content);
                what = I18n.t("backup.state.servers", lines.size()) + (markers > 0 ? " · " + I18n.t("backup.state.markers", markers) : "");
                tip = what + "\n" + String.join("\n", lines);
            } else if (kind == SshFolder.Kind.CONFIG) {
                // A config can run commands at the next connection: never ticked for you, and it says so.
                tick = false;
                List<String> runs = SshFolder.commandDirectives(content);
                if (!runs.isEmpty()) what = I18n.t("backup.state.runsCommands", String.join(", ", runs)) + " · " + what;
                tip = what;
            }
            rows.add(new SshRow(f.name(), kindLabel(kind), what, tick, false, f));
            rows.get(rows.size() - 1).tip = tip;
        }
        TableView<SshRow> table = sshTable(rows, I18n.t("backup.col.state"), I18n.t("backup.import.noSsh"));
        VBox body = new VBox(10,
                wrapped(I18n.t("backup.import.what", in.from(), Ui.dateTime(in.created()), in.hosts().size(), in.folders().size(), in.secrets().size())),
                merge, note(I18n.t("backup.merge.note")), replace, note(I18n.t("backup.replace.note")),
                new Label(I18n.t("backup.import.ssh", "~/.ssh")), table, note(I18n.t("backup.import.sshNote")));
        body.setPrefWidth(640);
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("backup.import.title"));
        ButtonType ok = new ButtonType(I18n.t("backup.import.button"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        d.getDialogPane().setContent(body);
        d.getDialogPane().lookupButton(ok).getStyleClass().add("btn-primary");
        if (d.showAndWait().filter(ok::equals).isEmpty()) return;
        if (replace.isSelected() && !app.state.hosts.isEmpty()
                && !Dialogs.confirm(I18n.t("backup.import.title"), I18n.t("backup.replace.ask", app.state.hosts.size()), I18n.t("backup.replace.button")))
            return;

        Backup.Contents here = new Backup.Contents(whoAmI(), System.currentTimeMillis(), new ArrayList<>(app.state.createdFolders),
                new ArrayList<>(app.state.hosts), app.vault.entries(), List.of());
        Backup.Contents result = replace.isSelected() ? in : Backup.merge(here, in);
        StringBuilder report = new StringBuilder();
        try {
            // The vault first: if it cannot be written, the hosts stay as they were too.
            app.vault.replaceAll(result.secrets());
            app.hosts.replaceAll(result.folders(), result.hosts());
        } catch (IOException | IllegalStateException e) {
            Dialogs.warning(I18n.t("backup.import.title"), I18n.t("backup.import.failed", e.getMessage()));
            return;
        }
        int added = result.hosts().size() - (replace.isSelected() ? 0 : here.hosts().size());
        report.append(I18n.t(replace.isSelected() ? "backup.import.replaced" : "backup.import.added", Math.max(0, added)));
        if (!replace.isSelected() && in.secrets().containsKey(Backup.CONTACTS)) report.append('\n').append(I18n.t("backup.import.contactsSkipped"));
        for (SshRow r : rows) {
            if (!r.chosen.get()) continue;
            Backup.SshFile f = (Backup.SshFile) r.data;
            try {
                SshFolder.Installed i = SshFolder.install(f.name(), f.bytes());
                report.append('\n').append(switch (i.outcome()) {
                    case ADDED -> I18n.t("backup.ssh.added", i.name());
                    case SAME -> I18n.t("backup.ssh.same", i.name());
                    case MERGED -> I18n.t("backup.ssh.merged", i.name());
                    case BESIDE -> I18n.t("backup.ssh.beside", i.name(), i.savedAs());
                });
            } catch (IOException e) {
                report.append('\n').append(I18n.t("backup.ssh.failed", f.name(), e.getMessage()));
            }
        }
        // The sharing key and the contacts may have changed with the vault.
        app.sharing.restart();
        app.state.task(source, I18n.t("task.backupImported", Math.max(0, added))).finish(Task.Outcome.OK, null);
        Dialogs.warning(I18n.t("backup.import.title"), report.toString());
    }

    /** Asks for the passphrase until it opens the file; null if the user gives up. */
    private static Backup.Contents unlock(byte[] file) {
        PasswordField pass = Ui.limit(new PasswordField(), Ui.MAX_PASSWORD);
        Label error = new Label();
        error.getStyleClass().add("warn-text");
        error.setWrapText(true);
        VBox body = new VBox(8, new Label(I18n.t("backup.passphrase")), pass, error);
        body.setPrefWidth(420);
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("backup.import.title"));
        ButtonType ok = new ButtonType(I18n.t("backup.open"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        d.getDialogPane().setContent(body);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        Backup.Contents[] opened = new Backup.Contents[1];
        // Checked before closing: with the wrong passphrase the window stays open.
        bOk.addEventFilter(ActionEvent.ACTION, e -> {
            char[] secret = pass.getText().toCharArray();
            try {
                opened[0] = Backup.open(file, secret);
            } catch (Backup.WrongPassphrase x) {
                error.setText(x.getMessage());
                pass.selectAll();
                e.consume();
            } catch (IOException x) {
                error.setText(x.getMessage());
                e.consume();
            } finally {
                Arrays.fill(secret, '\0');
            }
        });
        Platform.runLater(pass::requestFocus);
        return d.showAndWait().filter(ok::equals).isPresent() ? opened[0] : null;
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    /**
     * A file of ~/.ssh as a table line: whether it goes, its name, what it is, and a detail (the hosts that
     * use a key when exporting, what will happen to it when importing). {@code data}: what to read or write.
     */
    private static final class SshRow {
        final BooleanProperty chosen;
        final String name, kind, detail;
        /** Shown on hover: the detail, or more (the servers a known_hosts adds). */
        String tip;
        final boolean withPub;
        final Object data;

        SshRow(String name, String kind, String detail, boolean chosen, boolean withPub, Object data) {
            this.name = name;
            this.kind = kind;
            this.detail = detail;
            this.chosen = new SimpleBooleanProperty(chosen);
            this.withPub = withPub;
            this.data = data;
        }
    }

    /** The files as a table: a check box, the name in monospace, the kind, and the detail in the last column. */
    private static TableView<SshRow> sshTable(List<SshRow> rows, String detailTitle, String empty) {
        TableView<SshRow> t = Grids.table(FXCollections.observableArrayList(rows), empty);
        t.getColumns().add(Grids.checkColumn(rows, r -> r.chosen));
        t.getColumns().add(Grids.nodeColumn(I18n.t("backup.col.file"), 150, r -> Ui.code(r.name)));
        t.getColumns().add(Grids.column(I18n.t("backup.col.kind"), 150, r -> r.kind));
        t.getColumns().add(Grids.nodeColumn(detailTitle, 230, r -> {
            if (r.detail == null) return Ui.faint("-");
            Label l = new Label(r.detail);
            l.setTooltip(new Tooltip(r.tip != null ? r.tip : r.detail));
            return l;
        }));
        Grids.fitRows(t, rows.size(), 6);
        return t;
    }

    /** What a file of a backup is, from its name. */
    private static String kindLabel(SshFolder.Kind kind) {
        return I18n.t(switch (kind) {
            case KNOWN_HOSTS -> "backup.kind.knownHosts";
            case CONFIG -> "backup.kind.config";
            case PUBLIC_KEY -> "backup.kind.public";
            case PRIVATE_KEY -> "backup.kind.private";
        });
    }

    /** The known_hosts lines an import would add, shortened to host and key type for the tooltip. */
    private static List<String> knownHostsPreview(byte[] content) {
        try {
            return SshFolder.newKnownHosts(content).stream().map(l -> {
                String[] p = l.split("\\s+");
                return p.length >= 2 ? p[0] + "  " + p[1] : l;
            }).toList();
        } catch (IOException e) {
            return List.of();
        }
    }

    private static Label wrapped(String text) {
        Label l = new Label(text);
        l.setWrapText(true);
        return l;
    }

    private static Label note(String text) {
        Label l = Ui.faint(text);
        l.setWrapText(true);
        return l;
    }

}
