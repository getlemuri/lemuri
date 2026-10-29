package lemuri.be.storage;

import com.google.gson.Gson;
import com.google.gson.annotations.SerializedName;
import com.google.gson.reflect.TypeToken;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.GeneralSecurityException;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.Base64;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.logging.Logger;
import javax.crypto.AEADBadTagException;
import javax.crypto.Cipher;
import javax.crypto.SecretKey;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.SecretKeySpec;
import org.bouncycastle.crypto.generators.Argon2BytesGenerator;
import org.bouncycastle.crypto.params.Argon2Parameters;
import lemuri.be.service.Vault;
import lemuri.be.util.I18n;
import lemuri.be.util.Log;

/**
 * Host passwords, encrypted in ~/.lemuri/vault.json with AES-GCM.
 * The key is derived from the master password with Argon2id and stays only in
 * memory while the app is open (or until it is locked from the menu). The master
 * password is not saved anywhere: if it is lost, the passwords must be
 * entered again.
 *
 * <p>Since file version 2 the plain parameters (version, key derivation, salt) are part of
 * the AES-GCM AAD, so whoever touches them makes the file unreadable instead of
 * weakening it. Version 3 only renames things to English: the file, its fields, the
 * AAD label and the names of the entries. An older vault (cassaforte.json, version 1
 * without AAD or version 2) is rewritten as version 3 the first time it is unlocked
 * with the right password. Version 4 derives the key with Argon2id instead of PBKDF2:
 * memory-hard, so guessing the master password on a GPU costs far more. The older
 * versions are opened with PBKDF2 and rewritten as version 4. Version 5 only changes the AAD
 * label from the project's former name ("vpsman-vault") to "lemuri-vault".
 */
public final class FileVault implements Vault {
    private static final Path DIR = Path.of(System.getProperty("user.home"), ".lemuri");
    private static final Path FILE = DIR.resolve("vault.json");
    /** The file of the first versions: moved to {@link #FILE} if that does not exist yet. */
    private static final Path LEGACY_FILE = DIR.resolve("cassaforte.json");
    /** Entries saved by the first versions under Italian names, renamed at the first unlock. */
    private static final Map<String, String> LEGACY_ENTRIES = Map.of(
            "condivisione:privata", "sharing:private-key",
            "condivisione:pubblica", "sharing:public-key",
            "condivisione:buste-decise", "sharing:decided-envelopes",
            "lan:fidati", "lan:trusted");
    /**
     * Entries of the Firebase sharing, removed from Lemuri: deleted at the first unlock. The sign-in tokens
     * among them still gave access to the Google account's Firebase project.
     */
    private static final List<String> DROPPED_PREFIXES = List.of("firebase:", "sharing:colleague-keys", "sharing:verified",
            "condivisione:chiavi-colleghi", "condivisione:verificati");
    static final String ARGON2ID = "argon2id";
    /** Argon2id as recommended by RFC 9106 (second choice, for limited memory): 64 MiB, 3 passes, 4 lanes. About 0.2 s. */
    static final Kdf TODAY = new Kdf(ARGON2ID, 3, 64 * 1024, 4);
    static final int VERSION = 5;
    /** Beyond these, the file is broken or malicious: deriving the key would freeze the app or fill the memory. */
    private static final int MAX_PBKDF2_ITERATIONS = 10_000_000;
    private static final int MAX_PASSES = 8, MAX_MEMORY_KB = 256 * 1024, MAX_LANES = 16;
    private static final Logger LOG = Log.of(FileVault.class);

    /**
     * How the key comes from the master password. PBKDF2-HMAC-SHA256 (versions 1-3) has only {@code iterations};
     * Argon2id (version 4) has passes in {@code iterations}, memory in KiB and lanes.
     */
    record Kdf(String name, int iterations, int memoryKb, int lanes) {
        static Kdf pbkdf2(int iterations) {
            return new Kdf("pbkdf2", iterations, 0, 0);
        }

        boolean weakerThan(Kdf o) {
            return !name.equals(o.name) || iterations < o.iterations || memoryKb < o.memoryKb || lanes < o.lanes;
        }
    }
    /** What the data is encrypted with: a key and the salt and parameters it was derived with. */
    private record Sealing(SecretKey key, byte[] salt, Kdf kdf) {}
    private static final Gson GSON = new Gson();
    private static final SecureRandom RANDOM = new SecureRandom();

