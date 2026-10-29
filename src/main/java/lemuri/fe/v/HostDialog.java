package lemuri.fe.v;

import java.util.List;
import java.util.Optional;
import javafx.application.Platform;
import javafx.collections.FXCollections;
import javafx.scene.Node;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Control;
import javafx.scene.control.Dialog;
import javafx.scene.control.Label;
import javafx.scene.control.PasswordField;
import javafx.scene.control.Spinner;
import javafx.scene.control.TextField;
import javafx.scene.layout.ColumnConstraints;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.be.terminal.Jumps;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.AppState;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Task;

/**
 * Creating and editing an SSH host. A password typed here goes into the
 * encrypted vault, never into hosts.json.
 */
public final class HostDialog {
    private HostDialog() {}

    public static void open(AppContext app, Host existing) {
        if (existing == null) forNew(app, "");
        else window(app, existing.copy(), existing);
    }

    /** New host, already placed in the folder it is created from. */
    public static void forNew(AppContext app, String folder) {
        Host h = new Host();
        h.group = folder;
        window(app, h, null);
    }

    /** Same configuration, name and notes to redo: useful for twin hosts. */
    public static void duplicate(AppContext app, Host template) {
        Host h = template.copy();
        h.id = null;
        h.name = I18n.t("hostForm.copyName", template.name);
        h.note = "";
        h.state = HostStatus.UNKNOWN;
        window(app, h, null);
    }

