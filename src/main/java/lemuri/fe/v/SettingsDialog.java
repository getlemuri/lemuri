package lemuri.fe.v;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.function.Supplier;
import java.util.prefs.Preferences;
import javafx.application.Platform;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.collections.FXCollections;
import javafx.collections.transformation.FilteredList;
import javafx.event.ActionEvent;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.Parent;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Dialog;
import javafx.scene.control.Label;
import javafx.scene.control.Labeled;
import javafx.scene.control.ListCell;
import javafx.scene.control.ListView;
import javafx.scene.control.ScrollPane;
import javafx.scene.control.TextField;
import javafx.scene.layout.FlowPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.stage.Stage;
import javafx.stage.Window;
import lemuri.App;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.c.MonitorController;
import lemuri.fe.v.terminal.Terminal;

/**
 * The settings, from the button at the bottom of the activity bar: on the left the sections with
 * the search, on the right the chosen page, at the bottom OK, Cancel and Apply. The choices take effect
 * only with Apply or OK; the buttons inside the pages (vault) act right away.
 */
public final class SettingsDialog {
    private SettingsDialog() {}

    /** A section of the list; {@code items} are the texts the search looks in. */
    private record Page(String name, String icon, Node body, List<Labeled> items) {
        @Override public String toString() { return name; }
    }

    private static final String[][] LANGUAGES = {{"auto", I18n.t("menu.language.auto")}, {"en", "English"}, {"it", "Italiano"}};