    /** File layout: everything except {@code data} is plain and is used to derive the key. */
    private static final class VaultFile {
        @SerializedName(value = "version", alternate = "versione") int version = 1;
        /** From version 4: "argon2id", with {@code memory} (KiB) and {@code lanes}. Before, always PBKDF2. */
        String kdf;
        @SerializedName(value = "iterations", alternate = "iterazioni") int iterations;
        Integer memory;
        Integer lanes;
        @SerializedName(value = "salt", alternate = "sale") String salt;

        Kdf kdf() {
            return version >= 4 ? new Kdf(kdf, iterations, memory != null ? memory : 0, lanes != null ? lanes : 0) : Kdf.pbkdf2(iterations);
        }
        String iv;
        @SerializedName(value = "data", alternate = "dati") String data;
    }

    private SecretKey key;
    /** How {@link #key} was derived: saved with it, not taken from {@link #TODAY}. */
    private Kdf kdf;
    private final List<Runnable> onUnlock = new CopyOnWriteArrayList<>();
    private final List<Runnable> onLock = new CopyOnWriteArrayList<>();
    private byte[] salt;
    private Map<String, String> secrets;
    /**
     * SHA-256 of the file as it was at the last read or write: if it changes underneath (another copy of
     * the app, a manual restore) it is not overwritten, or the passwords saved over there would be lost.
     */
    private byte[] fingerprint;

    public FileVault() {
        try {
            if (!Files.exists(FILE) && Files.exists(LEGACY_FILE)) Files.move(LEGACY_FILE, FILE);
        } catch (IOException e) {
            // it stays where it is: the vault looks missing until the move succeeds
        }
    }

    public synchronized boolean exists() {
        return Files.exists(FILE);
    }

    /** Notifies every time the vault is unlocked or created, from the thread that unlocked it. */
    public void onUnlock(Runnable r) {
        onUnlock.add(r);
    }

    @Override
    public void onLock(Runnable r) {
        onLock.add(r);
    }

    public synchronized boolean isOpen() {
        return key != null;
    }

    /** Creates an empty vault. Replaces the existing one. */
    public synchronized void create(char[] main) throws IOException {
        try {
            Sealing fresh = newKey(main);
            Map<String, String> empty = new HashMap<>();
            write(fresh, empty, false); // create replaces the existing one on purpose
            use(fresh);
            secrets = empty;
            onUnlock.forEach(Runnable::run);
        } catch (GeneralSecurityException e) {
            throw new IOException(e);
        }
    }

    /** False if the master password is wrong (or the file has been tampered with). */
    public synchronized boolean open(char[] main) throws IOException {
        byte[] raw = Files.readAllBytes(FILE);
        VaultFile c = read(new String(raw, StandardCharsets.UTF_8));
        try {
            byte[] s = Base64.getDecoder().decode(c.salt);
            Kdf f = c.kdf();
            SecretKey k = derive(main, s, f);
            Cipher ci = Cipher.getInstance("AES/GCM/NoPadding");
            ci.init(Cipher.DECRYPT_MODE, k, new GCMParameterSpec(128, Base64.getDecoder().decode(c.iv)));
            if (c.version >= 2) ci.updateAAD(aad(c.version, f, c.salt));
            String plain = new String(ci.doFinal(Base64.getDecoder().decode(c.data)), StandardCharsets.UTF_8);
            Map<String, String> m = GSON.fromJson(plain, new TypeToken<Map<String, String>>() {}.getType());
            use(new Sealing(k, s, f));
            secrets = m != null ? new HashMap<>(m) : new HashMap<>();
            fingerprint = sha256(raw);
            boolean changed = secrets.keySet().removeIf(name -> DROPPED_PREFIXES.stream().anyMatch(name::startsWith));
            for (var e : LEGACY_ENTRIES.entrySet()) {
                String v = secrets.remove(e.getKey());
                if (v != null) {
                    secrets.putIfAbsent(e.getValue(), v);
                    changed = true;
                }
            }
            // Old format, old names or a key weaker than today's: rewrite it now, while we have the password.
            // If it cannot be written the vault stays open as it is, and is rewritten at the next unlock.
            if (c.version < VERSION || f.weakerThan(TODAY) || changed) {
                Sealing fresh = newKey(main);
                try {
                    write(fresh, secrets, true);
                    use(fresh);
                } catch (IOException e) {
                    LOG.warning("Cannot rewrite the vault in the current format: " + e.getMessage());
                }
            }
            onUnlock.forEach(Runnable::run);
            return true;
        } catch (AEADBadTagException e) {
            return false;
        } catch (GeneralSecurityException | IllegalArgumentException e) {
            throw new IOException(e);
        }
    }

