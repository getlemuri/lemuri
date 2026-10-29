package lemuri.be.sharing;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Host;
import lemuri.be.dto.Parcel;
import lemuri.be.dto.TrustedPeer;
import lemuri.be.service.SharingService;
import lemuri.be.service.Vault;

/** Hosts passed on as text, by email or chat: only the recipient opens them, only from a contact, only once. */
class TextSharingTest {
    private Sharing anna, bruno, carla;

    private static final class MemoryVault implements Vault {
        private final Map<String, String> secrets = new HashMap<>();
        private final List<Runnable> onLock = new ArrayList<>();
        private boolean open = true;

        @Override public boolean exists() { return true; }
        @Override public void onUnlock(Runnable r) {}
        @Override public void onLock(Runnable r) { onLock.add(r); }
        @Override public boolean isOpen() { return open; }
        @Override public void create(char[] main) {}
        @Override public boolean open(char[] main) { return open = true; }
        @Override public void lock() { open = false; }
        @Override public boolean changePassword(char[] current, char[] fresh) { return true; }
        @Override public String password(String key) { return open ? secrets.get(key) : null; }
        @Override public void remember(String key, String value) { secrets.put(key, value); }
        @Override public void forget(String key) { secrets.remove(key); }
        @Override public Map<String, String> entries() { return Map.copyOf(secrets); }
        @Override public void replaceAll(Map<String, String> entries) { secrets.clear(); secrets.putAll(entries); }
    }

    private static Sharing started() {
        Sharing s = new Sharing(new MemoryVault(), 0, 0);
        s.restartNow();
        return s;
    }

    @BeforeEach
    void start() throws Exception {
        anna = started();
        bruno = started();
        carla = started();
        // Anna and Bruno have exchanged their key cards.
        anna.addContact(bruno.myKeyCard(), "bruno");
        bruno.addContact(anna.myKeyCard(), "anna");
    }

    @AfterEach
    void stop() {
        anna.stop();
        bruno.stop();
        carla.stop();
    }

    private static Host host() {
        Host h = new Host();
        h.name = "db-prod";
        h.address = "203.0.113.5";
        h.user = "admin";
        return h;
    }

    private String fromAnnaToBruno() throws Exception {
        TrustedPeer to = anna.lan().contacts().get(0);
        List<SharingService.Outgoing> out = anna.send(host(), List.of(to), "secret", "the new db").get();
        assertEquals(1, out.size());
        assertNotNull(out.get(0).text(), "Bruno's PC is not on the local network: the host goes as text");
        return out.get(0).text();
    }

    @Test
    void theRecipientOpensItAndSeesWhoSentIt() throws Exception {
        String text = fromAnnaToBruno();
        assertTrue(text.startsWith(TextEnvelope.BEGIN));
        assertTrue(!text.contains("secret") && !text.contains("203.0.113.5"), "nothing readable in the text");
        // Pasted from an email reply: quoted and wrapped again.
        Delivery d = bruno.open("Hi, here it is:\n> " + text.replace("\n", "\n> "));
        assertEquals("anna", d.from());
        assertEquals("db-prod", d.content().name);
        assertEquals("secret", d.content().password);
    }

    @Test
    void anotherPersonCannotOpenIt() throws Exception {
        String text = fromAnnaToBruno();
        carla.addContact(anna.myKeyCard(), "anna");
        assertThrows(IllegalArgumentException.class, () -> carla.open(text));
    }

    @Test
    void onlyFromAContact() throws Exception {
        String text = fromAnnaToBruno();
        bruno.lan().forget(bruno.lan().contacts().get(0).key());
        assertTrue(bruno.lan().contacts().isEmpty());
        assertThrows(IllegalArgumentException.class, () -> bruno.open(text));
    }

    @Test
    void aChangedTextDoesNotOpen() throws Exception {
        String text = fromAnnaToBruno();
        String[] lines = text.split("\n");
        char c = lines[2].charAt(10);
        lines[2] = lines[2].substring(0, 10) + (c == 'A' ? 'B' : 'A') + lines[2].substring(11);
        assertThrows(IllegalArgumentException.class, () -> bruno.open(String.join("\n", lines)));
    }

    @Test
    void anEnvelopeIsDecidedOnlyOnce() throws Exception {
        String text = fromAnnaToBruno();
        Delivery d = bruno.open(text);
        assertThrows(IllegalArgumentException.class, () -> bruno.open(text), "already among the incoming ones");
        bruno.remove(d);
        assertThrows(IllegalArgumentException.class, () -> bruno.open(text), "accepted or rejected: not again");
    }

