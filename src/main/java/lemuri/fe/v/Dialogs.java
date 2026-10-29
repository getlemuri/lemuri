package lemuri.fe.v;

import java.io.IOException;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.FutureTask;
import java.util.function.Function;
import java.util.function.Supplier;
import java.util.logging.Level;
import java.util.logging.Logger;
import javafx.application.Platform;
import javafx.collections.ListChangeListener;
import javafx.event.ActionEvent;
import javafx.event.EventHandler;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.Scene;
import javafx.scene.control.Alert;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.PasswordField;
import javafx.scene.control.TextField;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.stage.Window;
import javafx.stage.WindowEvent;
import javafx.util.StringConverter;
import lemuri.App;
import lemuri.be.dto.Host;
import lemuri.be.service.Vault;
import lemuri.be.terminal.SshTransport;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.be.util.Log;

/** Modal windows: title bar (blue), buttons at the bottom right. */
public final class Dialogs {
    private static final Logger LOG = Log.of(Dialogs.class);
    private Dialogs() {}

    /** Current theme, applied to windows and menus too (they have their own scene). */
    static boolean dark;

    /**
     * Main window, set at startup. Dialogs belong to it so they always stay
     * on top: with the custom frame, without an owner, GNOME can put them
     * behind and the modal window blocks the app without being visible.
     */
    public static Window main;

    public static void prepare(Dialog<?> d, String title) {
        if (main != null && main.isShowing()) d.initOwner(main);
        d.setTitle(title);
        d.setHeaderText(null);
        d.setGraphic(null);
        DialogPane p = d.getDialogPane();
        p.getStylesheets().add(App.class.getResource("style.css").toExternalForm());
        p.getStyleClass().add("window");
        if (dark) p.getStyleClass().add("dark");
        Label head = new Label(title);
        head.getStyleClass().add("window-title");
        head.setMaxWidth(Double.MAX_VALUE);
        p.setHeader(centredTitle(head));
        // With the custom frame, dialogs too have the app bar in place of the system one.
        if (WindowFrame.active()) DialogFrame.install(d, title);
    }

    /** The title of a dialog in the middle of its bar, with the app icon, name and version on the left. */
    static StackPane centredTitle(Label title) {
        title.setAlignment(Pos.CENTER);
        title.getStyleClass().add("centred-title");
        Label app = new Label("Lemuri " + App.VERSION, AppIcon.view(22));
        app.getStyleClass().add("window-app-name");
        app.setGraphicTextGap(6);
        StackPane box = new StackPane(title, app);
        StackPane.setAlignment(app, Pos.CENTER_LEFT);
        StackPane.setMargin(app, new Insets(0, 0, 0, 8));
        // Clicks and drags go through the name to the bar, which moves the window.
        app.setMouseTransparent(true);
        return box;
    }

    /*
     * JavaFX opens each submenu as a popup of its own, without the classes of the menu it comes from: it would
     * keep the default look (white, and unreadable in ghost mode). It gets them from the popup that owns it.
     */
    static {
        Window.getWindows().addListener((ListChangeListener<Window>) c -> {
            while (c.next()) {
                for (Window w : c.getAddedSubList()) {
                    if (w instanceof ContextMenu sub && sub.getOwnerWindow() instanceof ContextMenu parent) {
                        sub.getStyleClass().setAll(parent.getStyleClass());
                        syncSkin(sub);
                    }
                }
            }
        });
    }

    /** The content of a menu takes its classes only once, when it is made: later changes are copied by hand. */
    private static void syncSkin(ContextMenu m) {
        if (m.getSkin() != null && m.getSkin().getNode() != null) m.getSkin().getNode().getStyleClass().setAll(m.getStyleClass());
    }

    /** The context menu open right now: there is always at most one. */
    private static ContextMenu openMenu;

