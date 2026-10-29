package lemuri.be.sharing;

import static lemuri.be.sharing.LanWire.explain;
import static lemuri.be.sharing.LanWire.text;

import com.google.gson.Gson;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import com.google.gson.reflect.TypeToken;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.UnknownHostException;
import java.security.GeneralSecurityException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import java.util.logging.Logger;
import java.util.prefs.Preferences;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Peer;
import lemuri.be.dto.Request;
import lemuri.be.dto.TrustedPeer;
import lemuri.be.service.LanService;
import lemuri.be.util.ContactNames;
import lemuri.be.util.I18n;
import lemuri.be.util.Log;

/**
 * Direct exchange between PCs on the same local network, without going through
 * the internet. Every app announces itself over UDP ({@link LanDiscovery}); deliveries
 * travel over TCP, in an envelope that the recipient opens only if the sender's key is
 * trusted: a contact, added by pairing the two PCs once with the 6-digit code that appears
 * on both screens ({@link LanPairing}) or by pasting the key card they sent.
 *
 * <p>This class starts and stops everything, keeps the contacts, has the incoming
 * connections ({@link LanServer}) and sends deliveries; the wire format is in {@link LanWire}.
 */
public final class Lan implements LanService {
    public static final int ANNOUNCE_PORT = 47820, PORT = 47821;
    private static final long ANNOUNCE_S = 5;
    /** Connections served at once: beyond that, new ones are refused, so a stranger cannot flood the app. */
    private static final int MAX_JOBS = 8;
    private static final String TRUSTED = "lan:trusted";
    private static final Gson GSON = new Gson();
    private static final Logger LOG = Log.of(Lan.class);
    private static final Preferences PREF = Preferences.userRoot().node("lemuri/sharing");

    /** What is needed from the app: keys, vault, arrivals. */
    public interface Environment {
        /** null if the vault is locked. */
        Envelope.Keys keys() throws IOException, GeneralSecurityException;

        /** null if the vault is locked. */
        String read(String key);

        void write(String key, String value) throws IOException;

        /** A host arrived from a trusted PC, from the network thread. */
        void arrival(Delivery i);
    }

    private final Environment env;
    private final LanDiscovery discovery = new LanDiscovery(this);
    private final LanPairing pairing = new LanPairing(this);
    private final LanServer server = new LanServer(this);
    private volatile State state = State.OFF;
    private volatile Listener listener = new Listener() {
        @Override public void state(State s, String message) {}

        @Override public void peer(List<Peer> peer) {}

        @Override public void trusted(List<TrustedPeer> trusted) {}

        @Override public CompletableFuture<String> request(Request r) { return CompletableFuture.completedFuture(null); }

        @Override public void firewall(String warning) {}
    };
    /** The firewall active on this PC ("firewalld", "ufw"), or null; checked at every start. */
    private volatile String firewallHere;
    private volatile String firewallWarning;
    /** The last lists sent to the interface: sent again only if they change. */
    private volatile List<Peer> peer = List.of();
    private volatile List<TrustedPeer> trusted = List.of();

    private volatile Set<String> trustedKeys = Set.of();
    /** Outgoing jobs (pairings, hand-typed addresses): separate from the incoming ones, which a stranger can flood. */
    // volatile: written in restart/stop (synchronized) and read from the network threads without a lock.
    private volatile ExecutorService outgoing;
    private volatile Envelope.Keys keys;
    private volatile int tcpPort;
    private volatile ScheduledExecutorService times;
    private volatile ExecutorService jobs;
    private volatile ServerSocket socket;
    private volatile DatagramSocket announcements;

    /** The ports to listen on: the app's fixed ones, or 0 (any free one) in tests. */
    private final int preferredPort, announcePort;
    /** In tests the on/off choice and the typed addresses stay in memory: the user's preferences are not touched. */
    private final boolean testing;
    private volatile boolean onInTest;

