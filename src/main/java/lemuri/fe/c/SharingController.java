package lemuri.fe.c;

import java.io.IOException;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.function.Consumer;
import javafx.application.Platform;
import javafx.beans.property.ObjectProperty;
import javafx.beans.property.SimpleObjectProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Host;
import lemuri.be.dto.TrustedPeer;
import lemuri.be.service.SharingService;
import lemuri.fe.m.AppState;
import lemuri.fe.m.Task;

/** Sharing as seen by the interface: observable state and lists, actions passed to the service. */
public final class SharingController {
    private final SharingService sharing;
    public final ObjectProperty<SharingService.State> status = new SimpleObjectProperty<>(SharingService.State.VAULT_LOCKED);
    public final StringProperty message = new SimpleStringProperty("");
    public final ObservableList<Delivery> incoming = FXCollections.observableArrayList();
    /** On the JavaFX thread for every delivery that has just arrived: the interface offers it right away. */
    public Consumer<Delivery> onArrival = i -> {};
    public final LanController lan;

    public SharingController(AppState state, SharingService sharing) {
        this.sharing = sharing;
        this.lan = new LanController(sharing.lan());
        sharing.listen(new SharingService.Listener() {
            @Override
            public void state(SharingService.State s, String m) {
                Platform.runLater(() -> {
                    status.set(s);
                    message.set(m);
                });
            }

            @Override
            public void incoming(List<Delivery> deliveries) {
                Platform.runLater(() -> incoming.setAll(deliveries));
            }

            @Override
            public void arrival(Delivery i) {
                Platform.runLater(() -> onArrival.accept(i));
            }

            @Override
            public void log(String destination, String description) {
                Platform.runLater(() -> state.task(destination, description).finish(Task.Outcome.OK, null));
            }
        });
    }

    /** The contacts: the same list the local network keeps. */
    public ObservableList<TrustedPeer> contacts() {
        return lan.trusted;
    }

    public String myFingerprint() {
        return sharing.myFingerprint();
    }

    public String myKeyCard() {
        return sharing.myKeyCard();
    }

    public void restart() {
        sharing.restart();
    }

    public void stop() {
        sharing.stop();
    }

    public CompletableFuture<List<SharingService.Outgoing>> send(Host h, List<TrustedPeer> to, String password, String message) {
        return sharing.send(h, to, password, message);
    }

    /** Opens a pasted envelope; throws an exception with the reason if it cannot. */
    public Delivery open(String text) {
        return sharing.open(text);
    }

    /** False if the vault could not save that the delivery was handled: it could be offered again. */
    public boolean remove(Delivery i) {
        return sharing.remove(i);
    }

    public void addContact(String keyCard, String name) throws IOException {
        sharing.addContact(keyCard, name);
    }
}
