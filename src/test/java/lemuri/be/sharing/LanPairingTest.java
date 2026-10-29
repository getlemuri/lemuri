package lemuri.be.sharing;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.gson.Gson;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Host;
import lemuri.be.dto.Parcel;
import lemuri.be.dto.Peer;
import lemuri.be.dto.Request;
import lemuri.be.dto.TrustedPeer;
import lemuri.be.service.LanService;
import lemuri.be.util.I18n;

/**
 * Pairing between two PCs, really exercised: two LAN services in the same test, talking over TCP
 * on loopback. Preferences and home are the fake ones set in the pom.
 */
class LanPairingTest {
    private Lan a, b;

    /** An in-memory environment: own keys, no team, fake vault. */
    private static Lan.Environment environment(Envelope.Keys k) {
        return environment(k, i -> {});
    }

    private static Lan.Environment environment(Envelope.Keys k, Consumer<Delivery> arrivals) {
        Map<String, String> vault = new HashMap<>();
        return new Lan.Environment() {
            public Envelope.Keys keys() { return k; }
            public String read(String key) { return vault.get(key); }
            public void write(String key, String value) { vault.put(key, value); }
            public void arrival(Delivery i) { arrivals.accept(i); }
        };
    }

    /** A listener that accepts every request and records the code shown. */
    private static LanService.Listener acceptAll(CompletableFuture<String> code) {
        return new LanService.Listener() {
            public void state(LanService.State s, String m) {}
            public void peer(List<Peer> l) {}
            public void trusted(List<TrustedPeer> l) {}
            public void firewall(String warning) {}
            public CompletableFuture<String> request(Request r) {
                code.complete(r.code());
                return CompletableFuture.completedFuture(r.peer().who());
            }
        };
    }

    @AfterEach
    void close() {
        if (a != null) a.stop();
        if (b != null) b.stop();
    }

    @Test
    void codeMatchesOnBothPcsAndResponderRemembersTheOther() throws Exception {
        Envelope.Keys ka = Envelope.newKeys(), kb = Envelope.newKeys();
        a = new Lan(environment(ka), 0, 0);
        b = new Lan(environment(kb), 0, 0);
        a.turnOn(true);
        b.turnOn(true);
        assertEquals(LanService.State.ACTIVE, b.state());
        CompletableFuture<String> codeOfB = new CompletableFuture<>(), codeOfA = new CompletableFuture<>();
        b.listen(acceptAll(codeOfB));

        Peer towardB = new Peer(kb.publicKey(), "b", null, "127.0.0.1", b.tcpPort(), System.currentTimeMillis(), true);
        assertTrue(a.pair(towardB, codeOfA::complete).get(20, TimeUnit.SECONDS));

        String c = codeOfA.get(1, TimeUnit.SECONDS);
        assertTrue(c.matches("\\d{3} \\d{3}"), c);
        assertEquals(c, codeOfB.get(1, TimeUnit.SECONDS));
        // B accepted: it trusts A. A will trust B only when its user confirms (trusted).
        assertTrue(b.trusted(ka.publicKey()));
        assertFalse(a.trusted(kb.publicKey()));

        // A second request from the same address within a minute is rejected.
        ExecutionException e = assertThrows(ExecutionException.class,
                () -> a.pair(towardB, x -> {}).get(20, TimeUnit.SECONDS));
        // The reason travels as an i18n key and is translated by the receiver.
        assertEquals(I18n.t("lan.wire.tooMany"), e.getCause().getMessage());
    }

    @Test
    void aPairedPcReceivesTheHostAndAStrangerIsRefused() throws Exception {
        Envelope.Keys ka = Envelope.newKeys(), kb = Envelope.newKeys();
        CompletableFuture<Delivery> arrived = new CompletableFuture<>();
        a = new Lan(environment(ka), 0, 0);
        b = new Lan(environment(kb, arrived::complete), 0, 0);
        a.turnOn(true);
        b.turnOn(true);
        b.listen(acceptAll(new CompletableFuture<>()));
        Peer towardB = new Peer(kb.publicKey(), "b", null, "127.0.0.1", b.tcpPort(), System.currentTimeMillis(), true);
        assertTrue(a.pair(towardB, x -> {}).get(20, TimeUnit.SECONDS));
        a.trusted(towardB);

        Host h = new Host();
        h.name = "web-01";
        h.address = "203.0.113.21";
        a.send(towardB, new Gson().toJson(Parcel.from(h, null, "hi")));
        Delivery d = arrived.get(5, TimeUnit.SECONDS);
        assertEquals("web-01", d.content().name);
        assertEquals("hi", d.content().message);

        // A third PC that trusts B on its own, but B never paired with it: B refuses what it sends.
        Lan c = new Lan(environment(Envelope.newKeys()), 0, 0);
        try {
            c.turnOn(true);
            c.trusted(towardB);
            Lan stranger = c;
            assertThrows(IllegalStateException.class, () -> stranger.send(towardB, "{}"));
        } finally {
            c.stop();
        }
    }
}