    public Lan(Environment env) {
        this(env, PORT, ANNOUNCE_PORT);
    }

    /** For tests: with 0 and 0 the real ports are not touched, and an app open on the same PC does not interfere. */
    Lan(Environment env, int preferredPort, int announcePort) {
        this.env = env;
        this.preferredPort = preferredPort;
        this.announcePort = announcePort;
        this.testing = preferredPort != PORT || announcePort != ANNOUNCE_PORT;
    }

    @Override
    public void listen(Listener l) {
        listener = l;
    }

    /** The TCP port being listened on (for tests). */
    int tcpPort() {
        return tcpPort;
    }

    @Override
    public State state() {
        return state;
    }

    @Override
    public boolean isOn() {
        // Off until the user turns it on: announcing yourself on the network is a choice, not a default.
        return testing ? onInTest : PREF.getBoolean("lanOn", false);
    }

    @Override
    public void turnOn(boolean yes) {
        if (testing) onInTest = yes;
        else PREF.putBoolean("lanOn", yes);
        restart();
    }

    /** Hand-typed addresses: called every now and then even if their announcements do not arrive. */
    public List<String> addresses() {
        if (testing) return new ArrayList<>();
        String s = PREF.get("lanAddresses", "");
        List<String> l = new ArrayList<>();
        for (String x : s.split(",")) if (!x.isBlank()) l.add(x.trim());
        return l;
    }

    private void saveAddresses(List<String> l) {
        if (testing) return;
        PREF.put("lanAddresses", String.join(",", l));
    }

    // ── Startup ──────────────────────────────────────────────────────────────

