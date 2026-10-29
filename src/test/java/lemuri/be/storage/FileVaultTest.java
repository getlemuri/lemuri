package lemuri.be.storage;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.SecureRandom;
import java.util.Base64;
import javax.crypto.Cipher;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.SecretKeySpec;
import org.junit.jupiter.api.Assumptions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/** Runs with user.home inside target (see the pom): it never touches the real vault. */
class FileVaultTest {
    private static final Path FILE = Path.of(System.getProperty("user.home"), ".lemuri", "vault.json");
    /** Where the first versions saved the vault, with Italian field names. */
    private static final Path LEGACY = FILE.resolveSibling("cassaforte.json");

    @BeforeEach
    void clear() throws Exception {
        assertTrue(FILE.toString().contains("target"), "the tests must not use the real home: " + FILE);
        Files.deleteIfExists(FILE);
        Files.deleteIfExists(LEGACY);
    }

    @Test
    void remembersAndReturnsPasswordsAfterReopening() throws Exception {
        FileVault c = new FileVault();
        c.create("master".toCharArray());
        c.remember("host1", "pw1");
        c.lock();
        assertFalse(c.isOpen());
        assertNull(c.password("host1"));

        FileVault d = new FileVault();
        assertTrue(d.open("master".toCharArray()));
        assertEquals("pw1", d.password("host1"));
    }

    /** The case from the review: two copies of the app open, each saves a password; none must disappear. */
    @Test
    void twoCopiesDoNotDeleteThePasswords() throws Exception {
        new FileVault().create("master".toCharArray());
        FileVault one = new FileVault(), other = new FileVault();
        assertTrue(one.open("master".toCharArray()));
        assertTrue(other.open("master".toCharArray()));
        one.remember("host-A", "pwA");
        assertThrows(IOException.class, () -> other.remember("host-B", "pwB"), "the second copy must not overwrite the file");

        FileVault after = new FileVault();
        assertTrue(after.open("master".toCharArray()));
        assertEquals("pwA", after.password("host-A"));
        // Unlocked again, the other copy saves too without losing anything.
        other.lock();
        assertTrue(other.open("master".toCharArray()));
        other.remember("host-B", "pwB");
        assertTrue(after.open("master".toCharArray()));
        assertEquals("pwA", after.password("host-A"));
        assertEquals("pwB", after.password("host-B"));
    }

    @Test
    void lockingNotifiesListeners() throws Exception {
        FileVault c = new FileVault();
        c.create("master".toCharArray());
        int[] warnings = {0};
        c.onLock(() -> {
            assertFalse(c.isOpen(), "when the listeners run, the secrets are already gone");
            warnings[0]++;
        });
        c.lock();
        assertEquals(1, warnings[0]);
    }

    @Test
    void wrongPasswordDoesNotOpen() throws Exception {
        new FileVault().create("master".toCharArray());
        FileVault c = new FileVault();
        assertFalse(c.open("wrong".toCharArray()));
        assertFalse(c.isOpen());
    }

    @Test
    void fileHasNoPlainPasswordsAndIsPrivate() throws Exception {
        FileVault c = new FileVault();
        c.create("master".toCharArray());
        c.remember("host1", "a-very-recognisable-password");
        assertFalse(Files.readString(FILE).contains("a-very-recognisable-password"));
        assertEquals("rw-------", PosixFilePermissions.toString(Files.getPosixFilePermissions(FILE)));
    }

    @Test
    void aTamperedFileDoesNotOpen() throws Exception {
        FileVault c = new FileVault();
        c.create("master".toCharArray());
        c.remember("host1", "pw1");
        // One bit changed in the encrypted data: AES-GCM notices.
        JsonObject j = JsonParser.parseString(Files.readString(FILE)).getAsJsonObject();
        byte[] data = Base64.getDecoder().decode(j.get("data").getAsString());
        data[0] ^= 1;
        j.addProperty("data", Base64.getEncoder().encodeToString(data));
        Files.writeString(FILE, j.toString());
        assertFalse(new FileVault().open("master".toCharArray()));
    }