    private static void window(AppContext app, Host h, Host existing) {
        // A new host has no name or address yet: a TextField built with null gives null back from getText().
        TextField name = Ui.limit(new TextField(h.name != null ? h.name : ""), Ui.MAX_NAME);
        name.setPromptText("web-03");
        // The folder is picked among the existing ones or typed: a new path creates it.
        ComboBox<String> group = new ComboBox<>(FXCollections.observableArrayList(app.state.folders()));
        group.setEditable(true);
        group.setValue(h.group);
        Ui.limit(group.getEditor(), Ui.MAX_FOLDER);
        group.getEditor().setPromptText(I18n.t("hostForm.folder.prompt"));
        TextField address = Ui.limit(new TextField(h.address != null ? h.address : ""), Ui.MAX_ADDRESS);
        address.setPromptText("203.0.113.10");
        Spinner<Integer> port = new Spinner<>(1, 65535, h.port);
        port.setEditable(true);
        TextField user = Ui.limit(new TextField(h.user != null ? h.user : ""), Ui.MAX_USER);
        ComboBox<String> auth = new ComboBox<>(FXCollections.observableArrayList(I18n.t("auth.key"), I18n.t("auth.password"), I18n.t("auth.agent")));
        auth.getSelectionModel().select(h.authMethod.ordinal());
        TextField key = Ui.limit(new TextField(h.key != null ? h.key : "id_ed25519"), Ui.MAX_KEY);
        PasswordField password = Ui.limit(new PasswordField(), Ui.MAX_PASSWORD);
        password.setPromptText(existing != null ? I18n.t("hostForm.password.keep") : I18n.t("hostForm.password.ask"));
        // Jump hosts (ProxyJump): another host of the list, chosen or typed, or user@host:port; several separated by commas.
        ComboBox<String> jump = new ComboBox<>(FXCollections.observableArrayList(app.state.hosts.stream()
                .filter(x -> !x.id.equals(h.id)).map(x -> x.name).sorted(String.CASE_INSENSITIVE_ORDER).toList()));
        jump.setEditable(true);
        jump.setValue(h.jump);
        // An editable ComboBox copies the value into its text only when it is shown: the check below needs it now.
        Ui.limit(jump.getEditor(), Ui.MAX_JUMP);
        jump.getEditor().setText(h.jump != null ? h.jump : "");
        jump.getEditor().setPromptText(I18n.t("hostForm.jump.prompt"));
        Label jumpError = Ui.faint("");
        jumpError.getStyleClass().add("warn-text");
        jumpError.setWrapText(true);
        jumpError.managedProperty().bind(jumpError.textProperty().isNotEmpty());
        jumpError.visibleProperty().bind(jumpError.managedProperty());
        Runnable checkJump = () -> {
            Host test = h.copy();
            test.name = name.getText().trim();
            test.jump = jump.getEditor().getText();
            List<Host> others = app.state.hosts.stream().filter(x -> !x.id.equals(h.id)).toList();
            String e = Jumps.resolve(test, others).routeError;
            jumpError.setText(e != null ? e : "");
        };
        jump.getEditor().textProperty().addListener((o, a, b) -> checkJump.run());
        name.textProperty().addListener((o, a, b) -> checkJump.run());
        checkJump.run();
        // Two hosts with one name: jump hosts and "lemuri -s" refer to hosts by name.
        Label nameError = Ui.faint("");
        nameError.getStyleClass().add("warn-text");
        nameError.managedProperty().bind(nameError.textProperty().isNotEmpty());
        nameError.visibleProperty().bind(nameError.managedProperty());
        Runnable checkName = () -> {
            String n = name.getText().trim();
            boolean taken = app.state.hosts.stream().anyMatch(x -> !x.id.equals(h.id) && n.equalsIgnoreCase(x.name));
            nameError.setText(taken ? I18n.t("hostForm.nameTaken") : "");
        };
        name.textProperty().addListener((o, a, b) -> checkName.run());
        checkName.run();
        TextField os = Ui.limit(new TextField(h.os), Ui.MAX_OS);
        os.setPromptText("Ubuntu 24.04 LTS");

        GridPane g = new GridPane();
        g.getStyleClass().add("form");
        g.getColumnConstraints().addAll(new ColumnConstraints(150), new ColumnConstraints(300));
        g.getColumnConstraints().get(1).setHgrow(Priority.ALWAYS);
        int r = 0;
        g.addRow(r++, label(I18n.t("host.name")), new VBox(4, name, nameError));
        g.addRow(r++, label(I18n.t("host.folder")), group);
        g.addRow(r++, label(I18n.t("host.address")), address);
        g.addRow(r++, label(I18n.t("host.port")), port);
        g.addRow(r++, label(I18n.t("host.user")), user);
        g.addRow(r++, label(I18n.t("host.auth")), auth);
        Label lKey = label(I18n.t("hostForm.key"));
        g.addRow(r++, lKey, key);
        Label lPassword = label(I18n.t("auth.password"));
        g.addRow(r++, lPassword, password);
        g.addRow(r++, label(I18n.t("host.jump")), new VBox(4, jump, jumpError));
        g.addRow(r++, label(I18n.t("host.os")), os);
        g.add(Ui.faint(I18n.t("hostForm.hint")), 0, r, 2, 1);
        for (Node n : List.of(group, port, auth, jump)) ((Control) n).setMaxWidth(Double.MAX_VALUE);
        lKey.visibleProperty().bind(auth.getSelectionModel().selectedIndexProperty().isEqualTo(0));
        key.visibleProperty().bind(lKey.visibleProperty());
        lPassword.visibleProperty().bind(auth.getSelectionModel().selectedIndexProperty().isEqualTo(1));
        password.visibleProperty().bind(lPassword.visibleProperty());
        // A hidden row takes no space either.
        for (Node n : List.of(lKey, key, lPassword, password)) n.managedProperty().bind(n.visibleProperty());

        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, existing != null ? I18n.t("hostForm.title.edit", existing.name) : h.id == null && h.name != null ? I18n.t("hostForm.title.duplicate") : I18n.t("hostForm.title.add"));
        ButtonType ok = new ButtonType(existing != null ? I18n.t("common.save") : I18n.t("common.add"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        d.getDialogPane().setContent(g);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        bOk.disableProperty().bind(name.textProperty().isEmpty().or(address.textProperty().isEmpty()).or(user.textProperty().isEmpty())
                .or(jumpError.textProperty().isNotEmpty()).or(nameError.textProperty().isNotEmpty()));
        Platform.runLater(name::requestFocus);

        Optional<ButtonType> outcome = d.showAndWait();
        if (outcome.isEmpty() || outcome.get() != ok) return;

        if (h.id == null) h.id = AppState.uid();
        if (h.state == null) h.state = HostStatus.UNKNOWN;
        h.name = name.getText().trim();
        h.group = Folders.normalize(group.getEditor().getText());
        h.address = address.getText().trim();
        try {
            // The Spinner accepts typed text only with Enter: read it directly.
            h.port = Math.min(65535, Math.max(1, Integer.parseInt(port.getEditor().getText().trim())));
        } catch (NumberFormatException e) {
            h.port = port.getValue();
        }
        h.user = user.getText().trim();
        h.authMethod = Host.AuthMethod.values()[auth.getSelectionModel().getSelectedIndex()];
        h.key = h.authMethod == Host.AuthMethod.KEY ? key.getText().trim() : null;
        String j = jump.getEditor().getText();
        h.jump = j == null || j.isBlank() ? null : j.trim();
        h.os = os.getText() == null || os.getText().isBlank() ? null : os.getText().trim();
        app.hosts.saveHost(h);
        if (h.authMethod == Host.AuthMethod.PASSWORD && !password.getText().isEmpty())
            Dialogs.rememberPassword(app.vault, h, password.getText());
        app.state.task(h.name, existing != null ? I18n.t("task.hostChanged") : I18n.t("task.hostAdded")).finish(Task.Outcome.OK, null);
        if (existing == null) app.state.select(new Selection.HostItem(h.id), null);
    }

    private static Label label(String t) {
        Label l = new Label(t);
        l.getStyleClass().add("field-caption");
        return l;
    }
}