    @Override
    public synchronized void restart() {
        stop();
        // The contacts are needed even with the local network off: hosts go to them as text too.
        loadTrusted();
        if (!isOn()) {
            set(State.OFF, "lan.msg.off");
            return;
        }
        Envelope.Keys k;
        try {
            k = env.keys();
        } catch (IOException | GeneralSecurityException | IllegalStateException e) {
            set(State.ERROR, "lan.msg.keys", explain(e));
            return;
        }
        if (k == null) {
            set(State.VAULT_LOCKED, "lan.msg.vault");
            return;
        }
        keys = k;
        try {
            socket = new ServerSocket();
            socket.setReuseAddress(true);
            try {
                socket.bind(new InetSocketAddress(preferredPort));
            } catch (IOException busy) {
                // Another app (or another copy of Lemuri) holds the port: take a free one and announce it.
                socket = new ServerSocket(0);
            }
            tcpPort = socket.getLocalPort();
            announcements = new DatagramSocket(null);
            announcements.setReuseAddress(true);
            announcements.setBroadcast(true);
            announcements.bind(new InetSocketAddress(announcePort));
        } catch (IOException e) {
            stop();
            set(State.ERROR, "lan.msg.ports", explain(e));
            return;
        }
        // Few threads and a short queue: beyond that, new connections are refused (see LanServer.listen).
        jobs = new ThreadPoolExecutor(MAX_JOBS, MAX_JOBS, 30, TimeUnit.SECONDS, new ArrayBlockingQueue<>(MAX_JOBS), r -> {
            Thread t = new Thread(r, "lan");
            t.setDaemon(true);
            return t;
        });
        ((ThreadPoolExecutor) jobs).allowCoreThreadTimeOut(true);
        times = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "lan-timers");
            t.setDaemon(true);
            return t;
        });
        outgoing = Executors.newVirtualThreadPerTaskExecutor();
        ServerSocket s = socket;
        ExecutorService j = jobs;
        ScheduledExecutorService t = times;
        DatagramSocket a = announcements;
        // The listeners have their own threads: the pool only serves incoming connections.
        Thread.ofPlatform().daemon().name("lan-tcp").start(() -> server.listen(s, j, t));
        Thread.ofPlatform().daemon().name("lan-udp").start(() -> discovery.listen(a));
        times.scheduleWithFixedDelay(survive("announce", () -> discovery.announce(announcements, announcePort)), 0, ANNOUNCE_S, TimeUnit.SECONDS);
        times.scheduleWithFixedDelay(survive("call addresses", discovery::callAddresses), 1, 15, TimeUnit.SECONDS);
        times.scheduleWithFixedDelay(survive("try nearby", discovery::tryNearby), 3, 15, TimeUnit.SECONDS);
        firewallHere = LanFirewall.active();
        times.scheduleWithFixedDelay(survive("publish", this::publish), 2, 2, TimeUnit.SECONDS);
        set(State.ACTIVE, "lan.msg.on", tcpPort, announcements.getLocalPort(), myName());
    }

    /**
     * A periodic task that cannot die: a task of scheduleWithFixedDelay that throws once is never run again,
     * silently. Whatever the network sends, the error is logged and the next round runs.
     */
    private static Runnable survive(String what, Runnable task) {
        return () -> {
            try {
                task.run();
            } catch (RuntimeException e) {
                LOG.warning("Local network, " + what + ": " + explain(e));
            }
        };
    }

    @Override
    public synchronized void stop() {
        if (times != null) times.shutdownNow();
        if (jobs != null) jobs.shutdownNow();
        if (outgoing != null) outgoing.shutdownNow();
        outgoing = null;
        try {
            if (socket != null) socket.close();
        } catch (IOException e) {
            // already closed
        }
        if (announcements != null) announcements.close();
        times = null;
        jobs = null;
        socket = null;
        announcements = null;
        // When stopped, the local network does not keep the private key in memory: it is read again from the vault on restart.
        keys = null;
        discovery.clear();
        if (!peer.isEmpty()) {
            peer = List.of();
            listener.peer(peer);
        }
    }

    @Override
    public void refresh() {
        ScheduledExecutorService t = times;
        if (t == null || t.isShutdown()) {
            restart();
            return;
        }
        t.execute(() -> {
            firewallHere = LanFirewall.active();
            discovery.announce(announcements, announcePort);
            discovery.callAddresses();
            discovery.tryNearby();
        });
    }

    /** Every 2 seconds the list for the interface, dropping whoever has not been seen for long. */
    synchronized void publish() {
        long now = System.currentTimeMillis();
        pairing.prune(now);
        server.prune(now);
        String a = firewallHere == null || now - server.lastEntry() < 10 * 60_000 ? null : LanFirewall.howToOpen(firewallHere);
        if (!Objects.equals(a, firewallWarning)) {
            firewallWarning = a;
            listener.firewall(a);
        }
        List<Peer> l = discovery.peers(now);
        if (!l.equals(peer)) {
            peer = List.copyOf(l);
            listener.peer(peer);
        }
    }

    // ── Trust ────────────────────────────────────────────────────────────────

    @Override
    public boolean trusted(String key) {
        return key != null && trustedKeys.contains(key);
    }

    @Override
    public Peer reachable(String key) {
        Peer p = discovery.get(key);
        return p != null && p.nearby() && trusted(key) ? p : null;
    }

    @Override
    public String myKey() {
        Envelope.Keys k = keys;
        return k != null ? k.publicKey() : null;
    }

    @Override
    public List<TrustedPeer> contacts() {
        return trusted;
    }

        /** The name of the contact with this key, as saved when it was added; null if it is not a contact. */
    @Override
    public String trustedName(String key) {
        for (TrustedPeer f : trusted) if (f.key().equals(key)) return f.name();
        return null;
    }

    private void loadTrusted() {
        List<TrustedPeer> l = new ArrayList<>();
        try {
            String j = env.read(TRUSTED);
            if (j != null) l = GSON.fromJson(j, new TypeToken<List<TrustedPeer>>() {}.getType());
        } catch (JsonParseException e) {
            // unreadable: start again without pairings
        }
        changeTrusted(l);
    }

    private void changeTrusted(List<TrustedPeer> l) {
        trusted = List.copyOf(l);
        Set<String> s = new HashSet<>();
        for (TrustedPeer f : l) s.add(f.key());
        trustedKeys = Set.copyOf(s);
        listener.trusted(trusted);
    }

    @Override
    public void trusted(Peer p) throws IOException {
        trusted(p.key(), p.who());
    }

    /**
     * Saves a contact. The name is what deliveries show as "from": an empty one, or one that looks like the name
     * of a contact with another key, is refused, whether it was typed or announced by the other PC.
     */
    @Override
    public synchronized void trusted(String key, String name) throws IOException {
        name = ContactNames.clean(name);
        String problem = ContactNames.problem(name);
        if (problem != null) throw new IllegalArgumentException(I18n.t(problem));
        String other = ContactNames.takenBy(trusted, name, key);
        if (other != null) throw new IllegalArgumentException(I18n.t("contact.nameTaken", other));
        List<TrustedPeer> l = new ArrayList<>(trusted);
        l.removeIf(f -> f.key().equals(key));
        l.add(new TrustedPeer(key, name));
        env.write(TRUSTED, GSON.toJson(l));
        changeTrusted(l);
    }

    @Override
    public synchronized void forget(String key) throws IOException {
        List<TrustedPeer> l = new ArrayList<>(trusted);
        l.removeIf(f -> f.key().equals(key));
        env.write(TRUSTED, GSON.toJson(l));
        changeTrusted(l);
    }

    // ── For the helpers in this package ─────────────────────────────────────

    Envelope.Keys keys() {
        return keys;
    }

    LanDiscovery discovery() {
        return discovery;
    }

    LanPairing pairing() {
        return pairing;
    }

    /** A host arrived from a trusted PC, from the network thread. */
    void arrival(Delivery d) {
        env.arrival(d);
    }

    /** Asks the user whether to accept a pairing request: the name chosen for the contact, or null if refused. */
    CompletableFuture<String> askUser(Request r) {
        return listener.request(r);
    }

    /** Who we are, as written in announcements, requests and replies. */
    JsonObject identity(String type) {
        JsonObject o = new JsonObject();
        o.addProperty("app", "lemuri");
        o.addProperty("v", 2);
        o.addProperty("type", type);
        o.addProperty("name", myName());
        // No email: anyone on the network would read who you are. Contacts are recognised by the key.
        o.addProperty("key", keys.publicKey());
        o.addProperty("port", tcpPort);
        return o;
    }

    JsonObject reply(boolean ok, String reason) {
        JsonObject r = new JsonObject();
        r.addProperty("ok", ok);
        if (reason != null) r.addProperty("reason", reason);
        Envelope.Keys k = keys;
        if (k != null) r.addProperty("key", k.publicKey());
        return r;
    }

    private static String myName() {
        String host;
        try {
            host = InetAddress.getLocalHost().getHostName();
        } catch (UnknownHostException e) {
            host = "pc";
        }
        return System.getProperty("user.name") + "@" + host;
    }

    // ── Requests to other PCs ────────────────────────────────────────────────

    /** Adds a hand-typed address, after checking that it answers. */
    @Override
    public CompletableFuture<Peer> addAddress(String address) {
        return CompletableFuture.supplyAsync(() -> {
            try {
                Peer p = discovery.hello(address);
                List<String> l = addresses();
                if (!l.contains(address)) l.add(address);
                saveAddresses(l);
                publish();
                return p;
            } catch (IOException | IllegalArgumentException | IllegalStateException e) {
                // IllegalArgumentException: a port that is not a number; IllegalStateException: not a Lemuri.
                throw new IllegalStateException(I18n.t("lan.noAnswer", address, explain(e)), e);
            }
        }, outgoing());
    }

    @Override
    public void removeAddress(String address) {
        List<String> l = addresses();
        l.remove(address);
        saveAddresses(l);
    }

    /**
     * Asks {@code p} to pair (see {@link LanPairing#ask}). On this side the PC is remembered only after
     * the user here has confirmed the same code too ({@link #trusted}).
     */
    @Override
    public CompletableFuture<Boolean> pair(Peer p, Consumer<String> onCode) {
        return CompletableFuture.supplyAsync(() -> {
            try {
                return pairing.ask(p, onCode);
            } catch (IOException e) {
                throw new UncheckedIOException(explain(e), e);
            }
        }, outgoing());
    }

    /**
     * Sends a parcel (JSON) to a trusted PC. From the network thread: blocks until the other side answers.
     * It counts as delivered only with a receipt sealed by the recipient's key; without one the next address
     * announced for that key is tried, and in the end an exception (and the host goes as text).
     */
    @Override
    public void send(Peer p, String parcel) throws IOException, GeneralSecurityException {
        if (!trusted(p.key())) throw new IllegalStateException(I18n.t("lan.notLinkedTo", p.who()));
        String id = UUID.randomUUID().toString();
        JsonObject inside = new JsonObject();
        inside.addProperty("id", id);
        inside.addProperty("when", System.currentTimeMillis());
        inside.addProperty("parcel", parcel);
        Envelope.Sealed b = Envelope.sealFrom(inside.toString(), keys, p.key(), keys.publicKey(), p.key());
        JsonObject m = identity("delivery");
        m.addProperty("ephemeral", b.ephemeral());
        m.addProperty("iv", b.iv());
        m.addProperty("data", b.data());
        List<String[]> where = new ArrayList<>();
        where.add(new String[] {p.address(), String.valueOf(p.port())});
        where.addAll(discovery.unproven(p.key()));
        Exception first = null;
        for (String[] w : where) {
            try {
                int port = Integer.parseInt(w[1]);
                JsonObject r = LanWire.ask(w[0], port, m, 15_000);
                if (!LanWire.yes(r, "ok"))
                    throw new IllegalStateException(I18n.t("lan.refused", p.who(), text(r, "reason") != null ? I18n.ifKey(text(r, "reason")) : I18n.t("lan.unknownReason")));
                if (!id.equals(receipt(r, p.key()))) throw new IllegalStateException(I18n.t("lan.noReceipt", p.who()));
                discovery.proven(p.key(), w[0], port);
                return;
            } catch (IOException | IllegalStateException | NumberFormatException e) {
                if (first == null) first = e;
            }
        }
        if (first instanceof IOException io) throw io;
        if (first instanceof RuntimeException r) throw r;
        throw new IllegalStateException(first);
    }

    /** What the receipt in {@code r} says, if it was sealed by {@code sender} for us; null otherwise. */
    private String receipt(JsonObject r, String sender) {
        try {
            JsonObject q = r.getAsJsonObject("receipt");
            return Envelope.openFrom(new Envelope.Sealed(text(q, "ephemeral"), text(q, "iv"), text(q, "data")), keys, sender, sender, keys.publicKey());
        } catch (GeneralSecurityException | RuntimeException e) {
            // a receipt that does not open, or a reply shaped by someone else: no receipt
            return null;
        }
    }

    private ExecutorService outgoing() {
        ExecutorService l = outgoing;
        if (l == null) throw new IllegalStateException(I18n.t("lan.notActive"));
        return l;
    }

    // ── Misc ─────────────────────────────────────────────────────────────────

    /** New state, with the message as an i18n key: to the user in their language, in the log in English. */
    private void set(State s, String key, Object... values) {
        String english = I18n.english(key, values);
        if (s == State.ERROR) LOG.warning("Local network sharing: " + english);
        else if (s != state) LOG.info("Local network sharing: " + english);
        state = s;
        listener.state(s, I18n.t(key, values));
    }
}
