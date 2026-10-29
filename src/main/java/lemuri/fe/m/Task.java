package lemuri.fe.m;

import javafx.beans.property.ObjectProperty;
import javafx.beans.property.SimpleObjectProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;

/** Line of the task log at the bottom: an operation, its state and how long it took. */
public class Task {
    public enum Outcome { RUNNING, OK, ERROR }

    public final long start = System.currentTimeMillis();
    public final ObjectProperty<Long> end = new SimpleObjectProperty<>();
    public final String destination;
    public final String description;
    public final ObjectProperty<Outcome> outcome = new SimpleObjectProperty<>(Outcome.RUNNING);
    public final StringProperty message = new SimpleStringProperty();

    public Task(String destination, String description) {
        this.destination = destination;
        this.description = description;
    }

    public void finish(Outcome e, String msg) {
        if (outcome.get() != Outcome.RUNNING) return;
        end.set(System.currentTimeMillis());
        message.set(msg);
        outcome.set(e);
    }
}
