package lemuri.be.storage;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;

/** The file that moves a whole Lemuri to another PC: only the passphrase opens it, nothing in it can be bent. */
class BackupTest {
    private static Host host(String id, String name) {
        Host h = new Host();
        h.id = id;
        h.name = name;
        h.address = "203.0.113.7";
        h.user = "deploy";
        return h;
    }

    private static Backup.Contents sample() {
        return new Backup.Contents("anna@pc", 1_700_000_000_000L, List.of("Prod"), List.of(host("h1", "web")),
                Map.of("h1", "secret", "sharing:private-key", "K"),
                List.of(Backup.SshFile.of("id_ed25519", "-----BEGIN OPENSSH PRIVATE KEY-----\n".getBytes(StandardCharsets.US_ASCII))));
    }

    @Test
    void whatIsSealedComesBackWithThePassphrase() throws Exception {
        byte[] file = Backup.seal(sample(), "a long passphrase".toCharArray());
        assertFalse(new String(file, StandardCharsets.UTF_8).contains("secret"), "nothing in clear");
        Backup.Contents c = Backup.open(file, "a long passphrase".toCharArray());
        assertEquals("anna@pc", c.from());
        assertEquals("web", c.hosts().get(0).name);
        assertEquals("secret", c.secrets().get("h1"));
        assertEquals(List.of("Prod"), c.folders());
        assertArrayEquals(sample().ssh().get(0).bytes(), c.ssh().get(0).bytes());
    }

    @Test
    void aWrongPassphraseOrATouchedFileDoesNotOpen() throws Exception {
        byte[] file = Backup.seal(sample(), "a long passphrase".toCharArray());
        assertThrows(Backup.WrongPassphrase.class, () -> Backup.open(file, "another passphrase".toCharArray()));
        // The plain parameters are bound to the data: weakening them makes the file unreadable, not weaker.
        JsonObject o = JsonParser.parseString(new String(file, StandardCharsets.UTF_8)).getAsJsonObject();
        o.addProperty("iterations", 1);
        assertThrows(Backup.WrongPassphrase.class, () -> Backup.open(o.toString().getBytes(StandardCharsets.UTF_8), "a long passphrase".toCharArray()));
    }

    @Test
    void parametersBeyondTheLimitsAreRefusedBeforeDeriving() throws Exception {
        JsonObject o = JsonParser.parseString(new String(Backup.seal(sample(), "a long passphrase".toCharArray()), StandardCharsets.UTF_8)).getAsJsonObject();
        o.addProperty("memory", 4 * 1024 * 1024);
        IOException e = assertThrows(IOException.class, () -> Backup.open(o.toString().getBytes(StandardCharsets.UTF_8), "x".toCharArray()));
        assertFalse(e instanceof Backup.WrongPassphrase);
    }

    @Test
    void notABackupAndNewerVersions() throws Exception {
        assertThrows(IOException.class, () -> Backup.open("hello".getBytes(StandardCharsets.UTF_8), "x".toCharArray()));
        assertThrows(IOException.class, () -> Backup.open("{\"format\":\"other\"}".getBytes(StandardCharsets.UTF_8), "x".toCharArray()));
        JsonObject o = JsonParser.parseString(new String(Backup.seal(sample(), "a long passphrase".toCharArray()), StandardCharsets.UTF_8)).getAsJsonObject();
        o.addProperty("version", Backup.VERSION + 1);
        IOException e = assertThrows(IOException.class, () -> Backup.open(o.toString().getBytes(StandardCharsets.UTF_8), "a long passphrase".toCharArray()));
        assertFalse(e instanceof Backup.WrongPassphrase);
    }

    /** Whoever made the file cannot make the import write outside ~/.ssh. */
    @Test
    void sshNamesWithAPathAreRefused() throws Exception {
        for (String name : List.of("../.bashrc", "/etc/passwd", "..", ".hidden", "a/b")) {
            Backup.Contents c = new Backup.Contents("x", 1, List.of(), List.of(), Map.of(), List.of(Backup.SshFile.of(name, new byte[] {1})));
            byte[] file = Backup.seal(c, "a long passphrase".toCharArray());
            assertThrows(IOException.class, () -> Backup.open(file, "a long passphrase".toCharArray()), name);
        }
    }

