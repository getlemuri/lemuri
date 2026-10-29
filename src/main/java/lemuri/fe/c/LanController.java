package lemuri.fe.c;

import java.io.IOException;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.function.Consumer;
import java.util.function.Function;
import javafx.application.Platform;
import javafx.beans.property.ObjectProperty;
import javafx.beans.property.SimpleObjectProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import lemuri.be.dto.Peer;
import lemuri.be.dto.Request;
import lemuri.be.dto.TrustedPeer;
import lemuri.be.service.LanService;
import lemuri.be.util.ContactNames;

/** Local network sharing as seen by the interface: observable state and lists, actions passed to the service. */
public final class LanController {
    private final LanService lan;
    public final ObjectProperty<LanService.State> state = new SimpleObjectProperty<>(LanService.State.OFF);
    public final StringProperty message = new SimpleStringProperty("");
    public final ObservableList<Peer> peer = FXCollections.observableArrayList();
    public final ObservableList<TrustedPeer> trusted = FXCollections.observableArrayList();
    /** What to do with this PC's firewall, or empty if there is nothing to say. */
    public final StringProperty firewall = new SimpleStringProperty("");
    /** On the JavaFX thread, when a PC asks to pair: the name for the contact if the user accepts, null otherwise. */
    public Function<Request, String> onRequest = r -> null;

    public LanController(LanService lan) {
        this.lan = lan;
        lan.listen(new LanService.Listener() {
            @Override
            public void state(LanService.State s, String m) {
                Platform.runLater(() -> {
                    state.set(s);
                    message.set(m);
                });
            }

            @Override
            public void peer(List<Peer> l) {
                Platform.runLater(() -> peer.setAll(l));
            }

            @Override
            public void trusted(List<TrustedPeer> l) {
                Platform.runLater(() -> trusted.setAll(l));
            }

            @Override
            public CompletableFuture<String> request(Request r) {
                CompletableFuture<String> choice = new CompletableFuture<>();
                Platform.runLater(() -> {
                    try {
                        choice.complete(onRequest.apply(r));
                    } catch (RuntimeException e) {
                        // The other PC waits for an answer: a dialog that fails counts as a refusal, never as a yes.
                        choice.complete(null);
                    }
                });
                return choice;
            }

            @Override
            public void firewall(String warning) {
                Platform.runLater(() -> firewall.set(warning != null ? warning : ""));
            }
        });
    }

    public void restart() {
        lan.restart();
    }

    public void refresh() {
        lan.refresh();
    }

    public boolean isOn() {
        return lan.isOn();
    }

    public void turnOn(boolean yes) {
        lan.turnOn(yes);
    }

    public boolean trusted(String key) {
        return lan.trusted(key);
    }

    public Peer reachable(String key) {
        return lan.reachable(key);
    }

    public String trustedName(String key) {
        return lan.trustedName(key);
    }

    public String myKey() {
        return lan.myKey();
    }

    public void trusted(String key, String name) throws IOException {
        lan.trusted(key, name);
    }

    /** The contact, with another key, whose name looks like {@code name}; null if the name is free. */
    public String nameTakenBy(String name, String key) {
        return ContactNames.takenBy(trusted, name, key);
    }

    public void forget(String key) throws IOException {
        lan.forget(key);
    }

    public CompletableFuture<Peer> addAddress(String address) {
        return lan.addAddress(address);
    }

    public void removeAddress(String address) {
        lan.removeAddress(address);
    }

    /** The code arrives on the JavaFX thread. */
    public CompletableFuture<Boolean> pair(Peer p, Consumer<String> onCode) {
        return lan.pair(p, c -> Platform.runLater(() -> onCode.accept(c)));
    }
}