    /**
     * Changes the master password: new salt and key, same passwords inside.
     * False if {@code current} is wrong. It must be unlocked first.
     */
    @Override
    public synchronized boolean changePassword(char[] current, char[] fresh) throws IOException {
        if (key == null) throw new IllegalStateException(I18n.t("sharing.state.vaultLocked"));
        try {
            byte[] verify = derive(current, salt, kdf).getEncoded();
            if (!MessageDigest.isEqual(verify, key.getEncoded())) return false;
            // The new key is used only once the file encrypted with it is on disk: if writing fails,
            // the user is told so and the old password must keep working, now and at the next save.
            Sealing next = newKey(fresh);
            write(next, secrets, true);
            use(next);
            return true;
        } catch (GeneralSecurityException e) {
            throw new IOException(e);
        }
    }

    /**
     * Forgets the key: the next saved password will need the master password again.
     * Listeners (sharing) in turn forget what they took from here.
     */
    public void lock() {
        synchronized (this) {
            key = null;
            salt = null;
            kdf = null;
            secrets = null;
            fingerprint = null;
        }
        // Outside the lock: listeners may want to read the vault again.
        onLock.forEach(Runnable::run);
    }

    public synchronized String password(String hostId) {
        return secrets == null ? null : secrets.get(hostId);
    }

    public synchronized void remember(String hostId, String password) throws IOException {
        if (secrets == null) throw new IllegalStateException(I18n.t("sharing.state.vaultLocked"));
        if (password.equals(secrets.put(hostId, password))) return;
        write(new Sealing(key, salt, kdf), secrets, true);
    }

    public synchronized void forget(String hostId) throws IOException {
        if (secrets != null && secrets.remove(hostId) != null) write(new Sealing(key, salt, kdf), secrets, true);
    }

    @Override
    public synchronized Map<String, String> entries() {
        return secrets == null ? Map.of() : Map.copyOf(secrets);
    }

    /** Replaces every entry in one save (an import); in memory only once the file is written. */
    @Override
    public synchronized void replaceAll(Map<String, String> entries) throws IOException {
        if (secrets == null) throw new IllegalStateException(I18n.t("sharing.state.vaultLocked"));
        Map<String, String> next = new HashMap<>(entries);
        write(new Sealing(key, salt, kdf), next, true);
        secrets = next;
    }

    /** New salt and a key derived with today's parameters; not in use until {@link #use}. */
    private static Sealing newKey(char[] main) throws GeneralSecurityException {
        byte[] s = new byte[16];
        RANDOM.nextBytes(s);
        return new Sealing(derive(main, s, TODAY), s, TODAY);
    }

    private void use(Sealing s) {
        key = s.key();
        salt = s.salt();
        kdf = s.kdf();
    }

    private static VaultFile read(String text) throws IOException {
        VaultFile c;
        try {
            c = GSON.fromJson(text, VaultFile.class);
        } catch (RuntimeException e) {
            throw new IOException(I18n.t("vault.err.damagedWhy", e.getMessage()), e);
        }
        if (c == null || c.salt == null || c.iv == null || c.data == null) throw new IOException(I18n.t("vault.err.damaged"));
        if (c.version > VERSION) throw new IOException(I18n.t("vault.err.newer"));
        if (c.version < 4) {
            if (c.iterations < 1 || c.iterations > MAX_PBKDF2_ITERATIONS) throw new IOException(I18n.t("vault.err.iterations", c.iterations));
        } else {
            checkArgon2(c.kdf());
        }
        return c;
    }