    @Test
    void mergeKeepsWhatIsHereAndAddsTheRest() {
        Backup.Contents here = new Backup.Contents("b@pc", 2, List.of("Dev"), List.of(host("h1", "mine")),
                Map.of("h1", "my-pw", "sharing:private-key", "MINE"), List.of());
        Backup.Contents merged = Backup.merge(here, sample());
        assertEquals(List.of("Dev", "Prod"), merged.folders());
        assertEquals(1, merged.hosts().size(), "same id: this PC's host stays");
        assertEquals("mine", merged.hosts().get(0).name);
        assertEquals("my-pw", merged.secrets().get("h1"));
        assertEquals("MINE", merged.secrets().get("sharing:private-key"), "this PC keeps its Team key");

        Backup.Contents other = new Backup.Contents("c@pc", 3, List.of(), List.of(host("h2", "db")), Map.of("h2", "db-pw"), List.of());
        Backup.Contents both = Backup.merge(here, other);
        assertEquals(2, both.hosts().size());
        assertEquals("db-pw", both.secrets().get("h2"));
        assertTrue(both.secrets().containsKey("h1"));
    }

    private static final String STRANGER = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl mallory";

    /** Analysis #12: a backup made by someone else cannot carry authorized_keys, rc or environment. */
    @Test
    void aBackupCannotCarryOtherFilesOfSsh() throws Exception {
        for (String name : List.of("authorized_keys", "authorized_keys2", "rc", "environment", "known_hosts2", "notes.txt", "id_x.pub")) {
            byte[] content = (name.endsWith(".pub") ? "not a key\nat all\n" : STRANGER + "\n").getBytes(StandardCharsets.UTF_8);
            Backup.Contents c = new Backup.Contents("x", 1, List.of(), List.of(), Map.of(), List.of(Backup.SshFile.of(name, content)));
            byte[] file = Backup.seal(c, "a long passphrase".toCharArray());
            IOException e = assertThrows(IOException.class, () -> Backup.open(file, "a long passphrase".toCharArray()), name);
            assertFalse(e instanceof Backup.WrongPassphrase, name);
        }
        // Keys, their .pub, known_hosts and config are fine.
        Backup.Contents ok = new Backup.Contents("x", 1, List.of(), List.of(), Map.of(), List.of(
                Backup.SshFile.of("id_ed25519", "-----BEGIN OPENSSH PRIVATE KEY-----\nabc\n".getBytes(StandardCharsets.US_ASCII)),
                Backup.SshFile.of("id_ed25519.pub", (STRANGER + "\n").getBytes(StandardCharsets.US_ASCII)),
                Backup.SshFile.of("known_hosts", ("host " + STRANGER + "\n").getBytes(StandardCharsets.US_ASCII)),
                Backup.SshFile.of("config", "Host x\n".getBytes(StandardCharsets.US_ASCII))));
        assertEquals(4, Backup.open(Backup.seal(ok, "a long passphrase".toCharArray()), "a long passphrase".toCharArray()).ssh().size());
    }

    /** Analysis #12: adding a backup to this PC does not bring in its contacts. */
    @Test
    void mergeLeavesTheContactsOfTheFileOut() {
        Backup.Contents here = new Backup.Contents("b@pc", 2, List.of(), List.of(), Map.of(), List.of());
        Backup.Contents file = new Backup.Contents("m@pc", 3, List.of(), List.of(), Map.of(Backup.CONTACTS, "[{\"key\":\"x\",\"name\":\"bruno\"}]", "h1", "pw"), List.of());
        Backup.Contents merged = Backup.merge(here, file);
        assertFalse(merged.secrets().containsKey(Backup.CONTACTS));
        assertEquals("pw", merged.secrets().get("h1"));
    }
}
