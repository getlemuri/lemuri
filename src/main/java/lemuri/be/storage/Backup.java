package lemuri.be.storage;

import com.google.gson.Gson;
import com.google.gson.JsonParseException;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Base64;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import javax.crypto.AEADBadTagException;
import javax.crypto.Cipher;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import lemuri.be.dto.Host;
import lemuri.be.util.I18n;
import lemuri.be.util.SshFolder;

/**
 * Everything of a Lemuri, to move it to another PC: hosts, folders, every entry of the vault (passwords,
 * the sharing key, contacts) and the files of ~/.ssh the user chose. One file, encrypted like the vault
 * (Argon2id and AES-GCM, the plain parameters in the AAD) but with a passphrase chosen for the transfer,
 * so it can travel on a USB stick or by email.
 */
public final class Backup {
    static final String FORMAT = "lemuri-backup";
    static final int VERSION = 1;
    /** Hosts and a few keys weigh a few hundred KB: a bigger file is not a backup of ours. */
    public static final int MAX_FILE = 16 * 1024 * 1024;
    /** A passphrase shorter than this is refused when exporting: the file may end up anywhere. */
    public static final int MIN_PASSPHRASE = 10;
    private static final Gson GSON = new Gson();
    private static final SecureRandom RANDOM = new SecureRandom();

    private Backup() {}

    /** A file of ~/.ssh: its bare name and its content in base64. */
    public record SshFile(String name, String data) {
        public static SshFile of(String name, byte[] content) {
            return new SshFile(name, Base64.getEncoder().encodeToString(content));
        }

        public byte[] bytes() {
            return Base64.getDecoder().decode(data);
        }
    }

    /** What is inside: where and when it was made, and the data. Missing lists (a file made by hand) become empty. */
    public record Contents(String from, long created, List<String> folders, List<Host> hosts,
            Map<String, String> secrets, List<SshFile> ssh) {
        public Contents {
            from = from != null ? from : "?";
            folders = folders != null ? List.copyOf(folders) : List.of();
            hosts = hosts != null ? List.copyOf(hosts) : List.of();
            secrets = secrets != null ? Map.copyOf(secrets) : Map.of();
            ssh = ssh != null ? List.copyOf(ssh) : List.of();
        }
    }

    /** The passphrase is wrong, or the file was changed after it was made. */
    public static final class WrongPassphrase extends IOException {
        WrongPassphrase() {
            super(I18n.t("backup.wrongPassphrase"));
        }
    }

    /** File layout: everything except {@code data} is plain and is used to derive the key. */
    private static final class BackupFile {
        String format;
        int version;
        String kdf;
        int iterations;
        int memory;
        int lanes;
        String salt;
        String iv;
        String data;
    }

    /** The encrypted file, ready to be written. */
    public static byte[] seal(Contents c, char[] passphrase) throws IOException {
        try {
            byte[] salt = new byte[16], iv = new byte[12];
            RANDOM.nextBytes(salt);
            RANDOM.nextBytes(iv);
            FileVault.Kdf kdf = FileVault.TODAY;
            BackupFile f = new BackupFile();
            f.format = FORMAT;
            f.version = VERSION;
            f.kdf = kdf.name();
            f.iterations = kdf.iterations();
            f.memory = kdf.memoryKb();
            f.lanes = kdf.lanes();
            f.salt = Base64.getEncoder().encodeToString(salt);
            f.iv = Base64.getEncoder().encodeToString(iv);
            Cipher ci = Cipher.getInstance("AES/GCM/NoPadding");
            ci.init(Cipher.ENCRYPT_MODE, FileVault.derive(passphrase, salt, kdf), new GCMParameterSpec(128, iv));
            ci.updateAAD(aad(f));
            f.data = Base64.getEncoder().encodeToString(ci.doFinal(GSON.toJson(c).getBytes(StandardCharsets.UTF_8)));
            return GSON.toJson(f).getBytes(StandardCharsets.UTF_8);
        } catch (GeneralSecurityException e) {
            throw new IOException(e);
        }
    }

