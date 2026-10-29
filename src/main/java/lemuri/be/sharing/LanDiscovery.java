package lemuri.be.sharing;

import static lemuri.be.sharing.LanWire.explain;
import static lemuri.be.sharing.LanWire.isPrivate;
import static lemuri.be.sharing.LanWire.shorten;
import static lemuri.be.sharing.LanWire.text;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.net.ConnectException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.NoRouteToHostException;
import java.net.Socket;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyFactory;
import java.security.spec.X509EncodedKeySpec;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import lemuri.be.dto.Peer;
import lemuri.be.util.I18n;

/**
 * Who is on the local network: the UDP announcements sent and received, the apps seen, the addresses
 * typed by hand and the check that each PC's TCP port can really be reached.
 *
 * <p>Announcements prove nothing (anyone can say "I am mario"): they only tell who is there and at
 * which address. Trust lies in the keys, which {@link Lan} checks.
 */
final class LanDiscovery {
    private static final long SEEN_MS = Peer.SEEN_MS;
    /** Unknown PCs remembered at most: a burst of fake announcements does not fill memory. */
    private static final int MAX_UNKNOWN = 64;

    /** Other addresses announced for a trusted key, tried only if the known one does not prove itself. */
    private static final int MAX_UNPROVEN = 3;

    private final Lan lan;
    private final Map<String, Peer> seen = new ConcurrentHashMap<>();
    /** Trusted key → "address port" announced by someone who has not proved to hold that key yet. */
    private final Map<String, Set<String>> unproven = new ConcurrentHashMap<>();

    LanDiscovery(Lan lan) {
        this.lan = lan;
    }