    /**
     * Context menus do not inherit the window classes: the theme is set here.
     * Opening one closes the previous one: on the terminal the right click does
     * not reach the menu already open, which would otherwise stay there and pile up.
     */
    public static ContextMenu style(ContextMenu m) {
        m.getStyleClass().add("app-menu");
        if (dark) m.getStyleClass().add("dark");
        // In ghost mode the menu is dark and see-through like the ghost, whatever the theme of the main window.
        m.addEventHandler(WindowEvent.WINDOW_SHOWING, e -> {
            Window owner = m.getOwnerWindow();
            boolean ghost = owner != null && owner.getScene() != null && owner.getScene().getRoot().getStyleClass().contains("ghost");
            m.getStyleClass().remove("ghost-menu");
            if (ghost) m.getStyleClass().add("ghost-menu");
            syncSkin(m);
        });
        // A click anywhere in the window closes the menu: on the terminal the automatic closing
        // of the popup does not fire, because the WebView takes the click.
        EventHandler<MouseEvent> outside = e -> m.hide();
        Scene[] scene = new Scene[1];
        m.addEventHandler(WindowEvent.WINDOW_SHOWING, e -> {
            if (openMenu != null && openMenu != m) openMenu.hide();
            openMenu = m;
            scene[0] = m.getOwnerWindow() != null ? m.getOwnerWindow().getScene() : null;
            if (scene[0] != null) scene[0].addEventFilter(MouseEvent.MOUSE_PRESSED, outside);
        });
        m.addEventHandler(WindowEvent.WINDOW_HIDDEN, e -> {
            if (openMenu == m) openMenu = null;
            if (scene[0] != null) scene[0].removeEventFilter(MouseEvent.MOUSE_PRESSED, outside);
            scene[0] = null;
        });
        return m;
    }

    /** Confirmation for actions that cannot be undone. */
    /** A notice: only the text and OK, nothing to choose. */
    public static void warning(String title, String text) {
        Alert a = new Alert(Alert.AlertType.NONE);
        prepare(a, title);
        a.getButtonTypes().setAll(ButtonType.OK);
        Label l = new Label(text);
        l.setWrapText(true);
        l.setMaxWidth(420);
        a.getDialogPane().setContent(l);
        a.getDialogPane().lookupButton(ButtonType.OK).getStyleClass().add("btn-primary");
        a.showAndWait();
    }

    public static boolean confirm(String title, String text, String action) {
        Alert a = new Alert(Alert.AlertType.NONE);
        prepare(a, title);
        ButtonType yes = new ButtonType(action, ButtonBar.ButtonData.OK_DONE);
        a.getButtonTypes().setAll(ButtonType.CANCEL, yes);
        Label l = new Label(text);
        l.setWrapText(true);
        l.setMaxWidth(420);
        a.getDialogPane().setContent(l);
        a.getDialogPane().lookupButton(yes).getStyleClass().add("btn-danger");
        return a.showAndWait().filter(yes::equals).isPresent();
    }

    /**
     * JSch questions come from the connection thread: they are answered on the
     * JavaFX thread while it waits. The saved password is tried only once;
     * if the server rejects it, it is asked by hand, and the typed one is remembered
     * only after authentication succeeded.
     */
    public static SshTransport.UserPrompts sshRequests(Host host, Vault vault) {
        return new SshTransport.UserPrompts() {
            private boolean triedSaved;
            private boolean wasSaved;
            private volatile String toRemember;

            @Override
            public boolean confirm(String message) {
                return onFx(() -> Dialogs.confirm(I18n.t("ssh.unknownHost", host.name), message, I18n.t("ssh.acceptConnect")));
            }

            @Override
            public String password(String message) {
                return onFx(() -> {
                    String saved = triedSaved ? null : saved();
                    triedSaved = true;
                    if (saved != null) {
                        wasSaved = true;
                        return saved;
                    }
                    CheckBox remember = new CheckBox(I18n.t("ssh.remember"));
                    remember.setSelected(true);
                    String question = wasSaved ? I18n.t("ssh.savedRejected") + "\n" + message : message;
                    String p = secret(I18n.t("ssh.passwordFor", host.destination()), question,
                            I18n.t("ssh.passwordNote"), remember);
                    toRemember = p != null && remember.isSelected() ? p : null;
                    return p;
                });
            }

            private String saved() {
                if (!vault.isOpen() && vault.exists()) openVault(vault);
                return vault.password(host.id);
            }

            @Override
            public String reply(String question) {
                return onFx(() -> secret(I18n.t("ssh.signIn", host.destination()), question, I18n.t("ssh.answerNotSaved"), null));
            }

            @Override
            public String passphrase(String message) {
                return onFx(() -> secret(I18n.t("ssh.keyFor", host.destination()), message, I18n.t("ssh.passphraseNotSaved"), null));
            }

            @Override
            public void authenticated() {
                String p = toRemember;
                toRemember = null;
                if (p != null) Platform.runLater(() -> rememberPassword(vault, host, p));
            }
        };
    }