    public static void open(AppContext app, Console console) {
        Preferences pref = Preferences.userRoot().node("lemuri/ui");

        // ── Appearance ──
        CheckBox dark = new CheckBox(I18n.t("menu.dark"));
        dark.setSelected(pref.getBoolean("darkTheme", false));
        CheckBox frame = new CheckBox(I18n.t("settings.titleBar"));
        frame.setSelected(WindowFrame.active());
        CheckBox splash = new CheckBox(I18n.t("menu.splash"));
        splash.setSelected(pref.getBoolean("splash", true));
        ComboBox<String> language = new ComboBox<>();
        for (String[] l : LANGUAGES) language.getItems().add(l[1]);
        String languageChoice = pref.get("language", "auto");
        for (int i = 0; i < LANGUAGES.length; i++) if (LANGUAGES[i][0].equals(languageChoice)) language.getSelectionModel().select(i);
        if (language.getSelectionModel().isEmpty()) language.getSelectionModel().select(0);
        Label languageLabel = new Label(I18n.t("settings.language"));
        HBox languageRow = new HBox(10, languageLabel, language);
        languageRow.setAlignment(Pos.CENTER_LEFT);
        Page appearance = page(I18n.t("settings.appearance"), "moon",
                group(I18n.t("settings.theme"), dark),
                group(I18n.t("settings.window"), frame, note(I18n.t("settings.afterRestart")), splash),
                group(I18n.t("settings.language"), languageRow, note(I18n.t("settings.afterRestart"))));
        appearance.items().add(languageLabel);

        // ── Vault ── (the buttons act right away: they are not choices to apply)
        Label vaultState = new Label();
        vaultState.setWrapText(true);
        Button unlockOrLock = Ui.button("", "lock", () -> {});
        Button change = Ui.button(I18n.t("menu.changeMaster"), "edit", () -> Dialogs.changeVaultPassword(app.vault));
        Runnable onVault = () -> {
            boolean isOpen = app.vault.isOpen();
            vaultState.setText(isOpen ? I18n.t("status.vault.open") : I18n.t("status.vault.locked"));
            unlockOrLock.setText(isOpen ? I18n.t("menu.vault.lock") : app.vault.exists() ? I18n.t("vault.unlock.title") : I18n.t("vault.create.title"));
            change.setDisable(!app.vault.exists());
        };
        unlockOrLock.setOnAction(e -> {
            if (app.vault.isOpen()) app.vault.lock();
            else Dialogs.openVault(app.vault);
            onVault.run();
        });
        onVault.run();
        Page vault = page(I18n.t("settings.vault"), "lock",
                group(I18n.t("common.status"), vaultState, note(I18n.t("settings.vault.note"))),
                group(I18n.t("settings.actions"), line(unlockOrLock, change)));

        // ── Backup ── (to another PC: one encrypted file with everything)
        Page backup = page(I18n.t("settings.backup"), "save",
                group(I18n.t("settings.backup"), note(I18n.t("settings.backup.note")),
                        line(Ui.button(I18n.t("backup.export.menu"), "upload", () -> BackupDialogs.export(app)),
                                Ui.button(I18n.t("backup.import.menu"), "download", () -> {
                                    BackupDialogs.importFrom(app);
                                    onVault.run();
                                }))));

        // ── Servers ──
        // Reachability: every minute, every 5 minutes (the default) or never.
        int[] probeChoices = {1, 5, 0};
        ComboBox<String> probe = new ComboBox<>(FXCollections.observableArrayList(
                I18n.t("settings.probe.every1"), I18n.t("settings.probe.every5"), I18n.t("settings.probe.never")));
        int probeNow = MonitorController.probeMinutes();
        probe.getSelectionModel().select(probeNow == 1 ? 0 : probeNow == 0 ? 2 : 1);
        CheckBox typesCommands = new CheckBox(I18n.t("settings.typesCommands"));
        typesCommands.setSelected(Terminal.typesCommands());
        Page server = page(I18n.t("settings.servers"), "shield",
                group(I18n.t("settings.probe"), probe, note(I18n.t("settings.probe.note"))),
                group(I18n.t("settings.terminal"), typesCommands, note(I18n.t("settings.typesCommands.note"))));

        // ── Local network ──
        CheckBox lan = new CheckBox(I18n.t("lan.enable"));
        lan.setSelected(app.sharing.lan.isOn());
        Page network = page(I18n.t("settings.lan"), "network",
                group(I18n.t("settings.lan"), lan, note(I18n.t("settings.lan.note"))));

        // ── About ─────────
        Path data = Path.of(System.getProperty("user.home"), ".lemuri");
        Page info = page(I18n.t("settings.about"), "note",
                group("Lemuri", Ui.details(List.of(
                        new Ui.Detail(I18n.t("settings.version"), new Label(App.VERSION)),
                        new Ui.Detail(I18n.t("settings.user"), new Label(System.getProperty("user.name"))),
                        new Ui.Detail(I18n.t("settings.data"), Ui.code(data.toString())),
                        new Ui.Detail(I18n.t("settings.logs"), Ui.code(data.resolve("logs").toString())),
                        new Ui.Detail("Java", new Label(System.getProperty("java.version")))))));

        List<Page> pages = List.of(appearance, vault, backup, server, network, info);

        // ── The choices to apply: Apply is enabled only if something changed ──
        Runnable[] save = new Runnable[1];
        Supplier<String> choices = () -> dark.isSelected() + "|" + frame.isSelected() + "|"
                + splash.isSelected() + "|" + language.getSelectionModel().getSelectedIndex() + "|" + lan.isSelected()
                + "|" + probe.getSelectionModel().getSelectedIndex() + "|" + typesCommands.isSelected();
        String[] savedOnes = {choices.get()};
        SimpleBooleanProperty changedOnes = new SimpleBooleanProperty(false);
        Runnable compare = () -> changedOnes.set(!choices.get().equals(savedOnes[0]));
        for (CheckBox c : List.of(dark, frame, splash, lan, typesCommands)) c.selectedProperty().addListener(o -> compare.run());
        probe.getSelectionModel().selectedIndexProperty().addListener(o -> compare.run());
        language.getSelectionModel().selectedIndexProperty().addListener(o -> compare.run());
        save[0] = () -> {
            pref.putBoolean("darkTheme", dark.isSelected());
            console.theme(dark.isSelected());
            WindowFrame.set(frame.isSelected());
            pref.putBoolean("splash", splash.isSelected());
            pref.put("language", LANGUAGES[Math.max(0, language.getSelectionModel().getSelectedIndex())][0]);
            if (lan.isSelected() != app.sharing.lan.isOn()) app.sharing.lan.turnOn(lan.isSelected());
            int minutes = probeChoices[Math.max(0, probe.getSelectionModel().getSelectedIndex())];
            if (minutes != MonitorController.probeMinutes()) app.monitor.probeMinutes(minutes);
            Terminal.typesCommands(typesCommands.isSelected());
            savedOnes[0] = choices.get();
            changedOnes.set(false);
        };

        // ── List with the search on the left, page on the right ──
        TextField search = Ui.limit(new TextField(), Ui.MAX_SEARCH);
        search.setPromptText(I18n.t("settings.search"));
        search.getStyleClass().add("settings-search");
        FilteredList<Page> visibleOnes = new FilteredList<>(FXCollections.observableArrayList(pages));
        ListView<Page> list = new ListView<>(visibleOnes);
        list.getStyleClass().add("settings-list");
        list.setCellFactory(l -> new ListCell<>() {
            @Override
            protected void updateItem(Page p, boolean empty) {
                super.updateItem(p, empty);
                setText(empty || p == null ? null : p.name());
                setGraphic(empty || p == null ? null : Icons.icon(p.icon(), 15));
            }
        });
        VBox.setVgrow(list, Priority.ALWAYS);
        VBox left = new VBox(8, search, list);
        left.getStyleClass().add("settings-left");
        // Fixed width: it does not change when moving from one page to another.
        left.setMinWidth(220);
        left.setMaxWidth(220);

        Label title = new Label();
        title.getStyleClass().add("settings-title");
        StackPane spot = new StackPane();
        ScrollPane scrolls = new ScrollPane(spot);
        scrolls.setFitToWidth(true);
        scrolls.getStyleClass().add("settings-scroll");
        VBox.setVgrow(scrolls, Priority.ALWAYS);
        VBox right = new VBox(10, title, scrolls);
        right.getStyleClass().add("settings-right");
        HBox.setHgrow(right, Priority.ALWAYS);

        list.getSelectionModel().selectedItemProperty().addListener((o, before, p) -> {
            if (p == null) return;
            title.setText(p.name());
            spot.getChildren().setAll(p.body());
        });
        // The search keeps the sections that contain the text, and marks the matching items in the page.
        search.textProperty().addListener((o, before, now) -> {
            String q = now.trim().toLowerCase(Locale.ROOT);
            Page choice = list.getSelectionModel().getSelectedItem();
            visibleOnes.setPredicate(p -> q.isEmpty() || p.name().toLowerCase(Locale.ROOT).contains(q)
                    || p.items().stream().anyMatch(v -> found(v, q)));
            for (Page p : pages) for (Labeled v : p.items()) {
                v.getStyleClass().remove("setting-match");
                // Notes count for the search, but only items are marked: a whole highlighted paragraph is unreadable.
                if (!q.isEmpty() && found(v, q) && !v.getStyleClass().contains("settings-note")) v.getStyleClass().add("setting-match");
            }
            if (choice == null || !visibleOnes.contains(choice)) list.getSelectionModel().selectFirst();
            if (visibleOnes.isEmpty()) {
                title.setText("");
                spot.getChildren().setAll(Ui.faint(I18n.t("settings.noResults")));
            }
        });
        list.getSelectionModel().selectFirst();

        HBox body = new HBox(left, right);
        body.getStyleClass().add("settings");
        // The size goes on the content: the window does not respect the one of the DialogPane.
        body.setPrefSize(860, 540);
        body.setMinSize(620, 380);

        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("activity.settings"));
        d.setResizable(true);
        ButtonType apply = new ButtonType(I18n.t("settings.apply"), ButtonBar.ButtonData.APPLY);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.OK, ButtonType.CANCEL, apply);
        d.getDialogPane().getStyleClass().add("settings-window");
        d.getDialogPane().setContent(body);
        d.getDialogPane().lookupButton(ButtonType.OK).getStyleClass().add("btn-primary");
        Button applyButton = (Button) d.getDialogPane().lookupButton(apply);
        applyButton.disableProperty().bind(changedOnes.not());
        // Apply saves and leaves the window open.
        applyButton.addEventFilter(ActionEvent.ACTION, e -> {
            save[0].run();
            e.consume();
        });
        // On top of the window with the custom frame, the resizable dialog opens at its minimum
        // size and recomputes it after setOnShown: the right size is set on the next pulse, centred on the main window.
        d.setOnShown(e -> Platform.runLater(() -> {
            Stage w = (Stage) d.getDialogPane().getScene().getWindow();
            // With the app frame the window also includes the transparent shadow margin.
            double m = WindowFrame.active() ? 2 * DialogFrame.SHADOW : 0;
            w.setMinWidth(680 + m);
            w.setMinHeight(480 + m);
            w.setWidth(900 + m);
            w.setHeight(640 + m);
            Window p = w.getOwner();
            if (p != null) {
                w.setX(p.getX() + (p.getWidth() - w.getWidth()) / 2);
                w.setY(p.getY() + (p.getHeight() - w.getHeight()) / 2);
            } else w.centerOnScreen();
            search.requestFocus();
        }));
        d.showAndWait().filter(ButtonType.OK::equals).ifPresent(b -> {
            if (changedOnes.get()) save[0].run();
        });
    }

    private static boolean found(Labeled v, String q) {
        return v.getText() != null && v.getText().toLowerCase(Locale.ROOT).contains(q);
    }

    /** A page made of groups; the items with a text go into the search. */
    private static Page page(String name, String icon, VBox... groups) {
        VBox body = new VBox(22, groups);
        body.getStyleClass().add("settings-page");
        List<Labeled> items = new ArrayList<>();
        collect(body, items);
        return new Page(name, icon, body, items);
    }

    private static void collect(Node n, List<Labeled> items) {
        if (n instanceof Labeled l && l.getText() != null && !l.getText().isBlank()) items.add(l);
        if (n instanceof Parent p) for (Node f : p.getChildrenUnmodifiable()) collect(f, items);
    }

    /** A group with its title and the items indented below. */
    private static VBox group(String title, Node... items) {
        Label t = new Label(title);
        t.getStyleClass().add("settings-group");
        VBox inside = new VBox(8, items);
        inside.getStyleClass().add("settings-items");
        return new VBox(8, t, inside);
    }

    /** Buttons side by side, without truncating their text: if they do not fit they wrap. */
    private static FlowPane line(Button... buttons) {
        for (Button b : buttons) b.setMinWidth(Region.USE_PREF_SIZE);
        FlowPane f = new FlowPane(8, 8, buttons);
        f.setPrefWrapLength(300);
        return f;
    }

    /** Small explanation below an item. */
    private static Label note(String text) {
        Label l = Ui.faint(text);
        l.setWrapText(true);
        l.getStyleClass().add("settings-note");
        return l;
    }
}
