package lemuri.be.service;

import java.io.IOException;
import java.security.GeneralSecurityException;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.function.Consumer;
import lemuri.be.dto.Peer;
import lemuri.be.dto.Request;
import lemuri.be.dto.TrustedPeer;

/** Direct exchange of hosts between PCs on the same local network. */
public interface LanService {
    enum State { OFF, VAULT_LOCKED, ACTIVE, ERROR }

    /** Notices for the interface, from any thread. */
    interface Listener {
        void state(State s, String message);

        /** The apps seen on the network, sorted by name. */
        void peer(List<Peer> peer);

        /** The contacts: PCs paired with the code and keys added by hand. */
        void trusted(List<TrustedPeer> trusted);

        /** A PC asks to pair: completed with the name the user gives the contact, or null if refused. */
        CompletableFuture<String> request(Request r);

        /** This PC's firewall may keep the others out: what to do. Null if there is nothing to say. */
        void firewall(String warning);
    }

    void listen(Listener l);

    State state();

    /** Stops everything and starts again according to preferences and vault. */
    void restart();

    void stop();

    /** Announces itself again and checks the other PCs right away, instead of waiting for the next round. */
    void refresh();

    /** True if the user wants it on (saved preference). */
    boolean isOn();

    void turnOn(boolean yes);

    /** The key belongs to a contact. */
    boolean trusted(String key);

    /** The PC with this key, if it is visible on the network now and trusted: deliveries go there directly. */
    Peer reachable(String key);

    /** The contacts, as saved in the vault (empty with the vault locked). */
    List<TrustedPeer> contacts();

        /** The name of the contact with this key; null if it is not a contact. */
    String trustedName(String key);

    String myKey();

    /** Remembers the PC as a contact, in the vault. */
    void trusted(Peer p) throws IOException;

    /** Remembers a key as a contact, in the vault, replacing an older name for it. */
    void trusted(String key, String name) throws IOException;

    /** Removes the contact. */
    void forget(String key) throws IOException;

    /** Calls a PC at an address typed by hand and remembers it, even if its announcements do not arrive. */
    CompletableFuture<Peer> addAddress(String address);

    void removeAddress(String address);

    /**
     * Asks for pairing: true if the user of the other PC accepted. The code to compare reaches
     * {@code onCode} (from a network thread) only after the exchange of the random numbers.
     */
    CompletableFuture<Boolean> pair(Peer p, Consumer<String> onCode);

    /** Sends a parcel, already in JSON, to a trusted PC. */
    void send(Peer p, String parcel) throws IOException, GeneralSecurityException;
}
