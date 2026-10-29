package lemuri.be.sharing;

import com.google.gson.Gson;
import com.google.gson.JsonParseException;
import java.io.IOException;
import java.security.GeneralSecurityException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.logging.Logger;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Host;
import lemuri.be.dto.Parcel;
import lemuri.be.dto.Peer;
import lemuri.be.dto.TrustedPeer;
import lemuri.be.service.LanService;
import lemuri.be.service.SharingService;
import lemuri.be.service.Vault;
import lemuri.be.util.I18n;
import lemuri.be.util.Log;

/**
 * Exchange of hosts and credentials with contacts. Every user has an X25519 key pair, in the vault;
 * a contact is a public key we trust, added by pairing on the local network or from the key card they
 * sent. A delivery is an {@link Envelope} sealed by the sender for one contact, who decides whether to
 * accept it. It travels over the local network ({@link Lan}) when the contact's PC is there, otherwise
 * as a text ({@link TextEnvelope}) that the user passes on by any channel: the channel never sees the
 * content and cannot change it. Replayed envelopes are refused ({@link DecidedEnvelopes}).
 */
public final class Sharing implements SharingService {
    private static final String PRIVATE = "sharing:private-key", PUBLIC = "sharing:public-key";
    private static final Gson GSON = new Gson();
    private static final Logger LOG = Log.of(Sharing.class);

