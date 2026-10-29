package lemuri.fe.v.sections;

import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.TextArea;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import lemuri.be.dto.Host;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.v.Ui;

public final class HostNotes extends VBox {
    public HostNotes(AppContext app, Host h) {
        getStyleClass().add("section");
        TextArea text = Ui.limit(new TextArea(h.note), Ui.MAX_NOTE);
        text.setPromptText(I18n.t("notes.prompt"));
        text.getStyleClass().add("note-host");
        VBox.setVgrow(text, Priority.ALWAYS);
        text.setPrefRowCount(6);
        Label warning = new Label(I18n.t("notes.unsaved"));
        warning.getStyleClass().add("warn-text");
        Button save = Ui.primary(I18n.t("common.save"), "save", () -> {
            Host c = h.copy();
            c.note = text.getText();
            app.hosts.saveHost(c);
        });
        save.disableProperty().bind(text.textProperty().isEqualTo(h.note));
        warning.visibleProperty().bind(save.disableProperty().not());
        getChildren().addAll(Ui.bar(save, Ui.space(), warning), text);
    }
}
