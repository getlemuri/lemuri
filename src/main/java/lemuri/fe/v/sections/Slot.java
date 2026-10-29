package lemuri.fe.v.sections;

import javafx.scene.layout.StackPane;
import lemuri.fe.v.terminal.Terminal;

/**
 * Place where a terminal is shown. A JavaFX node has only one parent: adding
 * it here, JavaFX removes it from the slot it was in before, and the session
 * goes on without losing its screen.
 */
public class Slot extends StackPane {
    public Slot() {
        getStyleClass().add("terminal-slot");
    }

    public void show(Terminal t) {
        if (t.getParent() != this) getChildren().setAll(t);
        t.focus();
    }
}