    private final Vault vault;
    private final DecidedEnvelopes decided;
    private volatile State state = State.VAULT_LOCKED;
    private volatile Listener listener = new Listener() {
        @Override public void state(State s, String message) {}

        @Override public void incoming(List<Delivery> deliveries) {}

        @Override public void arrival(Delivery i) {}

        @Override public void log(String destination, String description) {}
    };
    /** Sending waits for the other PCs: never on the JavaFX thread. */
    private final ExecutorService network = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "sharing");
        t.setDaemon(true);
        return t;
    });
    private volatile Envelope.Keys keys;
    private final Inbox inbox = new Inbox(l -> listener.incoming(l));
    private final Lan lan;

    public Sharing(Vault vault) {
        this(vault, Lan.PORT, Lan.ANNOUNCE_PORT);
    }

    /** For tests: the local network on other ports (0 and 0: any free one, no announcements). */
    Sharing(Vault vault, int lanPort, int announcePort) {
        this.vault = vault;
        this.decided = new DecidedEnvelopes(vault);
        this.lan = new Lan(new Lan.Environment() {
            @Override
            public Envelope.Keys keys() throws IOException, GeneralSecurityException {
                return vault.isOpen() ? myKeys() : null;
            }

            @Override
            public String read(String key) {
                return vault.isOpen() ? vault.password(key) : null;
            }

            @Override
            public void write(String key, String value) throws IOException {
                vault.remember(key, value);
            }

            @Override
            public void arrival(Delivery i) {
                // Already accepted or rejected once: the local network keeps its ids only in memory.
                String why = decided.check(i.content());
                if (why != null) {
                    LOG.info("Local network delivery refused: " + I18n.english(why, i.from()));
                    return;
                }
                if (!inbox.add(i)) return;
                listener.log(i.from(), I18n.t("sharing.receivedLan", i.content().name + " (" + i.content().address + ")"));
                listener.arrival(i);
            }
        }, lanPort, announcePort);
        vault.onUnlock(this::restart);
        // Vault locked: the local network stops and forgets the private key, the incoming deliveries are dropped.
        vault.onLock(this::restart);
    }

    @Override
    public void listen(Listener l) {
        listener = l;
    }

    @Override
    public State state() {
        return state;
    }

    @Override
    public LanService lan() {
        return lan;
    }

    @Override
    public String myFingerprint() {
        Envelope.Keys k = keys;
        return k != null ? Envelope.fingerprint(k.publicKey()) : null;
    }

    @Override
    public String myKeyCard() {
        Envelope.Keys k = keys;
        return k != null ? TextEnvelope.keyCard(k.publicKey(), System.getProperty("user.name")) : null;
    }

    @Override
    public void restart() {
        if (!network.isShutdown()) network.execute(this::restartNow);
    }

    /** From the "sharing" thread, or directly in tests. */
    void restartNow() {
        lan.restart();
        if (!vault.isOpen()) {
            keys = null;
            inbox.clear();
            set(State.VAULT_LOCKED, "sharing.msg.vault");
            return;
        }
        try {
            keys = myKeys();
            set(State.READY, "sharing.msg.ready");
        } catch (IOException | GeneralSecurityException | IllegalStateException e) {
            keys = null;
            set(State.VAULT_LOCKED, "sharing.msg.keys", explain(e));
        }
    }

    @Override
    public void stop() {
        network.shutdownNow();
        lan.stop();
    }

    /** The keys are in the vault; the first time they are created (only once even if two threads ask together). */
    private synchronized Envelope.Keys myKeys() throws IOException, GeneralSecurityException {
        String priv = vault.password(PRIVATE), pub = vault.password(PUBLIC);
        if (priv != null && pub != null) return new Envelope.Keys(pub, priv);
        Envelope.Keys k = Envelope.newKeys();
        vault.remember(PRIVATE, k.privateKey());
        vault.remember(PUBLIC, k.publicKey());
        return k;
    }

    // ── Actions ──────────────────────────────────────────────────────────────

    @Override
    public CompletableFuture<List<Outgoing>> send(Host h, List<TrustedPeer> to, String password, String message) {
        return CompletableFuture.supplyAsync(() -> {
            Envelope.Keys k = keys;
            if (k == null) throw new IllegalStateException(I18n.t("sharing.msg.vault"));
            List<Outgoing> out = new ArrayList<>();
            for (TrustedPeer c : to) {
                if (!lan.trusted(c.key())) throw new IllegalStateException(I18n.t("lan.notLinkedTo", c.name()));
                // A new id and date for each recipient: each envelope is decided on its own.
                String text = GSON.toJson(Parcel.from(h, password, message));
                // Their PC on the local network: direct. If that fails, as text.
                Peer nearby = lan.reachable(c.key());
                if (nearby != null) {
                    try {
                        lan.send(nearby, text);
                        out.add(new Outgoing(c, null));
                        continue;
                    } catch (IOException | GeneralSecurityException | IllegalStateException | NumberFormatException e) {
                        LOG.info("Local network delivery failed, falling back to text: " + explain(e));
                    }
                }
                try {
                    out.add(new Outgoing(c, TextEnvelope.armor(k.publicKey(), c.key(), Envelope.sealFrom(text, k, c.key(), k.publicKey(), c.key()))));
                } catch (GeneralSecurityException e) {
                    throw new IllegalStateException(explain(e), e);
                }
            }
            return out;
        }, network);
    }

    @Override
    public Delivery open(String text) {
        Envelope.Keys k = keys;
        if (k == null) throw new IllegalStateException(I18n.t("sharing.msg.vault"));
        TextEnvelope.Posted e = TextEnvelope.unarmor(text);
        if (e == null) throw new IllegalArgumentException(I18n.t("paste.notAnEnvelope"));
        if (!e.to().equals(k.publicKey())) throw new IllegalArgumentException(I18n.t("paste.notForYou"));
        String who = lan.trustedName(e.from());
        // Only from a contact: a stranger's envelope would open, but nobody could say who sent it.
        if (who == null) throw new IllegalArgumentException(I18n.t("paste.unknownSender", Envelope.fingerprint(e.from())));
        Parcel p;
        try {
            p = GSON.fromJson(Envelope.openFrom(e.sealed(), k, e.from(), e.from(), e.to()), Parcel.class);
        } catch (GeneralSecurityException | JsonParseException x) {
            throw new IllegalArgumentException(I18n.t("sharing.envelope.cannotOpen", who));
        }
        String problem = p != null ? p.admit() : "empty";
        if (problem != null) throw new IllegalArgumentException(I18n.t("sharing.envelope.invalid", who, problem));
        String why = decided.check(p);
        if (why != null) throw new IllegalArgumentException(I18n.t(why, who));
        Delivery d = new Delivery("text:" + p.id, who, e.from(), p.when, p, p.name, null);
        if (!inbox.add(d)) throw new IllegalArgumentException(I18n.t("paste.already"));
        listener.log(who, I18n.t("sharing.receivedText", p.name + " (" + p.address + ")"));
        return d;
    }

    @Override
    public boolean remove(Delivery i) {
        inbox.remove(i.id());
        // Accepted or rejected: the same envelope must not be able to come back, by text or over the network.
        return decided.remember(i.content());
    }

    @Override
    public void addContact(String keyCard, String name) throws IOException {
        TextEnvelope.KeyCard c = TextEnvelope.parseKey(keyCard);
        if (c == null) throw new IllegalArgumentException(I18n.t("contact.notAKey"));
        Envelope.Keys k = keys;
        if (k != null && c.key().equals(k.publicKey())) throw new IllegalArgumentException(I18n.t("contact.yours"));
        String other = lan.trustedName(c.key());
        if (other != null) throw new IllegalArgumentException(I18n.t("contact.already", other));
        // The name is checked (empty, or looking like another contact's) where every contact is saved.
        lan.trusted(c.key(), name);
    }

    // ── State ────────────────────────────────────────────────────────────────

    /** New state, with the message as an i18n key: to the user in their language, in the log in English. */
    private void set(State s, String key, Object... values) {
        if (s != state) LOG.info("Sharing: " + I18n.english(key, values));
        state = s;
        listener.state(s, I18n.t(key, values));
    }

    private static String explain(Exception e) {
        return e.getMessage() != null ? e.getMessage() : e.toString();
    }
}