    /** Saves a host password, unlocking (or creating) the vault if needed. */
    public static void rememberPassword(Vault vault, Host host, String password) {
        if (!vault.isOpen() && !openVault(vault)) return;
        try {
            vault.remember(host.id, password);
        } catch (IOException e) {
            warning(I18n.t("vault.title"), I18n.t("vault.cannotSave", e.getMessage()));
        }
    }

    /**
     * Asks for the master password and unlocks the vault; if it does not exist yet
     * it creates it, asking for the password twice. False if the user gives up.
     */
    /** Changes the master password: asks for the current one and the new one twice. Unlocks the vault first, if needed. */
    public static void changeVaultPassword(Vault vault) {
        if (!openVault(vault)) return;
        Dialog<Boolean> d = new Dialog<>();
        prepare(d, I18n.t("vault.change.title"));
        PasswordField current = Ui.limit(new PasswordField(), Ui.MAX_PASSWORD), fresh = Ui.limit(new PasswordField(), Ui.MAX_PASSWORD),
                retry = Ui.limit(new PasswordField(), Ui.MAX_PASSWORD);
        Label error = new Label();
        error.getStyleClass().add("warn-text");
        VBox body = new VBox(6, new Label(I18n.t("vault.change.current")), current, new Label(I18n.t("vault.change.new")), fresh,
                new Label(I18n.t("vault.repeat")), retry,
                Ui.faint(I18n.t("vault.change.note")),
                error);
        body.setPrefWidth(420);
        for (Label l : body.getChildren().stream().filter(Label.class::isInstance).map(Label.class::cast).toList()) l.setWrapText(true);
        d.getDialogPane().setContent(body);
        ButtonType ok = new ButtonType(I18n.t("vault.change.button"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        bOk.addEventFilter(ActionEvent.ACTION, e -> {
            try {
                if (current.getText().isEmpty() || fresh.getText().isEmpty()) error.setText(I18n.t("vault.fillAll"));
                else if (!fresh.getText().equals(retry.getText())) error.setText(I18n.t("vault.newMismatch"));
                else if (vault.changePassword(current.getText().toCharArray(), fresh.getText().toCharArray())) return;
                else {
                    error.setText(I18n.t("vault.currentWrong"));
                    current.selectAll();
                    current.requestFocus();
                }
            } catch (IOException ex) {
                error.setText(I18n.t("common.error", ex.getMessage()));
            }
            e.consume();
        });
        d.setResultConverter(b -> b == ok);
        Platform.runLater(current::requestFocus);
        if (d.showAndWait().orElse(false)) warning(I18n.t("vault.title"), I18n.t("vault.changed"));
    }

    public static boolean openVault(Vault vault) {
        if (vault.isOpen()) return true;
        boolean fresh = !vault.exists();
        Dialog<Boolean> d = new Dialog<>();
        prepare(d, fresh ? I18n.t("vault.create.title") : I18n.t("vault.unlock.title"));
        PasswordField field = Ui.limit(new PasswordField(), Ui.MAX_PASSWORD);
        PasswordField retry = Ui.limit(new PasswordField(), Ui.MAX_PASSWORD);
        Label error = new Label();
        error.getStyleClass().add("warn-text");
        VBox body = new VBox(6);
        if (fresh) {
            body.getChildren().addAll(
                    new Label(I18n.t("vault.create.choose")),
                    field, new Label(I18n.t("vault.repeat")), retry,
                    Ui.faint(I18n.t("vault.create.note")));
        } else {
            body.getChildren().addAll(new Label(I18n.t("vault.master")), field,
                    Ui.faint(I18n.t("vault.unlock.note")));
        }
        body.getChildren().add(error);
        body.setPrefWidth(420);
        for (Label l : body.getChildren().stream().filter(Label.class::isInstance).map(Label.class::cast).toList()) l.setWrapText(true);
        d.getDialogPane().setContent(body);
        ButtonType ok = new ButtonType(fresh ? I18n.t("vault.create.button") : I18n.t("vault.unlock.button"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        // The check happens before closing: with the wrong password the window stays open.
        bOk.addEventFilter(ActionEvent.ACTION, e -> {
            String p = field.getText();
            try {
                if (p.isEmpty()) error.setText(I18n.t("vault.enterPassword"));
                else if (fresh && !p.equals(retry.getText())) error.setText(I18n.t("vault.mismatch"));
                else if (fresh) {
                    vault.create(p.toCharArray());
                    return;
                } else if (vault.open(p.toCharArray())) return;
                else {
                    error.setText(I18n.t("vault.wrong"));
                    field.selectAll();
                }
            } catch (IOException ex) {
                error.setText(I18n.t("common.error", ex.getMessage()));
            }
            field.requestFocus();
            e.consume();
        });
        d.setResultConverter(b -> b == ok);
        Platform.runLater(field::requestFocus);
        return d.showAndWait().orElse(false) && vault.isOpen();
    }

    private static String secret(String title, String message, String note, Node extra) {
        Dialog<String> d = new Dialog<>();
        prepare(d, title);
        PasswordField field = Ui.limit(new PasswordField(), Ui.MAX_PASSWORD);
        VBox body = new VBox(6, new Label(message), field);
        if (extra != null) body.getChildren().add(extra);
        body.getChildren().add(Ui.faint(note));
        d.getDialogPane().setContent(body);
        ButtonType ok = new ButtonType(I18n.t("ssh.connect"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        d.getDialogPane().lookupButton(ok).getStyleClass().add("btn-primary");
        d.setResultConverter(b -> b == ok ? field.getText() : null);
        Platform.runLater(field::requestFocus);
        Optional<String> r = d.showAndWait();
        return r.orElse(null);
    }

    /**
     * Asks for a name. {@code error} tells what is wrong with the typed text (null if
     * fine): while there is an error the button stays disabled and the error is shown.
     */
    public static Optional<String> name(String title, String label, String initial, String action, Function<String, String> error) {
        Dialog<String> d = new Dialog<>();
        prepare(d, title);
        TextField field = Ui.limit(new TextField(initial), Ui.MAX_FOLDER);
        field.setPrefColumnCount(28);
        Label msg = new Label();
        msg.getStyleClass().add("warn-text");
        d.getDialogPane().setContent(new VBox(6, new Label(label), field, msg));
        ButtonType ok = new ButtonType(action, ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        Runnable check = () -> {
            String e = error.apply(field.getText().trim());
            msg.setText(e != null && !field.getText().isBlank() ? e : "");
            bOk.setDisable(e != null);
        };
        field.textProperty().addListener(o -> check.run());
        check.run();
        d.setResultConverter(b -> b == ok ? field.getText().trim() : null);
        Platform.runLater(() -> {
            field.requestFocus();
            field.selectAll();
        });
        return d.showAndWait();
    }

    /** Picks a destination folder; "" is the datacenter. */
    public static Optional<String> folder(String title, String text, List<String> choices, String current) {
        Dialog<String> d = new Dialog<>();
        prepare(d, title);
        ComboBox<String> c = new ComboBox<>();
        c.getItems().setAll(choices);
        c.setValue(choices.contains(current) ? current : choices.get(0));
        c.setMaxWidth(Double.MAX_VALUE);
        c.setPrefWidth(320);
        c.setConverter(new StringConverter<>() {
            @Override public String toString(String p) { return p == null ? "" : Folders.label(p); }

            @Override public String fromString(String s) { return s; }
        });
        d.getDialogPane().setContent(new VBox(6, new Label(text), c));
        ButtonType ok = new ButtonType(I18n.t("common.move"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        d.getDialogPane().lookupButton(ok).getStyleClass().add("btn-primary");
        d.setResultConverter(b -> b == ok ? c.getValue() : null);
        return d.showAndWait();
    }

    private static <T> T onFx(Supplier<T> f) {
        if (Platform.isFxApplicationThread()) return f.get();
        FutureTask<T> t = new FutureTask<>(f::get);
        Platform.runLater(t);
        try {
            return t.get();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return null;
        } catch (ExecutionException e) {
            LOG.log(Level.WARNING, "Dialog failed", e.getCause());
            return null;
        }
    }
}