    /** Announces this app on the broadcast address of every private interface. */
    void announce(DatagramSocket a, int announcePort) {
        if (a == null || lan.keys() == null) return;
        byte[] data = lan.identity("announce").toString().getBytes(StandardCharsets.UTF_8);
        // Only on private networks: never on an interface with a public address.
        Set<InetAddress> where = new HashSet<>();
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback()) continue;
                for (InterfaceAddress ia : ni.getInterfaceAddresses())
                    if (ia.getBroadcast() != null && isPrivate(ia.getAddress())) where.add(ia.getBroadcast());
            }
        } catch (SocketException e) {
            // no readable interface: no announcement this round
        }
        if (announcePort == 0) return; // no announcements in tests
        for (InetAddress d : where) {
            try {
                a.send(new DatagramPacket(data, data.length, d, announcePort));
            } catch (IOException e) {
                // network down or interface without broadcast: retry at the next round
            }
        }
    }

    void listen(DatagramSocket a) {
        byte[] buf = new byte[8192];
        while (!a.isClosed()) {
            try {
                DatagramPacket p = new DatagramPacket(buf, buf.length);
                a.receive(p);
                if (!isPrivate(p.getAddress())) continue;
                JsonObject o = JsonParser.parseString(new String(p.getData(), 0, p.getLength(), StandardCharsets.UTF_8)).getAsJsonObject();
                notice(o, p.getAddress().getHostAddress(), false);
            } catch (IOException | RuntimeException e) {
                // socket closed, or a packet that is not ours: anyone on the network can send any bytes here
            }
        }
    }

    /** Records an app that was seen (from an announcement, a request or a reply to "hello"); null if it is not one. */
    Peer notice(JsonObject o, String address, boolean manual) {
        return notice(o, address, manual, false);
    }

    /**
     * Like {@link #notice(JsonObject, String, boolean)}; {@code proven} when the message was sealed with the
     * key it claims. Anyone can announce a trusted key with their own address: for a trusted PC already seen
     * an unproven address does not replace the known one, it is kept aside and tried only if that one fails.
     */
    Peer notice(JsonObject o, String address, boolean manual, boolean proven) {
        if (!"lemuri".equals(text(o, "app"))) return null;
        String key = text(o, "key");
        String mine = lan.myKey();
        if (key == null || mine == null || key.equals(mine)) return null;
        try {
            Envelope.fingerprint(key);
            KeyFactory.getInstance("X25519").generatePublic(new X509EncodedKeySpec(Base64.getDecoder().decode(key)));
        } catch (GeneralSecurityException | IllegalArgumentException e) {
            return null; // not a key
        }
        int port = LanWire.number(o, "port", Lan.PORT);
        Peer before = seen.get(key);
        // A new, untrusted PC is remembered only if there is room.
        if (before == null && !lan.trusted(key) && seen.size() >= MAX_UNKNOWN) return null;
        boolean elsewhere = before != null && (!before.address().equals(address) || before.port() != port);
        String name = shorten(text(o, "name"));
        if (elsewhere && !proven && lan.trusted(key)) {
            name = before.name();
            Set<String> aside = unproven.computeIfAbsent(key, k -> ConcurrentHashMap.newKeySet());
            if (aside.size() < MAX_UNPROVEN) aside.add(address + " " + port);
            address = before.address();
            port = before.port();
        }
        if (proven) unproven.remove(key);
        Peer p = new Peer(key, name, null, address, port, System.currentTimeMillis(),
                manual || (before != null && before.manual()),
                before != null && before.address().equals(address) && before.port() == port ? before.warning() : null);
        seen.put(key, p);
        return p;
    }

    /** The addresses announced for {@code key} and not proven yet, as {address, port}. */
    List<String[]> unproven(String key) {
        Set<String> s = unproven.get(key);
        List<String[]> l = new ArrayList<>();
        if (s != null) for (String a : s) l.add(a.split(" ", 2));
        return l;
    }

    /** {@code key} has answered from this address with proof that it holds the private key: from now on it is the one. */
    void proven(String key, String address, int port) {
        unproven.remove(key);
        seen.computeIfPresent(key, (k, x) -> x.address().equals(address) && x.port() == port ? x
                : new Peer(x.key(), x.name(), x.email(), address, port, System.currentTimeMillis(), x.manual()));
    }

    Peer get(String key) {
        return key == null ? null : seen.get(key);
    }

    /** Drops whoever has not been seen for long (trusted PCs stay) and returns the others sorted by name. */
    List<Peer> peers(long now) {
        seen.values().removeIf(p -> now - p.seen() > 10 * SEEN_MS && !lan.trusted(p.key()));
        List<Peer> l = new ArrayList<>(seen.values());
        l.sort((x, y) -> x.who().compareToIgnoreCase(y.who()));
        return l;
    }

    void clear() {
        seen.clear();
        unproven.clear();
    }

    /**
     * Tries to open the TCP port of every PC seen recently: if announcements arrive but the port does not,
     * there is a firewall in between, and the kind of error tells which. The attempt also reaches the other
     * PC as proof that its firewall lets traffic in.
     */
    void tryNearby() {
        // Each on its own thread: a PC that does not answer makes us wait 3 seconds, and announcements must not slip.
        for (Peer p : List.copyOf(seen.values())) if (p.nearby()) Thread.ofVirtual().name("lan-probe").start(() -> tryPeer(p));
    }

    private void tryPeer(Peer p) {
        String a = null;
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(p.address(), p.port()), 3000);
        } catch (NoRouteToHostException e) {
            a = I18n.t("lan.probe.rejected", p.port());
        } catch (SocketTimeoutException e) {
            a = I18n.t("lan.probe.timeout", p.port());
        } catch (ConnectException e) {
            a = I18n.t("lan.probe.refused", p.port());
        } catch (IOException e) {
            a = I18n.t("lan.probe.other", explain(e));
        }
        String end = a;
        seen.computeIfPresent(p.key(), (k, x) -> Objects.equals(x.warning(), end) ? x : x.withWarning(end));
        lan.publish();
    }

    /** Calls the hand-typed addresses, whose announcements may not arrive. */
    void callAddresses() {
        for (String i : lan.addresses()) {
            try {
                hello(i);
            } catch (IOException | RuntimeException e) {
                // off, unreachable, not a Lemuri, a mistyped port or a reply shaped by someone else: retry soon,
                // and the other addresses are still called (one bad answer must not stop them)
            }
        }
    }

    /** Says "hello" to an address ("host" or "host:port") and records the app that answers. */
    Peer hello(String address) throws IOException {
        String host = address;
        int port = Lan.PORT;
        int i = address.lastIndexOf(':');
        if (i > 0 && address.indexOf(':') == i) {
            host = address.substring(0, i);
            port = Integer.parseInt(address.substring(i + 1));
        }
        JsonObject r = LanWire.ask(host, port, lan.identity("hello"), 5000);
        Peer p = notice(r, InetAddress.getByName(host).getHostAddress(), true);
        if (p == null) throw new IllegalStateException(I18n.t("lan.notLemuri", address));
        return p;
    }
}