    /** Argon2id parameters read from a file: beyond the limits the file is broken or malicious. Also for backups. */
    static void checkArgon2(Kdf f) throws IOException {
        // Argon2 needs at least 8 KiB per lane.
        if (!ARGON2ID.equals(f.name()) || f.iterations() < 1 || f.iterations() > MAX_PASSES || f.lanes() < 1 || f.lanes() > MAX_LANES
                || f.memoryKb() < 8 * f.lanes() || f.memoryKb() > MAX_MEMORY_KB)
            throw new IOException(I18n.t("vault.err.damagedWhy", f.name() + " t=" + f.iterations() + " m=" + f.memoryKb() + " p=" + f.lanes()));
    }

    /**
     * The plain parameters of the file, bound to the encrypted data. Versions 2-4 used the project's former
     * name in the label (version 2 in Italian).
     */
    private static byte[] aad(int version, Kdf f, String salt) {
        String label = version == 2 ? "vpsman-cassaforte" : version <= 4 ? "vpsman-vault" : "lemuri-vault";
        String params = version >= 4 ? f.name() + "|" + f.iterations() + "|" + f.memoryKb() + "|" + f.lanes() : String.valueOf(f.iterations());
        return (label + "|" + version + "|" + params + "|" + salt).getBytes(StandardCharsets.UTF_8);
    }

    static SecretKey derive(char[] main, byte[] salt, Kdf f) throws GeneralSecurityException {
        if (ARGON2ID.equals(f.name())) {
            Argon2BytesGenerator g = new Argon2BytesGenerator();
            g.init(new Argon2Parameters.Builder(Argon2Parameters.ARGON2_id).withVersion(Argon2Parameters.ARGON2_VERSION_13)
                    .withIterations(f.iterations()).withMemoryAsKB(f.memoryKb()).withParallelism(f.lanes()).withSalt(salt).build());
            byte[] k = new byte[32];
            g.generateBytes(main, k);
            try {
                return new SecretKeySpec(k, "AES");
            } finally {
                Arrays.fill(k, (byte) 0);
            }
        }
        PBEKeySpec spec = new PBEKeySpec(main, salt, f.iterations(), 256);
        try {
            byte[] k = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).getEncoded();
            return new SecretKeySpec(k, "AES");
        } finally {
            spec.clearPassword();
        }
    }

    private static byte[] sha256(byte[] data) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(data);
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException(e);
        }
    }

    /**
     * Every save uses a new IV; the file is replaced in one go and is readable by the user only.
     * With {@code check}, if the file changed in the meantime (see {@link #fingerprint}) nothing is written.
     */
    private void write(Sealing seal, Map<String, String> secrets, boolean check) throws IOException {
        if (check && fingerprint != null && Files.exists(FILE) && !MessageDigest.isEqual(sha256(Files.readAllBytes(FILE)), fingerprint))
            throw new IOException(I18n.t("vault.err.changedElsewhere"));
        Kdf kdf = seal.kdf();
        try {
            byte[] iv = new byte[12];
            RANDOM.nextBytes(iv);
            VaultFile c = new VaultFile();
            c.version = VERSION;
            c.kdf = kdf.name();
            c.iterations = kdf.iterations();
            c.memory = kdf.memoryKb();
            c.lanes = kdf.lanes();
            c.salt = Base64.getEncoder().encodeToString(seal.salt());
            c.iv = Base64.getEncoder().encodeToString(iv);
            Cipher ci = Cipher.getInstance("AES/GCM/NoPadding");
            ci.init(Cipher.ENCRYPT_MODE, seal.key(), new GCMParameterSpec(128, iv));
            ci.updateAAD(aad(c.version, kdf, c.salt));
            c.data = Base64.getEncoder().encodeToString(ci.doFinal(GSON.toJson(secrets).getBytes(StandardCharsets.UTF_8)));
            Files.createDirectories(DIR);
            Path tmp = DIR.resolve("vault.json.tmp");
            byte[] created = GSON.toJson(c).getBytes(StandardCharsets.UTF_8);
            Files.deleteIfExists(tmp);
            try {
                // Born readable by the user only, not with the umask's permissions and changed afterwards.
                Files.createFile(tmp, PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------")));
            } catch (UnsupportedOperationException e) {
                // File system without POSIX permissions: the encryption remains.
            }
            Files.write(tmp, created);
            Files.move(tmp, FILE, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
            fingerprint = sha256(created);
        } catch (GeneralSecurityException e) {
            throw new IOException(e);
        }
    }
}