    /** A file as version 1 wrote it: 310,000 iterations and no AAD. */
    private static void writeVersion1(String main, String secretsJson) throws Exception {
        byte[] salt = new byte[16], iv = new byte[12];
        new SecureRandom().nextBytes(salt);
        new SecureRandom().nextBytes(iv);
        PBEKeySpec spec = new PBEKeySpec(main.toCharArray(), salt, 310_000, 256);
        byte[] k = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).getEncoded();
        Cipher ci = Cipher.getInstance("AES/GCM/NoPadding");
        ci.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(k, "AES"), new GCMParameterSpec(128, iv));
        JsonObject j = new JsonObject();
        j.addProperty("versione", 1);
        j.addProperty("iterazioni", 310_000);
        j.addProperty("sale", Base64.getEncoder().encodeToString(salt));
        j.addProperty("iv", Base64.getEncoder().encodeToString(iv));
        j.addProperty("dati", Base64.getEncoder().encodeToString(ci.doFinal(secretsJson.getBytes(StandardCharsets.UTF_8))));
        Files.createDirectories(LEGACY.getParent());
        Files.writeString(LEGACY, j.toString());
    }

    private static JsonObject file() throws Exception {
        return JsonParser.parseString(Files.readString(FILE)).getAsJsonObject();
    }

    @Test
    void anOldVaultIsUpgradedOnFirstOpen() throws Exception {
        writeVersion1("master", "{\"host1\":\"pw1\"}");
        FileVault c = new FileVault();
        assertTrue(c.open("master".toCharArray()));
        assertEquals("pw1", c.password("host1"));
        assertFalse(Files.exists(LEGACY));
        assertEquals(FileVault.VERSION, file().get("version").getAsInt());
        assertEquals(FileVault.ARGON2ID, file().get("kdf").getAsString());
        assertEquals(FileVault.TODAY.memoryKb(), file().get("memory").getAsInt());
        // Unlocked with the new format, it gives the same passwords; and a later write does not break it.
        c.remember("host2", "pw2");
        FileVault d = new FileVault();
        assertTrue(d.open("master".toCharArray()));
        assertEquals("pw1", d.password("host1"));
        assertEquals("pw2", d.password("host2"));
    }

    @Test
    void anOldVaultWithTheWrongPasswordStays() throws Exception {
        writeVersion1("master", "{}");
        new FileVault(); // moves it to vault.json, unchanged
        String before = Files.readString(FILE);
        assertFalse(new FileVault().open("wrong".toCharArray()));
        assertEquals(before, Files.readString(FILE));
    }

    @Test
    void plainParametersAreProtected() throws Exception {
        new FileVault().create("master".toCharArray());
        // Lowering the passes or going back to version 1 (without AAD) makes the file unreadable, not weaker.
        JsonObject j = file();
        j.addProperty("iterations", 1);
        Files.writeString(FILE, j.toString());
        assertFalse(new FileVault().open("master".toCharArray()));
        new FileVault().create("master".toCharArray());
        j = file();
        j.addProperty("version", 1);
        Files.writeString(FILE, j.toString());
        assertFalse(new FileVault().open("master".toCharArray()));
    }

    @Test
    void aFileFromAFutureVersionOrBrokenIsRejected() throws Exception {
        new FileVault().create("master".toCharArray());
        JsonObject j = file();
        j.addProperty("version", 99);
        Files.writeString(FILE, j.toString());
        assertThrows(IOException.class, () -> new FileVault().open("master".toCharArray()));
        j.addProperty("version", 2);
        j.addProperty("iterations", 2_000_000_000);
        Files.writeString(FILE, j.toString());
        assertThrows(IOException.class, () -> new FileVault().open("master".toCharArray()));
        Files.writeString(FILE, "this is not json");
        assertThrows(IOException.class, () -> new FileVault().open("master".toCharArray()));
    }

    /** A file that asks for 64 GiB of memory, or an unknown derivation, is refused before deriving anything. */
    @Test
    void argon2ParametersOutOfRangeAreRejected() throws Exception {
        for (String[] change : new String[][] {{"memory", "67108864"}, {"memory", "4"}, {"iterations", "1000"}, {"lanes", "0"}, {"kdf", "scrypt"}}) {
            new FileVault().create("master".toCharArray());
            JsonObject j = file();
            if (change[0].equals("kdf")) j.addProperty("kdf", change[1]);
            else j.addProperty(change[0], Integer.parseInt(change[1]));
            Files.writeString(FILE, j.toString());
            assertThrows(IOException.class, () -> new FileVault().open("master".toCharArray()), change[0]);
        }
    }

    /** Lowering the memory within the limits does not weaken the vault: the parameters are in the AAD. */
    @Test
    void lowerArgon2MemoryMakesItUnreadable() throws Exception {
        new FileVault().create("master".toCharArray());
        JsonObject j = file();
        j.addProperty("memory", 1024);
        Files.writeString(FILE, j.toString());
        assertFalse(new FileVault().open("master".toCharArray()));
    }

    /** Version 3 (PBKDF2, 600,000 iterations): opens with the same password and is rewritten with Argon2id. */
    @Test
    void aPbkdf2VaultIsMovedToArgon2() throws Exception {
        byte[] salt = new byte[16], iv = new byte[12];
        new SecureRandom().nextBytes(salt);
        new SecureRandom().nextBytes(iv);
        String saltB64 = Base64.getEncoder().encodeToString(salt);
        PBEKeySpec spec = new PBEKeySpec("master".toCharArray(), salt, 600_000, 256);
        byte[] k = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).getEncoded();
        Cipher ci = Cipher.getInstance("AES/GCM/NoPadding");
        ci.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(k, "AES"), new GCMParameterSpec(128, iv));
        ci.updateAAD(("vpsman-vault|3|600000|" + saltB64).getBytes(StandardCharsets.UTF_8));
        JsonObject j = new JsonObject();
        j.addProperty("version", 3);
        j.addProperty("iterations", 600_000);
        j.addProperty("salt", saltB64);
        j.addProperty("iv", Base64.getEncoder().encodeToString(iv));
        j.addProperty("data", Base64.getEncoder().encodeToString(ci.doFinal("{\"host1\":\"pw1\"}".getBytes(StandardCharsets.UTF_8))));
        Files.createDirectories(FILE.getParent());
        Files.writeString(FILE, j.toString());

        FileVault c = new FileVault();
        assertTrue(c.open("master".toCharArray()));
        assertEquals("pw1", c.password("host1"));
        assertEquals(FileVault.VERSION, file().get("version").getAsInt());
        assertEquals(FileVault.ARGON2ID, file().get("kdf").getAsString());
        assertTrue(new FileVault().open("master".toCharArray()));
        assertFalse(new FileVault().open("wrong".toCharArray()));
    }

    /** Version 4 (Argon2id, label with the former name "vpsman"): opens and is rewritten with the "lemuri-vault" label. */
    @Test
    void aVersion4VaultGetsTheNewLabel() throws Exception {
        byte[] salt = new byte[16], iv = new byte[12];
        new SecureRandom().nextBytes(salt);
        new SecureRandom().nextBytes(iv);
        String saltB64 = Base64.getEncoder().encodeToString(salt);
        FileVault.Kdf f = FileVault.TODAY;
        Cipher ci = Cipher.getInstance("AES/GCM/NoPadding");
        ci.init(Cipher.ENCRYPT_MODE, FileVault.derive("master".toCharArray(), salt, f), new GCMParameterSpec(128, iv));
        ci.updateAAD(("vpsman-vault|4|" + f.name() + "|" + f.iterations() + "|" + f.memoryKb() + "|" + f.lanes() + "|" + saltB64)
                .getBytes(StandardCharsets.UTF_8));
        JsonObject j = new JsonObject();
        j.addProperty("version", 4);
        j.addProperty("kdf", f.name());
        j.addProperty("iterations", f.iterations());
        j.addProperty("memory", f.memoryKb());
        j.addProperty("lanes", f.lanes());
        j.addProperty("salt", saltB64);
        j.addProperty("iv", Base64.getEncoder().encodeToString(iv));
        j.addProperty("data", Base64.getEncoder().encodeToString(ci.doFinal("{\"host1\":\"pw1\"}".getBytes(StandardCharsets.UTF_8))));
        Files.createDirectories(FILE.getParent());
        Files.writeString(FILE, j.toString());

        FileVault c = new FileVault();
        assertTrue(c.open("master".toCharArray()));
        assertEquals("pw1", c.password("host1"));
        JsonObject now = file();
        assertEquals(5, now.get("version").getAsInt());
        byte[] newSalt = Base64.getDecoder().decode(now.get("salt").getAsString());
        Cipher check = Cipher.getInstance("AES/GCM/NoPadding");
        check.init(Cipher.DECRYPT_MODE, FileVault.derive("master".toCharArray(), newSalt, f),
                new GCMParameterSpec(128, Base64.getDecoder().decode(now.get("iv").getAsString())));
        check.updateAAD(("lemuri-vault|5|" + f.name() + "|" + f.iterations() + "|" + f.memoryKb() + "|" + f.lanes() + "|" + now.get("salt").getAsString())
                .getBytes(StandardCharsets.UTF_8));
        assertEquals("{\"host1\":\"pw1\"}", new String(check.doFinal(Base64.getDecoder().decode(now.get("data").getAsString())), StandardCharsets.UTF_8));
    }

    @Test
    void changingTheMasterPassword() throws Exception {
        FileVault c = new FileVault();
        c.create("old".toCharArray());
        c.remember("host1", "pw1");
        assertFalse(c.changePassword("wrong".toCharArray(), "new".toCharArray()));
        assertTrue(c.changePassword("old".toCharArray(), "new".toCharArray()));
        assertFalse(new FileVault().open("old".toCharArray()));
        FileVault d = new FileVault();
        assertTrue(d.open("new".toCharArray()));
        assertEquals("pw1", d.password("host1"));
    }

    /** Entries saved by the first versions under Italian names keep their values under the English ones. */
    @Test
    void oldEntryNamesAreRenamedOnFirstOpen() throws Exception {
        writeVersion1("master", "{\"lan:fidati\":\"[]\",\"condivisione:privata\":\"k\",\"host1\":\"pw1\"}");
        assertTrue(new FileVault().open("master".toCharArray()));
        FileVault d = new FileVault();
        assertTrue(d.open("master".toCharArray()));
        assertEquals("[]", d.password("lan:trusted"));
        assertEquals("k", d.password("sharing:private-key"));
        assertNull(d.password("lan:fidati"));
        assertEquals("pw1", d.password("host1"));
    }

    /** The Firebase sharing is gone: its sign-in token and colleague lists are deleted at the first unlock. */
    @Test
    void firebaseEntriesAreDeletedOnFirstOpen() throws Exception {
        writeVersion1("master", "{\"firebase:refresh:proj\":\"token\",\"sharing:colleague-keys\":\"{}\",\"sharing:verified\":\"x\",\"host1\":\"pw1\"}");
        assertTrue(new FileVault().open("master".toCharArray()));
        assertFalse(Files.readString(FILE).contains("token"));
        FileVault d = new FileVault();
        assertTrue(d.open("master".toCharArray()));
        assertNull(d.password("firebase:refresh:proj"));
        assertNull(d.password("sharing:colleague-keys"));
        assertNull(d.password("sharing:verified"));
        assertEquals("pw1", d.password("host1"));
    }

    /**
     * Analysis #10: a password change that cannot be written leaves the old password in force, also for the
     * saves that follow; before, the next save encrypted the file with the "failed" new password.
     */
    @Test
    void aFailedPasswordChangeKeepsTheOldPassword() throws Exception {
        // The failure comes from a folder without write permission: root writes there all the same (a container).
        Assumptions.assumeFalse("root".equals(System.getProperty("user.name")), "root ignores the permissions this test relies on");
        FileVault v = new FileVault();
        v.create("old".toCharArray());
        v.remember("h1", "p1");
        Path dir = FILE.getParent();
        Files.setPosixFilePermissions(dir, PosixFilePermissions.fromString("r-x------"));
        try {
            assertThrows(IOException.class, () -> v.changePassword("old".toCharArray(), "new".toCharArray()));
        } finally {
            Files.setPosixFilePermissions(dir, PosixFilePermissions.fromString("rwx------"));
        }
        v.remember("h2", "p2");
        FileVault again = new FileVault();
        assertFalse(again.open("new".toCharArray()));
        assertTrue(again.open("old".toCharArray()));
        assertEquals("p2", again.password("h2"));
    }
}
