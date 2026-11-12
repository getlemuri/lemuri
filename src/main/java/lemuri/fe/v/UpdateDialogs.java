package lemuri.fe.v;

import java.io.IOException;
import java.util.Optional;
import java.util.function.Consumer;
import javafx.event.ActionEvent;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Dialog;
import javafx.scene.control.Label;
import javafx.scene.control.ProgressIndicator;
import javafx.scene.control.TextArea;
import javafx.scene.layout.HBox;
import javafx.scene.layout.VBox;
import javafx.geometry.Pos;
import lemuri.be.dto.Release;
import lemuri.be.util.I18n;
import lemuri.fe.c.UpdateController;

/** A new version: what changes, and the update with its steps, up to the restart. */
public final class UpdateDialogs {
    private UpdateDialogs() {}

    /** Opens a page in the browser: set by the app, which has the host services. */
    private static volatile Consumer<String> browser = u -> {};

    public static void browser(Consumer<String> b) {
        browser = b;
    }

    /** What is new in {@code r}, with the choice to update now, later, or never for this version. */
    public static void offer(UpdateController c, Release r) {
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("update.title", r.tag()));
        Label current = new Label(I18n.t("update.current", "v" + c.current()));
        TextArea notes = new TextArea(readable(r.notes()));
        notes.setEditable(false);
        notes.setWrapText(true);
        notes.setPrefRowCount(14);
        notes.setPrefColumnCount(60);
        notes.getStyleClass().add("update-notes");
        VBox body = new VBox(10, current, new Label(I18n.t("update.whatsNew")), notes);
        Optional<String> blocker = c.blocker();
        blocker.ifPresent(b -> {
            Label why = Ui.faint(I18n.t("update.manual", b));
            why.setWrapText(true);
            why.setMaxWidth(560);
            body.getChildren().add(why);
        });
        d.getDialogPane().setContent(body);

        ButtonType update = new ButtonType(I18n.t("update.install"), ButtonBar.ButtonData.OK_DONE);
        ButtonType page = new ButtonType(I18n.t("update.page"), ButtonBar.ButtonData.HELP);
        ButtonType skip = new ButtonType(I18n.t("update.skip"), ButtonBar.ButtonData.OTHER);
        ButtonType later = new ButtonType(I18n.t("update.later"), ButtonBar.ButtonData.CANCEL_CLOSE);
        d.getDialogPane().getButtonTypes().setAll(page, skip, later, update);
        Button updateButton = (Button) d.getDialogPane().lookupButton(update);
        updateButton.getStyleClass().add("btn-primary");
        updateButton.setDisable(blocker.isPresent());
        // The page opens and the window stays: the user may still choose.
        d.getDialogPane().lookupButton(page).addEventFilter(ActionEvent.ACTION, e -> {
            e.consume();
            browser.accept(r.url());
        });
        ButtonType choice = d.showAndWait().orElse(later);
        if (choice == update) install(c, r);
        else if (choice == skip) c.skip(r);
        else c.later();
    }

    /** The steps one under the other while they run; at the end Restart now, or what went wrong. */
    private static void install(UpdateController c, Release r) {
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("update.installing", r.tag()));
        VBox steps = new VBox(6);
        ProgressIndicator wheel = new ProgressIndicator();
        wheel.setPrefSize(22, 22);
        Label now = new Label(I18n.t("update.wait"));
        HBox head = new HBox(10, wheel, now);
        head.setAlignment(Pos.CENTER_LEFT);
        Label note = Ui.faint(I18n.t("update.wait.note"));
        note.setWrapText(true);
        note.setMaxWidth(520);
        VBox body = new VBox(12, head, steps, note);
        body.setMinWidth(520);
        d.getDialogPane().setContent(body);

        ButtonType restart = new ButtonType(I18n.t("update.restart"), ButtonBar.ButtonData.OK_DONE);
        // Nothing to press while git and Maven work: closing halfway would leave the copy between two versions.
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CLOSE);
        d.getDialogPane().lookupButton(ButtonType.CLOSE).setDisable(true);
        // A flag and not the state of the Close button: the pane makes new buttons when their list changes.
        boolean[] working = {true};
        d.setOnCloseRequest(e -> {
            if (working[0]) e.consume();
        });

        c.install(r, step -> {
            steps.getChildren().add(new Label("• " + step));
            now.setText(step);
        }).whenComplete((ok, error) -> javafx.application.Platform.runLater(() -> {
            working[0] = false;
            body.getChildren().removeAll(head, note);
            if (error == null) {
                Label done = new Label(I18n.t("update.done", r.tag()));
                done.getStyleClass().add("update-done");
                body.getChildren().add(done);
                d.getDialogPane().getButtonTypes().setAll(ButtonType.CLOSE, restart);
                d.getDialogPane().lookupButton(restart).getStyleClass().add("btn-primary");
            } else {
                Throwable t = error.getCause() != null ? error.getCause() : error;
                Label failed = new Label(I18n.t("update.failed"));
                failed.setWrapText(true);
                failed.setMaxWidth(520);
                TextArea why = new TextArea(t.getMessage() == null ? t.toString() : t.getMessage());
                why.setEditable(false);
                why.setWrapText(true);
                why.setPrefRowCount(10);
                body.getChildren().addAll(failed, why);
                d.getDialogPane().lookupButton(ButtonType.CLOSE).setDisable(false);
            }
            d.getDialogPane().getScene().getWindow().sizeToScene();
        }));
        if (d.showAndWait().filter(restart::equals).isEmpty()) return;
        try {
            c.restart();
        } catch (IOException e) {
            Dialogs.warning(I18n.t("update.title", r.tag()), I18n.t("update.restartFailed", e.getMessage()));
        }
    }

    /** The notes are Markdown from CHANGELOG.md: headings and list marks become plain text. */
    static String readable(String markdown) {
        if (markdown == null || markdown.isBlank()) return I18n.t("update.noNotes");
        StringBuilder b = new StringBuilder();
        for (String line : markdown.replace("\r", "").split("\n")) {
            String l = line.stripTrailing();
            if (l.startsWith("#")) {
                if (!b.isEmpty() && !b.toString().endsWith("\n\n")) b.append('\n');
                b.append(l.replaceFirst("^#+\\s*", "").toUpperCase(java.util.Locale.ROOT)).append('\n');
            } else if (l.matches("^\\s*[-*] .*")) b.append(l.replaceFirst("[-*] ", "• ")).append('\n');
            else b.append(l).append('\n');
        }
        return b.toString().replace("**", "").replace("`", "").strip();
    }
}
