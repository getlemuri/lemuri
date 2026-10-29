package lemuri.be.sharing;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

import com.google.gson.Gson;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.lang.reflect.Field;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;
import java.util.function.Predicate;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Host;
import lemuri.be.dto.Parcel;
import lemuri.be.dto.Peer;

/**
 * A PC on the same network announces the public key of a trusted PC with its own port, and answers
 * {"ok":true} to everything. On loopback, with real sockets.
 */
class LanDeliveryTest {
    private Lan a, b;
    private ServerSocket fake;
    private final AtomicInteger fakeCalls = new AtomicInteger();

    private static Lan.Environment env(Envelope.Keys k, Consumer<Delivery> arrivals) {
        Map<String, String> vault = new HashMap<>();
        return new Lan.Environment() {
            public Envelope.Keys keys() { return k; }
            public String read(String key) { return vault.get(key); }
            public void write(String key, String value) { vault.put(key, value); }
            public void arrival(Delivery i) { arrivals.accept(i); }
        };
    }

    @AfterEach
    void close() throws Exception {
        if (a != null) a.stop();
        if (b != null) b.stop();
        if (fake != null) fake.close();
    }

    /** Reads the request and says "ok" without being able to open it. */
    private void startFake() throws Exception {
        fake = new ServerSocket(0, 50, InetAddress.getLoopbackAddress());
        Thread.ofVirtual().start(() -> {
            while (!fake.isClosed()) {
                try (Socket s = fake.accept()) {
                    new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8)).readLine();
                    fakeCalls.incrementAndGet();
                    OutputStream o = s.getOutputStream();
                    o.write("{\"ok\":true}\n".getBytes(StandardCharsets.UTF_8));
                    o.flush();
                } catch (Exception e) {
                    // closed at the end of the test
                }
            }
        });
    }

    private void announce(String key, int port) throws Exception {
        Field f = Lan.class.getDeclaredField("announcements");
        f.setAccessible(true);
        int udp = ((DatagramSocket) f.get(a)).getLocalPort();
        byte[] data = ("{\"app\":\"lemuri\",\"v\":2,\"type\":\"announce\",\"name\":\"b\",\"key\":\"" + key + "\",\"port\":" + port + "}")
                .getBytes(StandardCharsets.UTF_8);
        try (DatagramSocket u = new DatagramSocket()) {
            u.send(new DatagramPacket(data, data.length, InetAddress.getLoopbackAddress(), udp));
        }
    }

    /** Waits until A has heard of {@code key} at all, then for {@code check} to hold. */
    private Peer waitFor(String key, Predicate<Peer> check) throws Exception {
        Peer p = null;
        for (int i = 0; i < 60 && (p == null || !check.test(p)); i++) {
            Thread.sleep(50);
            p = a.reachable(key);
        }
        return p;
    }

    private static String parcel() {
        Host h = new Host();
        h.name = "db-prod";
        h.address = "203.0.113.5";
        return new Gson().toJson(Parcel.from(h, "secret", "new db"));
    }

    @Test
    void anOkWithoutReceiptIsNotADelivery() throws Exception {
        Envelope.Keys ka = Envelope.newKeys(), kb = Envelope.newKeys();
        CompletableFuture<Delivery> atB = new CompletableFuture<>();
        a = new Lan(env(ka, d -> {}), 0, 0);
        b = new Lan(env(kb, atB::complete), 0, 0);
        a.turnOn(true);
        b.turnOn(true);
        // Paired in the past, on both sides.
        a.trusted(new Peer(kb.publicKey(), "b", null, "127.0.0.1", b.tcpPort(), System.currentTimeMillis(), true));
        b.trusted(new Peer(ka.publicKey(), "a", null, "127.0.0.1", a.tcpPort(), System.currentTimeMillis(), true));
        startFake();

        // The fake is the first to announce B's key: it is the only address A knows.
        announce(kb.publicKey(), fake.getLocalPort());
        Peer p = waitFor(kb.publicKey(), x -> true);
        assertNotNull(p);
        assertEquals(fake.getLocalPort(), p.port());
        Peer first = p;
        assertThrows(IllegalStateException.class, () -> a.send(first, parcel()));
        assertEquals(1, fakeCalls.get());

        // B's real announcement does not replace the address, it is kept aside and tried when the first one fails.
        announce(kb.publicKey(), b.tcpPort());
        Thread.sleep(300);
        p = a.reachable(kb.publicKey());
        assertEquals(fake.getLocalPort(), p.port());
        a.send(p, parcel());
        assertEquals("db-prod", atB.get(5, TimeUnit.SECONDS).content().name);
        assertEquals(b.tcpPort(), a.reachable(kb.publicKey()).port(), "B proved itself: now it is the address");

        // Once proven, another announcement of the fake no longer moves it.
        announce(kb.publicKey(), fake.getLocalPort());
        Thread.sleep(300);
        assertEquals(b.tcpPort(), a.reachable(kb.publicKey()).port());
        int before = fakeCalls.get();
        a.send(a.reachable(kb.publicKey()), parcel());
        assertEquals(before, fakeCalls.get(), "the known address answered: the fake is not even asked");
        assertFalse(fake.isClosed());
    }
}