    /**
     * Opens a backup: {@link WrongPassphrase} if the passphrase is wrong (or the file was touched), an
     * IOException with the reason if it is not a backup or its content is not valid.
     */
    public static Contents open(byte[] file, char[] passphrase) throws IOException {
        if (file.length > MAX_FILE) throw new IOException(I18n.t("backup.notABackup"));
        BackupFile f;
        try {
            f = GSON.fromJson(new String(file, StandardCharsets.UTF_8), BackupFile.class);
        } catch (JsonParseException e) {
            throw new IOException(I18n.t("backup.notABackup"), e);
        }
        if (f == null || !FORMAT.equals(f.format) || f.salt == null || f.iv == null || f.data == null)
            throw new IOException(I18n.t("backup.notABackup"));
        if (f.version > VERSION) throw new IOException(I18n.t("backup.newer"));
        FileVault.Kdf kdf = new FileVault.Kdf(f.kdf, f.iterations, f.memory, f.lanes);
        FileVault.checkArgon2(kdf);
        String plain;
        try {
            SecretKey k = FileVault.derive(passphrase, Base64.getDecoder().decode(f.salt), kdf);
            Cipher ci = Cipher.getInstance("AES/GCM/NoPadding");
            ci.init(Cipher.DECRYPT_MODE, k, new GCMParameterSpec(128, Base64.getDecoder().decode(f.iv)));
            ci.updateAAD(aad(f));
            plain = new String(ci.doFinal(Base64.getDecoder().decode(f.data)), StandardCharsets.UTF_8);
        } catch (AEADBadTagException e) {
            throw new WrongPassphrase();
        } catch (GeneralSecurityException | IllegalArgumentException e) {
            throw new IOException(I18n.t("backup.notABackup"), e);
        }
        Contents c;
        try {
            c = GSON.fromJson(plain, Contents.class);
        } catch (RuntimeException e) {
            // Gson's own errors, or the record refusing null elements: whoever wrote the file wrote it wrong.
            throw new IOException(I18n.t("backup.notABackup"), e);
        }
        if (c == null) throw new IOException(I18n.t("backup.notABackup"));
        check(c);
        return c;
    }

    /**
     * Hosts without an id are refused, and so is a backup with a file of ~/.ssh that is not one of those it may
     * carry (keys, known_hosts, config: see SshFolder.kind): whoever made the file must not be able to plant
     * authorized_keys, rc or environment here.
     */
    private static void check(Contents c) throws IOException {
        for (Host h : c.hosts()) if (h.id == null || h.id.isBlank()) throw new IOException(I18n.t("backup.notABackup"));
        for (SshFile s : c.ssh()) {
            if (s.data() == null || !SshFolder.safeName(s.name())) throw new IOException(I18n.t("backup.badSshName", s.name()));
            byte[] b;
            try {
                b = s.bytes();
            } catch (IllegalArgumentException e) {
                throw new IOException(I18n.t("backup.notABackup"), e);
            }
            if (SshFolder.kind(s.name(), b) == null) throw new IOException(I18n.t("backup.sshNotAllowed", s.name()));
        }
    }

    /** The vault entry with the contacts (Lan's list of trusted keys). */
    public static final String CONTACTS = "lan:trusted";

    /**
     * The import that keeps what is here: hosts and vault entries of this PC stay as they are, those of the
     * file are added only if missing (same host id, same entry name: this PC's one wins). So this PC keeps
     * its sharing key; the folders are put together. The contacts of the file are never added this way:
     * a contact is a key trusted after comparing its fingerprint, not something that arrives in a file.
     */
    public static Contents merge(Contents here, Contents imported) {
        Set<String> ids = new HashSet<>();
        List<Host> hosts = new ArrayList<>(here.hosts());
        for (Host h : here.hosts()) ids.add(h.id);
        for (Host h : imported.hosts()) if (ids.add(h.id)) hosts.add(h);
        Set<String> folders = new LinkedHashSet<>(here.folders());
        folders.addAll(imported.folders());
        Map<String, String> secrets = new HashMap<>(imported.secrets());
        secrets.remove(CONTACTS);
        secrets.putAll(here.secrets());
        return new Contents(here.from(), here.created(), new ArrayList<>(folders), hosts, secrets, imported.ssh());
    }

    private static byte[] aad(BackupFile f) {
        return (FORMAT + "|" + f.version + "|" + f.kdf + "|" + f.iterations + "|" + f.memory + "|" + f.lanes + "|" + f.salt)
                .getBytes(StandardCharsets.UTF_8);
    }
}
