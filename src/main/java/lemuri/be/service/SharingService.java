package lemuri.be.service;

import java.io.IOException;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Host;
import lemuri.be.dto.TrustedPeer;

/**
 * Exchange of hosts and credentials with contacts: directly over the local network when their PC is
 * there, otherwise as an encrypted text that the user passes on by any channel. No server in between.
 */
public interface SharingService {
    enum State { VAULT_LOCKED, READY }

    /** Notices for the interface, from any thread. */
    interface Listener {
        void state(State s, String message);

        /** All incoming deliveries, newest first. */
        void incoming(List<Delivery> deliveries);

        /** A delivery that has just arrived, only once: the interface offers it right away. */
        void arrival(Delivery i);

        /** A line to put in the log, already finished. */
        void log(String destination, String description);
    }

    /** How a host left for one contact: {@code text} is the envelope to pass on, null if the local network delivered it. */
    record Outgoing(TrustedPeer to, String text) {}

    void listen(Listener l);

    State state();

    LanService lan();

    String myFingerprint();

    /** This user's public key as one line of text, for contacts to add; null with the vault locked. */
    String myKeyCard();

    /** Reads the vault again and starts from scratch. */
    void restart();

    void stop();

    /** Sends the host to each contact, in one envelope for each. */
    CompletableFuture<List<Outgoing>> send(Host h, List<TrustedPeer> to, String password, String message);

    /** Opens a pasted envelope and adds it to the incoming ones. Throws an exception with the reason if it cannot. */
    Delivery open(String text);

    /**
     * Removes a delivery from the incoming ones, accepted or rejected: the same envelope will not be offered again.
     * False if that could not be saved in the vault, so the envelope could be offered again.
     */
    boolean remove(Delivery i);

    /** Adds a contact from the key card they sent (see {@link #myKeyCard}), under {@code name}. */
    void addContact(String keyCard, String name) throws IOException;
}