    @Test
    void keyCards() throws Exception {
        TextEnvelope.KeyCard k = TextEnvelope.parseKey("my key:\n  " + TextEnvelope.keyCard(anna.lan().contacts().get(0).key(), "bruno\u0007 r.") + "  \nbye");
        assertNotNull(k);
        assertEquals("bruno r.", k.name());
        assertNull(TextEnvelope.parseKey("LEMURI-KEY 1 notakey name"));
        // Your own key, a key already there, or a name already taken are refused.
        assertThrows(IllegalArgumentException.class, () -> anna.addContact(anna.myKeyCard(), "me"));
        assertThrows(IllegalArgumentException.class, () -> anna.addContact(bruno.myKeyCard(), "bruno 2"));
        assertThrows(IllegalArgumentException.class, () -> anna.addContact(carla.myKeyCard(), "Bruno"));
        anna.addContact(carla.myKeyCard(), "carla");
        assertEquals(2, anna.lan().contacts().size());
    }

    /** Analysis #10: a contact name that only looks like another one (invisible or foreign letters) is refused. */
    @Test
    void lookAlikeContactNamesAreRefused() {
        for (String name : List.of("bruno\u200B", "brun\u043e", "BRUNO", "bru no", "\u202Ebruno", "brun0"))
            assertThrows(IllegalArgumentException.class, () -> anna.addContact(carla.myKeyCard(), name), name);
        assertEquals(1, anna.lan().contacts().size());
    }

    /** Analysis #10: pairing saves the contact through the same check, whatever name the other PC announces. */
    @Test
    void pairingCannotReuseAContactsName() throws Exception {
        String carlaKey = TextEnvelope.parseKey(carla.myKeyCard()).key();
        assertThrows(IllegalArgumentException.class, () -> anna.lan().trusted(carlaKey, "bruno"));
        assertEquals(1, anna.lan().contacts().size());
        // Pairing again with the same key keeps its name: that is not a duplicate.
        String brunoKey = TextEnvelope.parseKey(bruno.myKeyCard()).key();
        anna.lan().trusted(brunoKey, "Bruno");
        assertEquals(1, anna.lan().contacts().size());
    }

    /** Analysis #10: the name suggested in a key line loses text direction and zero-width characters. */
    @Test
    void keyCardNamesLoseFormatCharacters() {
        String card = TextEnvelope.keyCard(TextEnvelope.parseKey(carla.myKeyCard()).key(), "car\u202Ela\u200B");
        assertEquals("carla", TextEnvelope.parseKey(card).name());
    }

    /** Analysis #10: when the vault cannot remember a handled envelope, the caller is told. */
    @Test
    void anEnvelopeNotRememberedIsReported() {
        MemoryVault locked = new MemoryVault();
        locked.lock();
        Parcel p = new Parcel();
        p.id = "x";
        p.when = System.currentTimeMillis();
        assertEquals(false, new DecidedEnvelopes(locked).remember(p));
        assertTrue(new DecidedEnvelopes(new MemoryVault()).remember(p));
    }

    /** Analysis #11: a reply shaped by someone else (objects where values go) is an IOException or "not a Lemuri", never an escape. */
    @Test
    void malformedHelloRepliesAreNotFatal() throws Exception {
        String key = TextEnvelope.parseKey(carla.myKeyCard()).key();
        for (String reply : List.of("{\"app\":{}}", "{\"app\":\"lemuri\",\"key\":\"" + key + "\",\"port\":{}}", "[1,2]", "{\"ok\":{}}")) {
            try (ServerSocket ss = new ServerSocket(0, 5, InetAddress.getLoopbackAddress())) {
                Thread.ofVirtual().start(() -> {
                    try (Socket s = ss.accept()) {
                        s.getInputStream().read(new byte[4096]);
                        s.getOutputStream().write((reply + "\n").getBytes(StandardCharsets.UTF_8));
                    } catch (IOException e) {
                        // closed
                    }
                });
                Lan lan = (Lan) anna.lan();
                lan.turnOn(true);
                try {
                    lan.discovery().hello("127.0.0.1:" + ss.getLocalPort());
                } catch (IOException | IllegalStateException expected) {
                    // the only two ways it may fail
                } finally {
                    lan.turnOn(false);
                }
            }
        }
    }
}
